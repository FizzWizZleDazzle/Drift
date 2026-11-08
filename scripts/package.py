#!/usr/bin/env python3
"""Pack built examples and docs into the archives a release publishes.

Usage:

    scripts/package.py examples <build-dir> <out>   desktop example programs
    scripts/package.py web <build-dir> <out>        browser builds of the examples
    scripts/package.py dir <dir> <out>              any directory, such as the docs

<out> ends in .tar.gz or .zip, and the archive holds one top-level folder named
after it (drift-examples-linux-x86_64.tar.gz unpacks into
drift-examples-linux-x86_64/).

Desktop examples load assets relative to the working directory and shaders from
beside the executable, so each one lands in a folder of its own with its
drift.toml, its assets and its translated shaders: `cd hello_sprites &&
./hello_sprites` runs it. The web archive holds every example's page with its
.js, .wasm and .data files and an index.html that links them.

An example is whatever folder under examples/ has a matching program in the
build directory, so a new example ships without this file changing.
"""

from __future__ import annotations

import html
import os
import shutil
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / "examples"
WEB_SUFFIXES = (".html", ".js", ".wasm", ".data")


def fail(message: str) -> None:
    print(f"package.py: {message}", file=sys.stderr)
    raise SystemExit(1)


def archive_stem(out: Path) -> str:
    name = out.name
    for suffix in (".tar.gz", ".zip"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    fail(f"{out}: the archive name must end in .tar.gz or .zip")
    return ""


def write_archive(src: Path, out: Path) -> None:
    """Archive src's contents under one folder named after out."""
    stem = archive_stem(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in src.rglob("*") if p.is_file())
    if out.name.endswith(".zip"):
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for path in files:
                info = zipfile.ZipInfo.from_file(path, f"{stem}/{path.relative_to(src).as_posix()}")
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, "rb") as f:
                    z.writestr(info, f.read())
    else:
        with tarfile.open(out, "w:gz") as t:
            for path in files:
                t.add(path, f"{stem}/{path.relative_to(src).as_posix()}")
    print(f"{out}: {len(files)} files")


def copy_tree(src: Path, dst: Path) -> None:
    if src.is_dir():
        shutil.copytree(src, dst, dirs_exist_ok=True)


def program(build: Path, name: str) -> Path | None:
    exe = build / f"{name}.exe"
    if exe.is_file():
        return exe
    exe = build / name
    if exe.is_file() and os.access(exe, os.X_OK):
        return exe
    return None


def example_names() -> list[str]:
    return sorted(p.name for p in EXAMPLES.iterdir() if (p / "main.cpp").is_file())


def stage_examples(build: Path, stage: Path) -> None:
    found = 0
    for name in example_names():
        exe = program(build, name)
        if exe is None:
            continue
        folder = stage / name
        folder.mkdir(parents=True)
        shutil.copy2(exe, folder / exe.name)
        source = EXAMPLES / name
        if (source / "drift.toml").is_file():
            shutil.copy2(source / "drift.toml", folder / "drift.toml")
        copy_tree(source / "assets", folder / "assets")
        copy_tree(build / "shaders" / name, folder / "shaders" / name)
        found += 1
    if found == 0:
        fail(f"{build}: no example programs found")


def stage_web(build: Path, stage: Path) -> None:
    names = [n for n in example_names() if (build / f"{n}.html").is_file()]
    if not names:
        fail(f"{build}: no example pages found")
    stage.mkdir(parents=True, exist_ok=True)
    for name in names:
        for suffix in WEB_SUFFIXES:
            path = build / f"{name}{suffix}"
            if path.is_file():
                shutil.copy2(path, stage / path.name)
    links = "\n".join(
        f'<li><a href="{html.escape(n)}.html">{html.escape(n)}</a></li>' for n in names)
    (stage / "index.html").write_text(
        "<!doctype html>\n<meta charset=\"utf-8\">\n<title>Drift examples</title>\n"
        "<h1>Drift examples</h1>\n<p>Serve this folder over HTTP; browsers do not load "
        "WebAssembly from file:// pages.</p>\n<ul>\n" + links + "\n</ul>\n",
        encoding="utf-8")


def main(args: list[str]) -> None:
    if len(args) != 3 or args[0] not in ("examples", "web", "dir"):
        print(__doc__.strip(), file=sys.stderr)
        raise SystemExit(2)
    kind, src, out = args[0], Path(args[1]).resolve(), Path(args[2]).resolve()
    if not src.is_dir():
        fail(f"{src}: not a directory")
    archive_stem(out)
    if kind == "dir":
        write_archive(src, out)
        return
    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "stage"
        if kind == "examples":
            stage_examples(src, stage)
        else:
            stage_web(src, stage)
        write_archive(stage, out)


if __name__ == "__main__":
    main(sys.argv[1:])
