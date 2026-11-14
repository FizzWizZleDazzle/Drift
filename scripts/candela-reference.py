#!/usr/bin/env python3
"""Writes the Candela book's reference pages from the `drift` module's source.

    scripts/candela-reference.py           write the pages
    scripts/candela-reference.py --check   exit 1 if a page differs from
                                           what the module gives (CI)

The module, bindings/candela/drift/src/main.cdl, the core/ files it passes
on with `export import`, and the opt-in modules beside it, is the only
source. Every signature, field, default and enum variant comes from the
declarations, and every description and example from the `///` doc comment
above them, the same text the editor shows on hover:

    /// One-sentence summary.
    ///
    /// Detail, then an example in a fenced code block.
    struct Name {
        /// What the field means.
        field: int = 0,
    }

`//!` lines say which page the declarations go on:

    //! page: components        the declarations after this line go on
                                reference/components.md, until the next
                                `//! page:` line
    //! # Engine Components     right after a page line: the page's title,
    //!                         once per page, and the `//!` lines after it
    //! Introduction...         are the page's introduction

A doc comment that starts with `Internal:` keeps a helper the module needs
off the pages. Every other type, function, constant, attribute and method
needs a doc comment; one without is an error, so new API is documented in
the change that adds it.

The pages go to bindings/candela/docs/src/reference/, plus index.md, an
alphabetical list of every name, and api-index.json, which maps every name
to its entry for the book's shift-click links
(bindings/candela/docs/theme/api-links.js) and for the editor's docs links
(the `docs` URL in bindings/candela/drift/candela.toml).
"""

import argparse
import json
import sys
sys.dont_write_bytecode = True
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "bindings" / "candela" / "drift" / "src"
MODULE = SRC / "main.cdl"
# The files `drift` is written as: main.cdl passes each one on with
# `export import "drift/core/<name>" { * };`, in this order.
CORE = [SRC / f"{m.group(1)}.cdl"
        for m in re.finditer(r'^export import "drift/(core/\w+)"', MODULE.read_text(), re.M)]
# Each file of the module: `drift` itself, then the opt-in modules such as
# `drift/ui`.
MODULES = [MODULE] + CORE + [SRC / f"{name}.cdl" for name in
                             ("ui", "gameplay", "control", "screens", "ai", "rpg", "debug")]
OUT = ROOT / "bindings" / "candela" / "docs" / "src" / "reference"

GENERATED = ("<!-- Written by scripts/candela-reference.py from the doc comments in\n"
             "     bindings/candela/drift/src. Edit those, not this page. -->\n")

INTERNAL = "Internal:"


# ---------------------------------------------------------------------------
# Reading the module
# ---------------------------------------------------------------------------

class Decl:
    def __init__(self, kind, name, doc, line):
        self.kind = kind          # struct, enum, fn, const, attribute, method, system
        self.name = name          # Type.method for methods
        self.doc = doc            # the /// doc comment, as Markdown
        self.line = line
        self.signature = ""
        self.fields = []          # (name, type, default, doc)
        self.variants = []        # (text, doc)
        self.generics = ""
        self.owner = ""           # the type of a method
        self.attrs = []           # attributes written on it, such as @component
        self.page = ""            # the page it goes on
        self.module = ""          # the import path of its file: drift, drift/ui, ...
        self.path = None

    @property
    def internal(self):
        return self.doc.startswith(INTERNAL)


class Page:
    def __init__(self, slug):
        self.slug = slug
        self.title = ""
        self.intro = ""
        self.where = ""           # file:line of the title, for errors
        self.entries = []         # Decls, in module order


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
    raise SystemExit(f"{READING}:{start + 1}: unbalanced braces")


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


DOC_RE = re.compile(r"^\s*///(?!/)(.*)$")


def doc_text(line):
    """The text of a `///` line, or None for any other line."""
    m = DOC_RE.match(line)
    if not m:
        return None
    t = m.group(1)
    return t[1:] if t.startswith(" ") else t


def doc_above(lines, i):
    """The `///` doc comment that ends on the line above lines[i]."""
    out = []
    j = i - 1
    while j >= 0:
        t = doc_text(lines[j])
        if t is None:
            break
        out.insert(0, t.rstrip())
        j -= 1
    return "\n".join(out).strip()


def md_text(text):
    """Doc text as Markdown for a page: `<` outside code would start an HTML
    tag."""
    out, in_code, in_span = [], False, False
    for line in text.split("\n"):
        if line.lstrip().startswith("```"):
            in_code = not in_code
        elif not in_code:
            if not line.strip():
                in_span = False       # a code span ends with its paragraph
            chars = []
            for c in line:
                if c == "`":
                    in_span = not in_span
                chars.append("&lt;" if c == "<" and not in_span else c)
            line = "".join(chars)
        out.append(line)
    return "\n".join(out)


def one_line(text):
    """Doc text for a table cell."""
    return " ".join(md_text(text).split())


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
    line without any attribute in front. Returns the Decl and its last line.
    A `///` comment inside the braces documents the member that starts the
    next line."""
    end = block_end(lines, i, lines[i].index("{"))
    d = Decl(kind, name, "", i + 1)
    d.generics = generics
    if end == i:
        body = [code[code.index("{") + 1:code.rindex("}")]]
    else:
        last = lines[end]
        body = [code[code.index("{") + 1:]] + lines[i + 1:end] + [last[:last.rindex("}")]]
    pending = []
    for n, bl in enumerate(body):
        t = doc_text(bl)
        if t is not None:
            pending.append(t.rstrip())
            continue
        text, _ = strip_comment(bl)
        parts = split_top(text)
        if not parts:
            if pending and text.strip() == "" and bl.strip():
                pending = []          # a plain comment line ends a doc comment
            continue
        doc = "\n".join(pending).strip()
        pending = []
        for k, part in enumerate(parts):
            member_doc = doc if k == 0 else ""
            if kind == "struct":
                fm = re.match(r"^([A-Za-z_]\w*)\s*:\s*(.+?)(\s*=\s*(.+))?$", part, re.S)
                if not fm:
                    raise SystemExit(f"{path}:{i + 1}: cannot read field `{part}` of {name}")
                d.fields.append((fm.group(1), " ".join(fm.group(2).split()),
                                 " ".join((fm.group(4) or "").split()), member_doc))
            else:
                d.variants.append((" ".join(part.split()), member_doc))
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


def module_name(path):
    """The path a game imports a module file by."""
    return "drift" if path == MODULE or path in CORE else "drift/" + path.stem


READING = MODULE                  # the file being read, for error messages
PAGE_RE = re.compile(r"^//! page: ([\w-]+)\s*$")


def read_pages(path, lines, pages):
    """The `//! page:` lines of a file: the page each line index is on, and
    the titles and introductions the file gives."""
    on = [""] * len(lines)
    current = ""
    i = 0
    while i < len(lines):
        m = PAGE_RE.match(lines[i])
        if m:
            current = m.group(1)
            page = pages.setdefault(current, Page(current))
            j = i + 1
            if j < len(lines) and lines[j].startswith("//! # "):
                if page.title:
                    raise SystemExit(f"{path}:{j + 1}: page {current} already has a title, at {page.where}")
                page.title = lines[j][len("//! # "):].strip()
                page.where = f"{path.relative_to(ROOT)}:{j + 1}"
                intro = []
                j += 1
                while j < len(lines) and lines[j].startswith("//!") and not PAGE_RE.match(lines[j]):
                    t = lines[j][3:]
                    intro.append(t[1:] if t.startswith(" ") else t)
                    j += 1
                page.intro = "\n".join(intro).strip()
            for k in range(i, j):
                on[k] = current
            i = j
            continue
        on[i] = current
        i += 1
    return on


def read_module(path, pages):
    global READING
    READING = path
    lines = path.read_text().split("\n")
    on_page = read_pages(path, lines, pages)
    decls = []
    owner, owner_end = None, -1
    i = 0
    while i < len(lines):
        line = lines[i]
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
        page = on_page[i]

        if top and code.startswith("host "):
            start = len(decls)
            i = read_host(lines, i, path, decls)
            for d in decls[start:]:
                d.page = page
            continue
        m = re.match(r"^impl\s+([A-Za-z_]\w*)(<[^>]*>)?\s*\{", code)
        if top and m:
            owner, owner_end = m.group(1), block_end(lines, i, line.index("{"))
            i += 1
            continue
        m = re.match(r"^(struct|enum)\s+([A-Za-z_]\w*)\s*(<[^>]*>)?\s*\{", code)
        if top and m:
            d, end = read_type(lines, i, code, path, m.group(1), m.group(2), m.group(3) or "")
            d.doc, d.attrs, d.page = doc, attrs, page
            decls.append(d)
            i = end + 1
            continue
        m = re.match(r"^const\s+([A-Za-z_]\w*)\s*:\s*([^=]+?)\s*=\s*(.+?);", code)
        if top and m:
            d = Decl("const", m.group(1), doc, i + 1)
            d.signature = f"const {m.group(1)}: {m.group(2)} = {m.group(3)}"
            d.page = page
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
            d.attrs, d.page = attrs, page
            decls.append(d)
            i = fn_end(lines, k, text)
            continue
        i += 1
    for d in decls:
        d.module = module_name(path)
        d.path = path
    return decls


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------

def anchor(name):
    """The id mdBook gives a heading: lower case, punctuation dropped."""
    return re.sub(r"[^a-z0-9_ -]", "", name.lower()).replace(" ", "-")


def heading_ids(text):
    """The (text, id) of each heading of a page, in order. mdBook numbers a
    repeated id, so `Fsm` is #fsm and a later `fsm` is #fsm-1."""
    out, seen, in_code = [], {}, False
    for line in text.split("\n"):
        if line.startswith("```"):
            in_code = not in_code
        m = None if in_code else re.match(r"^#{1,6} (.+?)\s*$", line)
        if not m:
            continue
        title = m.group(1)
        plain = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", re.sub(r"<[^>]+>", "", title)).replace("`", "")
        base = anchor(plain)
        n = seen.get(base, 0)
        seen[base] = n + 1
        out.append((title, base if n == 0 else f"{base}-{n}"))
    return out


def short(text, limit=160):
    return text if len(text) <= limit else text[:limit - 4].rstrip(" ,") + " ..."


def variant_names(d):
    return [v for v, _ in d.variants]


def signature_of(d):
    """The one-line signature the shift-click tooltip shows."""
    if d.kind == "struct":
        return " ".join(d.attrs + [f"struct {d.name}{d.generics}"])
    if d.kind == "enum":
        return short(f"enum {d.name} {{ {', '.join(variant_names(d))} }}")
    if d.kind == "attribute":
        return f"{d.signature} on {'an' if d.target[0] in 'aeiou' else 'a'} {d.target}"
    return " ".join(d.attrs + [d.signature])


def api_entry(d, slug, anc, kind=None):
    entry = {"module": d.module, "kind": kind or d.kind, "url": f"reference/{slug}.html#{anc}",
             "sig": signature_of(d)}
    if d.kind == "struct" and kind is None and d.fields:
        # Field types let the links follow `t.position.normalized()`.
        entry["fields"] = {name: typ for name, typ, _, _ in d.fields}
    return entry


def cell(text):
    return text.replace("|", "\\|")


def split_example(text):
    """The prose of a description, and the code blocks after it."""
    i = text.find("```")
    if i < 0:
        return text.strip(), ""
    return text[:i].strip(), text[i:].strip()


def write_struct(d, methods):
    out = [f"## {d.name}", ""]
    prose, example = split_example(md_text(d.doc))
    if prose:
        out += [prose, ""]
    lines = list(d.attrs) + [f"struct {d.name}{d.generics} {{"]
    for name, typ, default, _ in d.fields:
        lines.append(f"    {name}: {typ}" + (f" = {default}" if default else "") + ",")
    lines.append("}")
    if not d.fields:
        lines = list(d.attrs) + [f"struct {d.name}{d.generics} {{}}"]
    out += ["```rust", *lines, "```", ""]
    if any(doc for _, _, _, doc in d.fields):
        out += ["| Field | Type | Default | Meaning |", "|-------|------|---------|---------|"]
        for name, typ, default, doc in d.fields:
            out.append(f"| `{name}` | `{cell(typ)}` | {('`' + cell(default) + '`') if default else 'required'} "
                       f"| {cell(one_line(doc))} |")
        out.append("")
    if example:
        out += [example, ""]
    for m in methods:
        out += write_method(m)
    return out


def write_method(m):
    out = [f"### {m.name}", "", "```rust", m.signature, "```", ""]
    if m.doc:
        out += [md_text(m.doc), ""]
    return out


def write_enum(d):
    out = [f"## {d.name}", ""]
    out += [md_text(d.doc), ""]
    names = variant_names(d)
    body = ", ".join(names)
    if len(body) > 70:
        out += ["```rust", f"enum {d.name} {{"]
        row = "   "
        for v in names:
            if len(row) + len(v) + 2 > 78:
                out.append(row.rstrip())
                row = "   "
            row += " " + v + ","
        out += [row.rstrip(), "}", "```", ""]
    else:
        out += ["```rust", f"enum {d.name} {{ {body} }}", "```", ""]
    if any(doc for _, doc in d.variants):
        out += ["| Variant | Meaning |", "|---------|---------|"]
        for v, doc in d.variants:
            out.append(f"| `{v}` | {cell(one_line(doc))} |")
        out.append("")
    return out


def write_simple(d):
    out = [f"## {d.name}", "", "```rust", *d.attrs, d.signature, "```", ""]
    if d.kind == "system":
        out += ["A system the module adds: it runs in every game that imports the module.", ""]
    if d.doc:
        out += [md_text(d.doc), ""]
    return out


def write_attribute(d):
    out = [f"## {d.name}", "", "```rust", d.signature, "```", ""]
    out += [f"On a `{d.target}`.", ""]
    if d.doc:
        out += [md_text(d.doc), ""]
    return out


def where(d):
    return f"{d.path.relative_to(ROOT)}:{d.line}"


def generate():
    pages = {}
    decls = []
    for path in MODULES:
        decls += read_module(path, pages)

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

    errors = []
    for d in decls:
        if d.kind == "method" and not re.match(r"^\w", d.name.split(".", 1)[1]):
            continue
        if d.kind != "method" and by_name.get(d.name) is not d:
            continue
        if not d.doc:
            errors.append(f"{where(d)}: {d.name} has no /// doc comment")
        elif d.internal:
            continue
        if d.kind == "method":
            continue
        if not d.page:
            errors.append(f"{where(d)}: {d.name} comes before any `//! page:` line")
        elif d.page not in pages or not pages[d.page].title:
            errors.append(f"{where(d)}: page {d.page} has no title; give one `//! page: {d.page}` "
                          "line a `//! # Title` line after it")
        else:
            pages[d.page].entries.append(d)
    if errors:
        raise SystemExit("\n".join(errors))

    files = {}
    index = []                    # (name, page slug, anchor)
    api = {}                      # name -> entries, one per module that has it
    for p in pages.values():
        out = [GENERATED, f"# {p.title}", ""]
        if p.intro:
            out += [p.intro, ""]
        placed_here = []
        for d in p.entries:
            ms = [m for m in methods.get(d.name, []) if not m.internal]
            if d.kind == "struct":
                out += write_struct(d, ms)
            elif d.kind == "enum":
                out += write_enum(d)
            elif d.kind == "attribute":
                out += write_attribute(d)
            else:
                out += write_simple(d)
            placed_here += [d] + ms
        text = re.sub(r"\n{3,}", "\n\n", "\n".join(out).rstrip() + "\n")
        files[p.slug + ".md"] = text
        # Each entry's heading comes after the one before it; a doc
        # comment's own headings sit in between.
        ids = iter(heading_ids(text))
        for d in placed_here:
            anc = next(i for title, i in ids if title == d.name)
            index.append((d.name, p.slug, anc, d.kind))
            api.setdefault(d.name, []).append(api_entry(d, p.slug, anc))
            if d.kind == "enum":
                for v in variant_names(d):
                    vname = d.name + "::" + re.match(r"\w+", v).group(0)
                    api.setdefault(vname, []).append(api_entry(d, p.slug, anc, "variant"))

    kinds = {"struct": "type", "enum": "enum", "fn": "function", "const": "constant",
             "attribute": "attribute", "method": "method", "system": "system"}
    rows = sorted(index, key=lambda r: (r[0].lstrip("@").lower(), r[0]))
    out = [GENERATED, "# API Index", "",
           "Every name the `drift` module gives a game, in alphabetical order. Search the book for a name to "
           "land on its entry, or follow the link. Anywhere in the book, Shift-click a name in code to open its entry "
           "(Ctrl+Shift-click, or Cmd+Shift-click on a Mac, for a new tab), and hover it to see its signature.", ""]
    letter = None
    for name, slug, anc, kind in rows:
        first = name.lstrip("@")[0].upper()
        if first != letter:
            letter = first
            out += ["", f"## {letter}", ""]
        out.append(f"- [`{name}`]({slug}.md#{anc}) {kinds[kind]}")
    files["index.md"] = "\n".join(out).rstrip() + "\n"
    # One name per line, so a change to the module is a readable diff.
    body = ",\n".join(f"{json.dumps(k)}: {json.dumps(v, sort_keys=True)}" for k, v in sorted(api.items()))
    files["api-index.json"] = "{\n" + body + "\n}\n"
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
