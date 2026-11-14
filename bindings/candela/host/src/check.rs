//! `drift check`: what loading a game would refuse or warn about, as
//! diagnostics an editor can place, without building or running any of the
//! game.
//!
//! The game is read the way a load reads it (`read_game`), so a check and a
//! run never disagree about a system, a query or a settings struct. On top of
//! that it reads the game's own files for two things a load only meets while
//! running: a quoted path passed to a `drift` function that loads a file,
//! naming no file, and a quoted name passed to one that reads an input
//! action, naming no action.

use super::{bare, call, read_game, Finding, Item, Unread};
use candela::Diagnostic;
use serde_json::{json, Value as Json};
use std::collections::HashSet;
use std::path::{Path, PathBuf};

/// One problem, placed in a file. Lines and columns count from 1; a column
/// counts characters.
struct Placed {
    file: PathBuf,
    line: usize,
    column: usize,
    end_line: usize,
    end_column: usize,
    severity: &'static str,
    code: String,
    message: String,
}

impl Placed {
    /// The diagnostic as JSON. A column of 0 covers the whole line, and the
    /// message leaves out the `drift_...: ` code it starts with, which has a
    /// key of its own.
    fn json(&self) -> Json {
        let message = self.message.strip_prefix(&format!("{}: ", self.code)).unwrap_or(&self.message);
        let mut d = json!({
            "file": self.file.to_string_lossy(),
            "line": self.line,
            "severity": self.severity,
            "code": self.code,
            "message": message,
        });
        if self.column > 0 {
            d["column"] = json!(self.column);
            d["end_line"] = json!(self.end_line);
            d["end_column"] = json!(self.end_column);
        }
        d
    }
}

/// A game file's text.
struct Source {
    path: PathBuf,
    text: String,
}

impl Source {
    /// Line and column of byte `offset`, from 1, the column in characters.
    fn place(&self, offset: usize) -> (usize, usize) {
        let offset = offset.min(self.text.len());
        let before = &self.text[..offset];
        let line = before.matches('\n').count() + 1;
        let start = before.rfind('\n').map_or(0, |i| i + 1);
        (line, self.text[start..offset].chars().count() + 1)
    }
}

/// The game's `.cdl` files: every one under its folder but hidden folders
/// and build output, the files `drift run` watches.
fn game_sources(dir: &Path) -> Vec<Source> {
    let mut out = Vec::new();
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        let Ok(entries) = std::fs::read_dir(&d) else { continue };
        let mut entries: Vec<_> = entries.flatten().collect();
        entries.sort_by_key(|e| e.file_name());
        for e in entries {
            let path = e.path();
            let name = e.file_name().to_string_lossy().into_owned();
            if path.is_dir() {
                if !(name.starts_with('.') || name == "build" || name.starts_with("build-")) {
                    stack.push(path);
                }
            } else if path.extension().is_some_and(|x| x == "cdl") {
                if let Ok(text) = std::fs::read_to_string(&path) {
                    out.push(Source { path, text });
                }
            }
        }
    }
    out
}

fn is_word(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

/// The byte ranges of `text` outside comments and string literals, as a mask.
fn code_mask(text: &str) -> Vec<bool> {
    let b = text.as_bytes();
    let mut mask = vec![true; b.len()];
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'/' && b.get(i + 1) == Some(&b'/') {
            while i < b.len() && b[i] != b'\n' {
                mask[i] = false;
                i += 1;
            }
        } else if b[i] == b'/' && b.get(i + 1) == Some(&b'*') {
            while i < b.len() && !(b[i] == b'*' && b.get(i + 1) == Some(&b'/')) {
                mask[i] = false;
                i += 1;
            }
            for _ in 0..2 {
                if i < b.len() {
                    mask[i] = false;
                    i += 1;
                }
            }
        } else if b[i] == b'"' {
            mask[i] = false;
            i += 1;
            while i < b.len() && b[i] != b'"' {
                if b[i] == b'\\' {
                    mask[i] = false;
                    i += 1;
                }
                if i < b.len() {
                    mask[i] = false;
                    i += 1;
                }
            }
            if i < b.len() {
                mask[i] = false;
                i += 1;
            }
        } else {
            i += 1;
        }
    }
    mask
}

/// Where `keyword name` is declared in the game's files: the name's span.
fn declaration(sources: &[Source], keyword: &str, name: &str) -> Option<(usize, usize, usize)> {
    for (i, s) in sources.iter().enumerate() {
        let mask = code_mask(&s.text);
        let b = s.text.as_bytes();
        let mut from = 0;
        while let Some(at) = s.text[from..].find(keyword).map(|k| k + from) {
            from = at + keyword.len();
            if !mask[at] || (at > 0 && is_word(b[at - 1])) || !b.get(from).is_some_and(u8::is_ascii_whitespace) {
                continue;
            }
            let mut n = from;
            while n < b.len() && b[n].is_ascii_whitespace() {
                n += 1;
            }
            if s.text[n..].starts_with(name) && !b.get(n + name.len()).copied().is_some_and(is_word) {
                return Some((i, n, n + name.len()));
            }
        }
    }
    None
}

/// The line of `section.key` (or of `[section]`) in drift.toml's text.
fn toml_line(text: &str, path: &str) -> Option<usize> {
    let (section, key) = path.split_once('.').unwrap_or((path, ""));
    let mut current = String::new();
    let mut header_line = None;
    for (i, line) in text.lines().enumerate() {
        let t = line.trim();
        if let Some(h) = t.strip_prefix('[').and_then(|r| r.split(']').next()) {
            current = h.trim().trim_matches('[').trim().to_owned();
            if current == section {
                header_line = Some(i + 1);
                if key.is_empty() {
                    return header_line;
                }
            }
            continue;
        }
        if current == section {
            let k = t.split('=').next().unwrap_or("").trim().trim_matches('"');
            if !key.is_empty() && t.contains('=') && k == key {
                return Some(i + 1);
            }
        }
    }
    header_line
}

/// The literal text of a plain string argument: `"assets/a.png"`, with its
/// byte span (quotes included). `None` for anything else, such as a
/// concatenation.
fn string_argument(text: &str, open: usize, index: usize) -> Option<(String, usize, usize)> {
    let b = text.as_bytes();
    let mut i = open + 1;
    let mut depth = 0usize;
    let mut arg = 0usize;
    let mut start = i;
    while i < b.len() {
        match b[i] {
            b'"' => {
                i += 1;
                while i < b.len() && b[i] != b'"' {
                    i += if b[i] == b'\\' { 2 } else { 1 };
                }
            }
            b'(' | b'[' | b'{' => depth += 1,
            b')' if depth == 0 => break,
            b')' | b']' | b'}' => depth -= 1,
            b',' if depth == 0 => {
                if arg == index {
                    break;
                }
                arg += 1;
                start = i + 1;
            }
            _ => {}
        }
        i += 1;
    }
    if arg != index {
        return None;
    }
    let raw = &text[start..i.min(text.len())];
    let lead = raw.len() - raw.trim_start().len();
    let t = raw.trim();
    let inner = t.strip_prefix('"')?.strip_suffix('"')?;
    if inner.contains('"') || inner.contains('\\') {
        return None;
    }
    let s = start + lead;
    Some((inner.to_owned(), s, s + t.len()))
}

/// Each call in the game's files to a function or method called `name`
/// whose argument `index` is a plain string literal: the file, the literal
/// and its span.
fn literal_calls<'a>(sources: &'a [Source], name: &str, method: bool, index: usize) -> Vec<(&'a Source, String, usize, usize)> {
    let mut out = Vec::new();
    for s in sources {
        let mask = code_mask(&s.text);
        let b = s.text.as_bytes();
        let mut from = 0;
        while let Some(at) = s.text[from..].find(name).map(|k| k + from) {
            from = at + name.len();
            if !mask[at] || (at > 0 && is_word(b[at - 1])) || b.get(from).copied().is_some_and(is_word) {
                continue;
            }
            // The name is a call: `name(` or `name<T>(`.
            let mut open = from;
            if b.get(open) == Some(&b'<') {
                while open < b.len() && b[open] != b'>' {
                    open += 1;
                }
                open += 1;
            }
            if b.get(open) != Some(&b'(') {
                continue;
            }
            let dotted = at > 0 && b[at - 1] == b'.';
            let declared = s.text[..at].trim_end().ends_with("fn");
            if dotted != method || declared {
                continue;
            }
            if let Some((lit, start, end)) = string_argument(&s.text, open, index) {
                out.push((s, lit, start, end));
            }
        }
    }
    out
}

/// A function of the `drift` module whose argument names something: the
/// name a call writes, whether it is a method, and the argument's index
/// among those the call passes.
type Named = (&'static str, bool, usize);

/// The `drift` module's functions that load a file of the game folder
/// (core/assets.cdl, audio.cdl, data.cdl, rendering.cdl, resources.cdl,
/// scenes.cdl).
const LOADS_FILE: &[Named] = &[
    ("texture", false, 0),
    ("sound", false, 0),
    ("music", false, 0),
    ("read_text", false, 0),
    ("load_data", false, 0),
    ("image", true, 0),
    ("font", true, 0),
    ("shader", true, 0),
    ("aseprite", true, 0),
    ("tilemap", true, 0),
    ("ldtk_level", true, 0),
    ("scene", true, 0),
];

/// The `Input` methods that read an input action by name (core/input.cdl,
/// core/resources.cdl).
const READS_ACTION: &[Named] = &[
    ("action_pressed", true, 0),
    ("action_held", true, 0),
    ("action_released", true, 0),
    ("action_value", true, 0),
    ("axis", true, 0),
    ("axis2d", true, 0),
    ("bindings", true, 0),
    ("rebind", true, 0),
];

/// The `Input` methods that declare an input action.
const DECLARES_ACTION: &[Named] = &[
    ("bind", true, 0),
    ("bind_axis", true, 0),
    ("bind_key", true, 0),
    ("bind_mouse", true, 0),
    ("bind_button", true, 0),
    ("bind_axis2d", true, 0),
    ("bind_stick", true, 0),
];

/// Whether the asset `path` names a file in the game folder: the file
/// itself, or one named the same with an extension added, the way a shader
/// is named by what its files share.
fn asset_exists(game: &Path, path: &str) -> bool {
    let full = game.join(path);
    if full.is_file() {
        return true;
    }
    let (Some(dir), Some(stem)) = (full.parent(), full.file_name().map(|n| n.to_string_lossy().into_owned())) else {
        return false;
    };
    std::fs::read_dir(dir).is_ok_and(|entries| {
        entries.flatten().any(|e| e.file_name().to_string_lossy().starts_with(&format!("{stem}.")))
    })
}

fn code_of(message: &str) -> String {
    message.split(':').next().filter(|c| c.starts_with("drift_")).unwrap_or("drift_check").to_owned()
}

/// Checks the game whose entry is `entry`: its diagnostics as JSON objects.
/// `toml` is the drift.toml the engine read, or empty when there is none.
pub(crate) fn check(entry: &str, module_dir: &str, toml: &str) -> Vec<Json> {
    let entry_path = std::fs::canonicalize(entry).unwrap_or_else(|_| PathBuf::from(entry));
    let game = entry_path.parent().map(Path::to_path_buf).unwrap_or_default();
    let sources = game_sources(&game);
    let toml_path = if toml.is_empty() { None } else { Some(std::fs::canonicalize(toml).unwrap_or_else(|_| PathBuf::from(toml))) };
    let toml_text = toml_path.as_ref().and_then(|p| std::fs::read_to_string(p).ok()).unwrap_or_default();
    let mut out: Vec<Placed> = Vec::new();

    let entry_top = |severity: &'static str, code: String, message: String| Placed {
        file: entry_path.clone(),
        line: 1,
        column: 1,
        end_line: 1,
        end_column: 1,
        severity,
        code,
        message,
    };
    let reading = match read_game(entry, module_dir) {
        Ok(r) => r,
        Err(Unread::NoEntry(why)) => {
            return vec![entry_top("error", "drift_no_entry".into(), why).json()];
        }
        Err(Unread::Compile(d, _)) => {
            out.push(compile_error(&d, &entry_path, entry, &sources));
            return out.iter().map(Placed::json).collect();
        }
    };

    let mut place_finding = |f: &Finding, severity: &'static str| {
        let code = code_of(&f.message);
        let at = match &f.item {
            Item::Fn(name) => declaration(&sources, "fn", name).map(|(i, s, e)| (i, s, e)),
            Item::Struct(name) => declaration(&sources, "struct", bare(name)),
            Item::Setting(path) => {
                if let (Some(file), Some(line)) = (&toml_path, toml_line(&toml_text, path)) {
                    out.push(Placed { file: file.clone(), line, column: 0, end_line: line, end_column: 0, severity, code, message: f.message.clone() });
                    return;
                }
                None
            }
        };
        match at {
            Some((i, s, e)) => {
                let src = &sources[i];
                let ((line, column), (end_line, end_column)) = (src.place(s), src.place(e));
                out.push(Placed { file: src.path.clone(), line, column, end_line, end_column, severity, code, message: f.message.clone() });
            }
            None => out.push(entry_top(severity, code, f.message.clone())),
        }
    };
    for f in &reading.errors {
        place_finding(f, "error");
    }
    for f in &reading.warnings {
        place_finding(f, "warning");
    }

    // Asset paths that name no file.
    for &(name, method, index) in LOADS_FILE {
        for (src, lit, s, e) in literal_calls(&sources, name, method, index) {
            if lit.is_empty() || lit.contains("://") || asset_exists(&game, &lit) {
                continue;
            }
            let ((line, column), (end_line, end_column)) = (src.place(s), src.place(e));
            out.push(Placed {
                file: src.path.clone(),
                line,
                column,
                end_line,
                end_column,
                severity: "error",
                code: "drift_missing_asset".into(),
                message: format!("drift_missing_asset: no file {lit} in the game folder {}; {name} reads its path from there", game.display()),
            });
        }
    }

    // Input actions that are neither in drift.toml [input] nor bound in code.
    let mut actions: HashSet<String> = match call("settings_section", json!(["input"])) {
        Json::Object(m) => m.keys().cloned().collect(),
        _ => HashSet::new(),
    };
    for &(name, method, index) in DECLARES_ACTION {
        for (_, lit, _, _) in literal_calls(&sources, name, method, index) {
            actions.insert(lit);
        }
    }
    let mut known: Vec<&String> = actions.iter().collect();
    known.sort();
    let known = if known.is_empty() {
        String::from("none")
    } else {
        known.iter().map(|a| format!("\"{a}\"")).collect::<Vec<_>>().join(", ")
    };
    for &(name, method, index) in READS_ACTION {
        for (src, lit, s, e) in literal_calls(&sources, name, method, index) {
            if actions.contains(&lit) {
                continue;
            }
            let ((line, column), (end_line, end_column)) = (src.place(s), src.place(e));
            out.push(Placed {
                file: src.path.clone(),
                line,
                column,
                end_line,
                end_column,
                severity: "error",
                code: "drift_unknown_action".into(),
                message: format!(
                    "drift_unknown_action: no input action \"{lit}\"; the actions are {known}. Declare actions in drift.toml [input] or with the Input.bind methods"
                ),
            });
        }
    }
    out.iter().map(Placed::json).collect()
}

/// A compile error, placed in the file it names.
fn compile_error(d: &Diagnostic, entry_path: &Path, entry: &str, sources: &[Source]) -> Placed {
    let file = if d.filename == entry || d.filename.is_empty() {
        entry_path.to_path_buf()
    } else {
        std::fs::canonicalize(&d.filename).unwrap_or_else(|_| PathBuf::from(&d.filename))
    };
    let text = sources
        .iter()
        .find(|s| s.path == file)
        .map(|s| s.text.clone())
        .or_else(|| std::fs::read_to_string(&file).ok())
        .unwrap_or_default();
    let src = Source { path: file, text };
    let ((line, column), (end_line, end_column)) = (src.place(d.span.start), src.place(d.span.end));
    Placed {
        file: src.path,
        line,
        column,
        end_line,
        end_column,
        severity: "error",
        code: d.code.clone(),
        message: d.message.clone(),
    }
}
