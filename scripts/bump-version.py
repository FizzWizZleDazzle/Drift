#!/usr/bin/env python3
"""Set, check or print the version this source tree calls itself.

Usage:

    scripts/bump-version.py 0.3.1          set every copy to 0.3.1
    scripts/bump-version.py --check 0.3.1  fail unless every copy is 0.3.1
    scripts/bump-version.py --current      print the version, failing if the
                                           copies disagree

The `project(drift VERSION ...)` call in CMakeLists.txt is where the version is
decided. The candela package in bindings/candela releases with the engine and
carries the same number in its candela.toml. A copy that lags behind ships a
package claiming a version the engine does not, so this moves them together.

Versions are plain X.Y.Z: CMake's project(VERSION) takes numeric components
only, so a prerelease suffix has nowhere to live.

.github/workflows/release.yml runs `--check` against the tag before it builds,
and runs this after the release publishes, to move main to the next patch
version. Run it by hand to go somewhere other than the next patch.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

VERSION = re.compile(r"^\d+\.\d+\.\d+$")


class Copy:
    """One place in the tree that spells the version out."""

    def __init__(self, path: str, pattern: str, what: str):
        self.path = ROOT / path
        self.pattern = re.compile(pattern, re.MULTILINE)
        self.what = what

    def read(self) -> str:
        text = self.path.read_text(encoding="utf-8")
        found = self.pattern.findall(text)
        if len(found) != 1:
            fail(f"{self.rel()}: expected one {self.what}, found {len(found)}")
        return found[0][1]

    def write(self, version: str) -> bool:
        text = self.path.read_text(encoding="utf-8")
        new = self.pattern.sub(lambda m: m.group(1) + version + m.group(3), text, count=1)
        if new == text:
            return False
        self.path.write_text(new, encoding="utf-8")
        return True

    def rel(self) -> str:
        return self.path.relative_to(ROOT).as_posix()


# The first entry is the source of truth; --current reads it.
COPIES = [
    Copy("CMakeLists.txt",
         r"^(project\(drift\s+VERSION\s+)(\d+\.\d+\.\d+)(\b)",
         "project(drift VERSION ...) call"),
    Copy("bindings/candela/candela.toml",
         r'(\[package\][^\[]*?^version\s*=\s*")([^"]*)(")',
         "[package] version"),
]


def fail(message: str) -> None:
    print(f"bump-version.py: {message}", file=sys.stderr)
    raise SystemExit(1)


def versions() -> dict[str, str]:
    return {copy.rel(): copy.read() for copy in COPIES}


def main(args: list[str]) -> None:
    if args == ["--current"]:
        found = versions()
        if len(set(found.values())) != 1:
            fail("the copies disagree: " + ", ".join(f"{p} {v}" for p, v in found.items()))
        print(COPIES[0].read())
        return

    if len(args) == 2 and args[0] == "--check":
        want = args[1]
        wrong = {p: v for p, v in versions().items() if v != want}
        for path, version in wrong.items():
            print(f"bump-version.py: {path} is at {version}, not {want}", file=sys.stderr)
        if wrong:
            fail(f"run scripts/bump-version.py {want}, or retag, so the two agree")
        return

    if len(args) != 1 or args[0].startswith("-"):
        print(__doc__.strip(), file=sys.stderr)
        raise SystemExit(2)

    version = args[0]
    if not VERSION.match(version):
        fail(f"'{version}' is not a plain X.Y.Z version")
    versions()  # every copy is readable before any is written
    for copy in COPIES:
        if copy.write(version):
            print(f"{copy.rel()}: {version}")


if __name__ == "__main__":
    main(sys.argv[1:])
