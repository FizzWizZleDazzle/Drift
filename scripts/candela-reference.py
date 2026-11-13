#!/usr/bin/env python3
"""Writes the Candela book's reference pages from the `drift` module.

    scripts/candela-reference.py           write the pages
    scripts/candela-reference.py --check   exit 1 if a page differs from
                                           what the module gives (CI)

The module, bindings/candela/drift/src/main.cdl, is the source of every
signature, field, default and enum variant. Descriptions come from the
comments above each declaration in the module, and from
bindings/candela/docs/api-notes.md, which also holds the examples and says
which page each type and function belongs on. A note for a name the module
no longer has is an error, so the notes cannot describe API that is gone.

The pages go to bindings/candela/docs/src/reference/, one per section of
the notes, plus index.md, an alphabetical list of every name.
"""

import argparse
import sys
sys.dont_write_bytecode = True
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "bindings" / "candela" / "drift" / "src"
MODULE = SRC / "main.cdl"
# Each file of the module, and the page its names go on when the notes do
# not place them: `drift` itself, and `drift/ui`.
MODULES = [(SRC / "main.cdl", "values"), (SRC / "ui.cdl", "ui"), (SRC / "gameplay.cdl", "gameplay"),
           (SRC / "control.cdl", "control"), (SRC / "screens.cdl", "screens"), (SRC / "ai.cdl", "ai"),
           (SRC / "rpg.cdl", "rpg"), (SRC / "debug.cdl", "debug")]
NOTES = ROOT / "bindings" / "candela" / "docs" / "api-notes.md"
OUT = ROOT / "bindings" / "candela" / "docs" / "src" / "reference"

GENERATED = ("<!-- Written by scripts/candela-reference.py from the drift module and\n"
             "     bindings/candela/docs/api-notes.md. Edit those, not this page. -->\n")


# ---------------------------------------------------------------------------
# Reading the module
# ---------------------------------------------------------------------------

class Decl:
    def __init__(self, kind, name, doc, line):
        self.kind = kind          # struct, enum, fn, const, attribute, method
        self.name = name          # Type.method for methods
        self.doc = doc            # the comment lines above it, joined
        self.line = line
        self.signature = ""
        self.fields = []          # (name, type, default, comment)
        self.variants = []
        self.generics = ""
        self.owner = ""           # the type of a method
        self.attrs = []           # attributes written on it, such as @component
        self.page = ""            # the page it goes on without a note
        self.section = ""         # the `// ---- Section ----` it sits under


def strip_comment(line):
    """The line without a trailing // comment, and the comment."""
    in_str = False
    i = 0
    while i < len(line):
        c = line[i]
        if c == '"' and (i == 0 or line[i - 1] != "\\"):
            in_str = not in_str
        elif not in_str and line.startswith("//", i):
            return line[:i], line[i + 2:].strip()
        i += 1
    return line, ""


def block_end(lines, start, col):
    """The line index where the brace opened at lines[start][col] closes."""
    depth = 0
    for i in range(start, len(lines)):
        code, _ = strip_comment(lines[i])
        in_str = False
        for j, c in enumerate(code):
            if i == start and j < col:
                continue
            if c == '"' and (j == 0 or code[j - 1] != "\\"):
                in_str = not in_str
            if in_str:
                continue
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return i
    raise SystemExit(f"{MODULE}:{start + 1}: unbalanced braces")


def split_top(text, sep=","):
    """Splits on `sep` outside brackets, braces, parentheses and strings."""
    parts, depth, cur, in_str = [], 0, "", False
    for i, c in enumerate(text):
        if c == '"' and (i == 0 or text[i - 1] != "\\"):
            in_str = not in_str
        if not in_str:
            if c in "([{<":
                depth += 1
            elif c in ")]}>" and not (c == ">" and i > 0 and text[i - 1] == "-"):
                depth -= 1
            elif c == sep and depth == 0:
                parts.append(cur)
                cur = ""
                continue
        cur += c
    parts.append(cur)
    return [p.strip() for p in parts if p.strip()]


def md_text(text):
    """Module comment text as Markdown: `<` outside code spans would start
    an HTML tag."""
    parts = text.split("`")
    for k in range(0, len(parts), 2):
        parts[k] = parts[k].replace("<", "&lt;")
    return "`".join(parts)


def doc_above(lines, i):
    """The comment block that ends on the line above lines[i]."""
    out = []
    j = i - 1
    while j >= 0 and lines[j].strip().startswith("//"):
        text = lines[j].strip()[2:].strip()
        if text.startswith("----") or text.startswith("===="):
            break
        out.insert(0, text)
        j -= 1
    # A blank comment line separates a section's prose from the
    # declaration's own comment; keep only the last paragraph.
    if "" in out:
        out = out[len(out) - out[::-1].index(""):]
    return md_text(" ".join(out).strip())


FN_RE = re.compile(r"^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*|[-+*/<>=!]+)\s*(<[^>(]*>)?\s*\(")


def fn_signature(text):
    """`fn name(params) -> ret` from a declaration's first lines."""
    head = text
    # Cut at the body: the first `{` or `=>` outside parentheses.
    depth = 0
    cut = len(head)
    for i, c in enumerate(head):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and (c == "{" or head.startswith("=>", i)):
            cut = i
            break
    return " ".join(head[:cut].split())


def attrs_above(lines, i):
    """Attribute lines (`@component`, `@system(...)`) right above lines[i],
    and the index of the first of them."""
    out = []
    j = i - 1
    while j >= 0 and lines[j].strip().startswith("@"):
        out.insert(0, lines[j].strip())
        j -= 1
    return out, j + 1


ATTR_PREFIX = re.compile(r"^((?:@\w+(?:\([^)]*\))?\s+)+)")


def read_host(lines, i, path, decls):
    """The attributes a `host` block declares; returns the line after it."""
    end = block_end(lines, i, lines[i].index("{"))
    j = i + 1
    while j < end:
        s = lines[j].strip()
        if s.startswith("attribute "):
            first = j
            text = s
            while not text.rstrip().endswith(";"):
                j += 1
                text += " " + lines[j].strip()
            m = re.match(r"attribute\s+(\w+)\s*(\((.*)\))?\s*on\s+(\w+);", " ".join(text.split()))
            if not m:
                raise SystemExit(f"{path}:{j + 1}: cannot read attribute: {text}")
            d = Decl("attribute", "@" + m.group(1), doc_above(lines, first), first + 1)
            d.signature = "@" + m.group(1) + (f"({m.group(3)})" if m.group(3) else "")
            d.target = m.group(4)
            decls.append(d)
        j += 1
    return end + 1


def read_type(lines, i, code, path, kind, name, generics):
    """A struct or enum whose declaration starts at lines[i]; `code` is that
    line without any attribute in front. Returns the Decl and its last line."""
    end = block_end(lines, i, lines[i].index("{"))
    d = Decl(kind, name, "", i + 1)
    d.generics = generics
    if end == i:
        body = [code[code.index("{") + 1:code.rindex("}")]]
    else:
        last = lines[end]
        body = [code[code.index("{") + 1:]] + lines[i + 1:end] + [last[:last.rindex("}")]]
    if kind == "struct":
        for bl in body:
            text, comment = strip_comment(bl)
            for part in split_top(text):
                fm = re.match(r"^([A-Za-z_]\w*)\s*:\s*(.+?)(\s*=\s*(.+))?$", part, re.S)
                if not fm:
                    raise SystemExit(f"{path}:{i + 1}: cannot read field `{part}` of {name}")
                d.fields.append((fm.group(1), " ".join(fm.group(2).split()),
                                 " ".join((fm.group(4) or "").split()), comment))
    else:
        d.variants = split_top(" ".join(strip_comment(bl)[0] for bl in body))
    return d, end


def fn_end(lines, k, text):
    """The line after a function whose header ends on lines[k]."""
    head = strip_comment(lines[k])[0]
    arrow = text.find("=>")
    brace = strip_comment(text)[0].find("{", text.find(")"))
    if arrow >= 0 and (brace < 0 or arrow < brace):
        while not strip_comment(lines[k])[0].rstrip().endswith(";"):
            k += 1
        return k + 1
    return block_end(lines, k, head.index("{", head.find(")") if ")" in head else 0)) + 1


def read_module(path, page):
    lines = path.read_text().split("\n")
    decls = []
    owner, owner_end = None, -1
    section = ""
    i = 0
    while i < len(lines):
        line = lines[i]
        sm = re.match(r"^// (?:----|====) (.+?) (?:----|====)\s*$", line)
        if sm:
            section = sm.group(1)
        if owner is not None and i > owner_end:
            owner = None
        top = not line.startswith((" ", "\t"))
        code = line.strip()
        inline = []
        pm = ATTR_PREFIX.match(code)
        if top and pm:
            inline = pm.group(1).split()
            code = code[pm.end():]
        above, first = attrs_above(lines, i)
        attrs = above + inline
        doc = doc_above(lines, first)

        if top and code.startswith("host "):
            i = read_host(lines, i, path, decls)
            continue
        m = re.match(r"^impl\s+([A-Za-z_]\w*)(<[^>]*>)?\s*\{", code)
        if top and m:
            owner, owner_end = m.group(1), block_end(lines, i, line.index("{"))
            i += 1
            continue
        m = re.match(r"^(struct|enum)\s+([A-Za-z_]\w*)\s*(<[^>]*>)?\s*\{", code)
        if top and m:
            d, end = read_type(lines, i, code, path, m.group(1), m.group(2), m.group(3) or "")
            d.doc, d.attrs, d.page, d.section = doc, attrs, page, section
            decls.append(d)
            i = end + 1
            continue
        m = re.match(r"^const\s+([A-Za-z_]\w*)\s*:\s*([^=]+?)\s*=\s*(.+?);", code)
        if top and m:
            d = Decl("const", m.group(1), doc, i + 1)
            d.signature = f"const {m.group(1)}: {m.group(2)} = {m.group(3)}"
            d.page, d.section = page, section
            decls.append(d)
            i += 1
            continue
        m = FN_RE.match(code)
        if m and (top or owner is not None):
            text, k = code, i
            while "{" not in strip_comment(text)[0] and "=>" not in text:
                k += 1
                text += " " + lines[k].strip()
            if owner is not None and not top:
                d = Decl("method", f"{owner}.{m.group(1)}", doc, i + 1)
                d.owner = owner
            else:
                d = Decl("system" if any(a.startswith("@system") for a in attrs) else "fn", m.group(1), doc, i + 1)
            d.signature = fn_signature(strip_comment(text)[0])
            d.attrs, d.page, d.section = attrs, page, section
            decls.append(d)
            i = fn_end(lines, k, text)
            continue
        i += 1
    return decls


# ---------------------------------------------------------------------------
# Reading the notes
# ---------------------------------------------------------------------------

class Page:
    def __init__(self, slug, title):
        self.slug = slug
        self.title = title
        self.intro = []
        self.entries = []         # names, in order


def read_notes(path):
    """Pages, notes per name, and the names kept off the pages."""
    pages, notes, internal = [], {}, set()
    page = None
    current = None
    in_code = False
    for n, line in enumerate(path.read_text().split("\n"), 1):
        if line.startswith("```"):
            in_code = not in_code
        if not in_code:
            m = re.match(r"^# (.+?)\s*\{#([\w-]+)\}\s*$", line)
            if m:
                page = Page(m.group(2), m.group(1))
                pages.append(page)
                current = None
                continue
            m = re.match(r"^## (\S+)\s*$", line)
            if m:
                current = m.group(1)
                if current in notes:
                    raise SystemExit(f"{path}:{n}: {current} has two notes")
                notes[current] = []
                if page is None:
                    raise SystemExit(f"{path}:{n}: {current} comes before any page")
                if page.slug == "internal":
                    internal.add(current)
                elif "." not in current:
                    page.entries.append(current)
                continue
        if current is not None:
            notes[current].append(line)
        elif page is not None:
            page.intro.append(line)
    for k in notes:
        notes[k] = "\n".join(notes[k]).strip()
    for p in pages:
        p.intro = "\n".join(p.intro).strip()
    sections = {}
    for p in pages:
        if p.slug == "sections":
            for line in p.intro.split("\n"):
                m = re.match(r"^- (.+?): ([\w-]+)\s*$", line)
                if m:
                    sections[m.group(1)] = m.group(2)
    return [p for p in pages if p.slug not in ("internal", "sections")], notes, internal, sections


def field_notes(text):
    """`- name: text` lines of a note, and the note without them."""
    found = {}
    rest = []
    for line in text.split("\n"):
        m = re.match(r"^- `?([A-Za-z_]\w*)`?: (.+)$", line)
        if m:
            found[m.group(1)] = m.group(2)
        else:
            rest.append(line)
    return found, "\n".join(rest).strip()


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------

def anchor(name):
    """The id mdBook gives a heading: lower case, punctuation dropped."""
    return re.sub(r"[^a-z0-9_ -]", "", name.lower()).replace(" ", "-")


def cell(text):
    return text.replace("|", "\\|")


def describe(d, notes):
    """A declaration's description and example. The note's prose wins over
    the module's comment; a note that is only an example keeps the comment
    as its description."""
    note = notes.get(d.name, "")
    if d.kind == "struct" or d.kind == "enum":
        _, note = field_notes(note)
    prose = []
    in_code = False
    for line in note.split("\n"):
        if line.startswith("```"):
            in_code = not in_code
            if in_code:
                break
        prose.append(line)
    if "".join(prose).strip():
        return note
    return (d.doc + "\n\n" + note).strip()


def split_example(text):
    """The prose of a description, and the code blocks after it."""
    i = text.find("```")
    if i < 0:
        return text.strip(), ""
    return text[:i].strip(), text[i:].strip()


def write_struct(d, notes, methods):
    out = [f"## {d.name}", ""]
    fnotes, _ = field_notes(notes.get(d.name, ""))
    prose, example = split_example(describe(d, notes))
    if prose:
        out += [prose, ""]
    lines = list(d.attrs) + [f"struct {d.name}{d.generics} {{"]
    for name, typ, default, _ in d.fields:
        lines.append(f"    {name}: {typ}" + (f" = {default}" if default else "") + ",")
    lines.append("}")
    if not d.fields:
        lines = list(d.attrs) + [f"struct {d.name}{d.generics} {{}}"]
    out += ["```rust", *lines, "```", ""]
    shown = [f for f in d.fields if fnotes.get(f[0]) or f[3]]
    if d.fields and shown:
        out += ["| Field | Type | Default | Meaning |", "|-------|------|---------|---------|"]
        for name, typ, default, comment in d.fields:
            meaning = fnotes.get(name) or md_text(comment)
            out.append(f"| `{name}` | `{cell(typ)}` | {('`' + cell(default) + '`') if default else 'required'} | {cell(meaning)} |")
        out.append("")
    if example:
        out += [example, ""]
    for m in methods:
        out += write_method(m, notes)
    return out


def write_method(m, notes):
    out = [f"### {m.name}", "", "```rust", m.signature, "```", ""]
    text = describe(m, notes)
    if text:
        out += [text, ""]
    return out


def write_enum(d, notes):
    out = [f"## {d.name}", ""]
    vnotes, _ = field_notes(notes.get(d.name, ""))
    out += [describe(d, notes), ""]
    body = ", ".join(d.variants)
    if len(body) > 70:
        out += ["```rust", f"enum {d.name} {{"]
        row = "   "
        for v in d.variants:
            if len(row) + len(v) + 2 > 78:
                out.append(row.rstrip())
                row = "   "
            row += " " + v + ","
        out += [row.rstrip(), "}", "```", ""]
    else:
        out += ["```rust", f"enum {d.name} {{ {body} }}", "```", ""]
    if vnotes:
        out += ["| Variant | Meaning |", "|---------|---------|"]
        for v in d.variants:
            out.append(f"| `{v}` | {cell(vnotes.get(v, ''))} |")
        out.append("")
    return out


def write_simple(d, notes, heading=None):
    out = [f"## {heading or d.name}", "", "```rust", *d.attrs, d.signature, "```", ""]
    if d.kind == "system":
        out += ["A system the module adds: it runs in every game that imports the module.", ""]
    text = describe(d, notes)
    if text:
        out += [text, ""]
    return out


def write_attribute(d, notes):
    out = [f"## {d.name}", "", "```rust", d.signature, "```", ""]
    out += [f"On a `{d.target}`.", ""]
    text = describe(d, notes)
    if text:
        out += [text, ""]
    return out


def generate():
    decls = []
    for path, page in MODULES:
        decls += read_module(path, page)
    pages, notes, internal, sections = read_notes(NOTES)

    by_name = {}
    for d in decls:
        if d.kind == "method":
            continue
        # A type declared twice (impl blocks only add methods) keeps the first.
        by_name.setdefault(d.name, d)
    # Operators (Vec2 + Vec2 and the like) are described with their type.
    methods = {}
    for d in decls:
        if d.kind == "method" and re.match(r"^\w", d.name.split(".", 1)[1]):
            methods.setdefault(d.owner, []).append(d)
    method_names = {m.name for ms in methods.values() for m in ms}

    errors = []
    for name in notes:
        if name not in by_name and name not in method_names and name not in internal:
            errors.append(f"{NOTES}: a note for {name}, which the drift module does not have")
    for name in internal:
        if name not in by_name and name not in method_names:
            errors.append(f"{NOTES}: {name} is listed as internal but the drift module does not have it")

    placed = set()
    for p in pages:
        for name in p.entries:
            if name not in by_name:
                continue
            placed.add(name)
    # A name the notes do not place goes on its file's page, in source order,
    # so new API is on a page as soon as it is in the module.
    by_slug = {p.slug: p for p in pages}
    for d in decls:
        if d.kind == "method" or d.name in placed or d.name in internal or by_name.get(d.name) is not d:
            continue
        page = sections.get(d.section, d.page) if d.page == "values" else d.page
        if page not in by_slug:
            errors.append(f"{NOTES}: no page with the slug {page} for {d.name}")
            continue
        by_slug[page].entries.append(d.name)
        placed.add(d.name)
    if errors:
        raise SystemExit("\n".join(errors))

    files = {}
    index = []                    # (name, page slug, anchor)
    for p in pages:
        out = [GENERATED, f"# {p.title}", ""]
        if p.intro:
            out += [p.intro, ""]
        for name in p.entries:
            d = by_name.get(name)
            if d is None:
                continue
            ms = [m for m in methods.get(name, []) if m.name not in internal]
            if d.kind == "struct":
                out += write_struct(d, notes, ms)
            elif d.kind == "enum":
                out += write_enum(d, notes)
            elif d.kind == "attribute":
                out += write_attribute(d, notes)
            else:
                out += write_simple(d, notes)
            index.append((d.name, p.slug, anchor(d.name), d.kind))
            for m in ms:
                index.append((m.name, p.slug, anchor(m.name), "method"))
        # The examples of each entry sit in its note; nothing else follows.
        text = "\n".join(out).rstrip() + "\n"
        files[p.slug + ".md"] = re.sub(r"\n{3,}", "\n\n", text)

    kinds = {"struct": "type", "enum": "enum", "fn": "function", "const": "constant",
             "attribute": "attribute", "method": "method", "system": "system"}
    rows = sorted(index, key=lambda r: (r[0].lstrip("@").lower(), r[0]))
    out = [GENERATED, "# API Index", "",
           "Every name the `drift` module gives a game, in alphabetical order. Search the book for a name to "
           "land on its entry, or follow the link.", ""]
    letter = None
    for name, slug, anc, kind in rows:
        first = name.lstrip("@")[0].upper()
        if first != letter:
            letter = first
            out += ["", f"## {letter}", ""]
        out.append(f"- [`{name}`]({slug}.md#{anc}) {kinds[kind]}")
    files["index.md"] = "\n".join(out).rstrip() + "\n"
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail if the pages are not up to date")
    args = ap.parse_args()
    files = generate()
    stale = []
    for name, text in files.items():
        path = OUT / name
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists() or path.read_text() != text:
                path.write_text(text)
                print(f"wrote {path.relative_to(ROOT)}")
    if stale:
        print("The Candela reference does not match the drift module. Run scripts/candela-reference.py "
              "and commit the pages:", file=sys.stderr)
        for s in stale:
            print("  " + s, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
