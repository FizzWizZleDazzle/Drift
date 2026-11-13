#!/bin/sh
# Drift toolchain installer.
#
#   curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh
#
# Resolves a release of FizzWizZleDazzle/Drift (latest by default, or the one
# --version names), downloads the archive for this platform, checks it against
# the release's sha256sums.txt, and unpacks it under ~/.drift. Nothing is
# written outside the prefix except an optional PATH line in a shell rc file,
# added only with consent. Nothing here needs root.
#
# Every request is a plain file download from
#
#   https://github.com/FizzWizZleDazzle/Drift/releases/download/<tag>/<asset>
#
# and the latest tag is where https://github.com/<repo>/releases/latest
# redirects to. --from replaces that whole location with a folder or URL laid
# out the same way (an archive and sha256sums.txt), for trying a build before
# it is released.
#
# The asset names are the contract with .github/workflows/build-artifacts.yml:
#
#   drift-<os>-<arch>.tar.gz   os in {linux, macos}, arch in {x86_64, aarch64}
#   drift-windows-x86_64.zip   installed by install.ps1, not by this script
#   sha256sums.txt             sha256sum's format, one line per asset
#
# The archive holds one folder with the tree to install:
#
#   bin/drift                  the drift command
#   lib/drift/module/          the `drift` Candela module
#   lib/drift/libs/            the Candela standard library drift was built with
#   lib/drift/web/             the web runtime `drift build --web` copies
#
# Every installed path is recorded in <prefix>/receipt, so a later run
# replaces an old version exactly and --uninstall removes it.

set -eu

GH_REPO="${DRIFT_GH_REPO:-FizzWizZleDazzle/Drift}"
GH_URL="https://github.com/$GH_REPO"
PREFIX="${DRIFT_PREFIX:-$HOME/.drift}"

PIN_VERSION=""
FROM=""
NO_CONFIRM=0
MODIFY_PATH=1
FORCE=0
UNINSTALL=0

say() { printf '%s\n' "$*"; }
fail() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'EOF'
Drift toolchain installer.

Usage:
  install.sh [options]

Installs the drift command, which runs Candela games and builds them for the
web, under a per-user prefix. Never uses sudo.

Options:
  --prefix DIR         Install root. Default: ~/.drift
  --version VERSION    Install this release instead of the latest.
  --from DIR|URL       Install from a folder or URL holding the archive and
                       sha256sums.txt, instead of a GitHub release.
  --no-confirm         Run without prompting; still writes a PATH line to a
                       shell rc file unless --no-modify-path is also given.
  --no-modify-path     Never write a PATH line to a shell rc file.
  --force              Reinstall even if already at the target version.
  --uninstall          Remove every file this installer put under the prefix.
  -h, --help           Show this help.

Environment:
  DRIFT_GH_REPO   GitHub repo to install from, as owner/name.
                  Default: FizzWizZleDazzle/Drift
  DRIFT_PREFIX    Same as --prefix.
EOF
}

# --- arguments ---------------------------------------------------------------

while [ "$#" -gt 0 ]; do
  case "$1" in
    --prefix)
      [ "$#" -ge 2 ] || fail "--prefix needs a directory"
      PREFIX="$2"
      shift 2
      ;;
    --prefix=*) PREFIX="${1#--prefix=}"; shift ;;
    --version)
      [ "$#" -ge 2 ] || fail "--version needs a version"
      PIN_VERSION="$2"
      shift 2
      ;;
    --version=*) PIN_VERSION="${1#--version=}"; shift ;;
    --from)
      [ "$#" -ge 2 ] || fail "--from needs a folder or URL"
      FROM="$2"
      shift 2
      ;;
    --from=*) FROM="${1#--from=}"; shift ;;
    --no-confirm) NO_CONFIRM=1; shift ;;
    --no-modify-path) MODIFY_PATH=0; shift ;;
    --force) FORCE=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) fail "unknown option: $1 (try --help)" ;;
  esac
done

[ -n "$PREFIX" ] || fail "--prefix needs a directory"
[ -z "$FROM" ] || [ -z "$PIN_VERSION" ] || fail "--from and --version do not go together; --from names the files to install"

case "$PREFIX" in
  /*) ;;
  ~*) PREFIX="$HOME${PREFIX#\~}" ;;
  *) PREFIX="$PWD/$PREFIX" ;;
esac

BIN_DIR="$PREFIX/bin"
RECEIPT="$PREFIX/receipt"

# --- tools -------------------------------------------------------------------

if command -v curl >/dev/null 2>&1; then
  DOWNLOADER=curl
elif command -v wget >/dev/null 2>&1; then
  DOWNLOADER=wget
else
  DOWNLOADER=none
fi

if command -v sha256sum >/dev/null 2>&1; then
  HASHER=sha256sum
elif command -v shasum >/dev/null 2>&1; then
  HASHER=shasum
else
  HASHER=none
fi

# A location that is not a URL is a local path, copied rather than downloaded.
is_url() {
  case "$1" in
    http://*|https://*|file://*) return 0 ;;
    *) return 1 ;;
  esac
}

fetch() {
  # fetch URL|PATH DEST [shown]
  if ! is_url "$1"; then
    [ -f "$1" ] || return 1
    cp "$1" "$2"
    return 0
  fi
  case "$DOWNLOADER" in
    curl)
      if [ "${3:-}" = shown ]; then
        curl -fSL --progress-bar -o "$2" "$1"
      else
        curl -fsSL -o "$2" "$1"
      fi
      ;;
    wget)
      if [ "${3:-}" = shown ]; then wget -O "$2" "$1"; else wget -q -O "$2" "$1"; fi
      ;;
    *) fail "need curl or wget" ;;
  esac
}

sha256_of() {
  case "$HASHER" in
    sha256sum) sha256sum "$1" | cut -d' ' -f1 ;;
    shasum) shasum -a 256 "$1" | cut -d' ' -f1 ;;
    *) fail "need sha256sum or shasum to check downloads" ;;
  esac
}

final_url() {
  # final_url URL -> the URL a GET of URL ends at, after redirects.
  case "$DOWNLOADER" in
    curl) curl -fsSL -o /dev/null -w '%{url_effective}' "$1" ;;
    wget)
      wget --server-response --spider "$1" 2>&1 |
        awk 'tolower($1) == "location:" { print $2 }' |
        tail -n 1
      ;;
    *) fail "need curl or wget" ;;
  esac
}

# --- prompts -----------------------------------------------------------------

# Reads from the terminal, not stdin: with `curl ... | sh` stdin is the script
# itself. Without a terminal the answer is no, and --no-confirm is the way
# through.
ask() {
  if [ "$NO_CONFIRM" -eq 1 ]; then
    return 0
  fi
  if ! ( : >/dev/tty ) 2>/dev/null; then
    say "No terminal to ask on. Re-run with --no-confirm to accept the defaults."
    return 1
  fi
  printf '%s [Y/n] ' "$1" >/dev/tty
  ask_reply=""
  read -r ask_reply </dev/tty 2>/dev/null || ask_reply=n
  case "$ask_reply" in
    ''|y|Y|yes|Yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

# --- receipt -------------------------------------------------------------------
#
#   version 0.3.0
#   target linux-x86_64
#   file bin/drift
#   file lib/drift/module/src/main.cdl

receipt_version() {
  [ -f "$RECEIPT" ] || return 0
  awk '$1 == "version" { print $2; exit }' "$RECEIPT"
}

receipt_files() {
  [ -f "$RECEIPT" ] || return 0
  awk '$1 == "file" { print substr($0, 6) }' "$RECEIPT"
}

prune_dirs() {
  # Removes directories left empty by a removal. rmdir refuses non-empty ones.
  [ -d "$PREFIX" ] || return 0
  find "$PREFIX" -depth -type d -exec rmdir {} + 2>/dev/null || true
}

# --- uninstall ---------------------------------------------------------------

if [ "$UNINSTALL" -eq 1 ]; then
  if [ ! -f "$RECEIPT" ]; then
    say "Nothing to uninstall: no Drift install found at $PREFIX"
    exit 0
  fi
  say "Removing from $PREFIX:"
  say "  drift $(receipt_version)"
  if ! ask "Remove it?"; then
    say "Cancelled."
    exit 1
  fi
  receipt_files | while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    rm -f "$PREFIX/$rel"
  done
  rm -f "$RECEIPT"
  prune_dirs
  say "Removed. If a PATH line for $BIN_DIR is still in a shell rc file, delete it by hand."
  exit 0
fi

# --- platform ----------------------------------------------------------------

UNAME_S="$(uname -s)"
UNAME_M="$(uname -m)"

case "$UNAME_S" in
  Linux) OS=linux ;;
  Darwin) OS=macos ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    say "On Windows, install from PowerShell:"
    say "  irm https://raw.githubusercontent.com/$GH_REPO/main/install.ps1 | iex"
    exit 1
    ;;
  *) fail "unsupported operating system: $UNAME_S. Drift ships for Linux, macOS and Windows." ;;
esac

case "$UNAME_M" in
  x86_64|amd64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=aarch64 ;;
  *) fail "unsupported architecture: $UNAME_M. Drift ships for x86_64 and aarch64." ;;
esac

TARGET="$OS-$ARCH"
ASSET_NAME="drift-$TARGET.tar.gz"

[ "$HASHER" != none ] || fail "need sha256sum or shasum to check downloads"

# --- resolve the release ------------------------------------------------------

TMP="$(mktemp -d "${TMPDIR:-/tmp}/drift-install.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

SUMS="$TMP/sha256sums.txt"

if [ -n "$FROM" ]; then
  BASE="${FROM%/}"
  RELEASE=""
  fetch "$BASE/sha256sums.txt" "$SUMS" ||
    fail "$BASE has no sha256sums.txt; --from names a folder or URL holding $ASSET_NAME and sha256sums.txt"
else
  [ "$DOWNLOADER" != none ] || fail "need curl or wget"
  # A pinned version is tried as given, then with a "v" prefix: tags are
  # vX.Y.Z, but --version takes the bare number too. sha256sums.txt is the
  # probe, since a release without it cannot be checked.
  if [ -n "$PIN_VERSION" ]; then
    TAG="$PIN_VERSION"
    if ! fetch "$GH_URL/releases/download/$TAG/sha256sums.txt" "$SUMS" 2>/dev/null; then
      case "$PIN_VERSION" in
        v*) fail "release $PIN_VERSION of $GH_REPO has no sha256sums.txt. See $GH_URL/releases" ;;
        *)
          TAG="v$PIN_VERSION"
          fetch "$GH_URL/releases/download/$TAG/sha256sums.txt" "$SUMS" ||
            fail "no release $PIN_VERSION of $GH_REPO (tried tags $PIN_VERSION and $TAG). See $GH_URL/releases"
          ;;
      esac
    fi
  else
    # /releases/latest redirects to /releases/tag/<tag>.
    LATEST_URL="$(final_url "$GH_URL/releases/latest" || true)"
    TAG="${LATEST_URL##*/}"
    case "$TAG" in
      ''|latest|releases)
        fail "could not resolve the latest release of $GH_REPO. Either it has no releases yet, or the request did not get through. See $GH_URL/releases"
        ;;
    esac
    fetch "$GH_URL/releases/download/$TAG/sha256sums.txt" "$SUMS" ||
      fail "release $TAG of $GH_REPO has no sha256sums.txt. See $GH_URL/releases"
  fi
  BASE="$GH_URL/releases/download/$TAG"
  RELEASE="${TAG#v}"
fi

[ -s "$SUMS" ] || fail "$BASE/sha256sums.txt is empty"

ASSET_SHA="$(awk -v want="$ASSET_NAME" '
  NF >= 2 {
    name = $2
    sub(/^\*/, "", name)
    if (name == want) { print $1; exit }
  }' "$SUMS")"
[ -n "$ASSET_SHA" ] || fail "$BASE publishes no $ASSET_NAME, so there is no Drift for $TARGET there. See $GH_URL/releases"

INSTALLED="$(receipt_version)"

up_to_date() {
  say "Already up to date: drift $INSTALLED at $PREFIX"
  say "Use --force to reinstall."
  exit 0
}

if [ "$FORCE" -eq 0 ] && [ -n "$RELEASE" ] && [ "$INSTALLED" = "$RELEASE" ]; then
  up_to_date
fi

say ""
say "Drift toolchain installer"
say ""
say "  release   ${RELEASE:-from $BASE}"
say "  target    $TARGET"
say "  prefix    $PREFIX"
say ""

if ! ask "Install?"; then
  say "Cancelled. Nothing was written."
  exit 1
fi

# --- download and check ------------------------------------------------------

say "Downloading $ASSET_NAME"
fetch "$BASE/$ASSET_NAME" "$TMP/drift.tar.gz" shown || fail "download failed: $BASE/$ASSET_NAME"

got="$(sha256_of "$TMP/drift.tar.gz")"
if [ "$got" != "$ASSET_SHA" ]; then
  fail "checksum mismatch for $ASSET_NAME
  expected $ASSET_SHA
  got      $got
Nothing was installed. The download was corrupted, or the file does not match the sha256sums.txt beside it."
fi

# --- unpack ------------------------------------------------------------------

root="$TMP/x"
mkdir -p "$root"
tar -xzf "$TMP/drift.tar.gz" -C "$root" || fail "could not unpack $ASSET_NAME"

# The archive holds one folder named after itself.
if [ ! -f "$root/bin/drift" ] && [ -f "$root/drift-$TARGET/bin/drift" ]; then
  root="$root/drift-$TARGET"
fi
[ -f "$root/bin/drift" ] || fail "$ASSET_NAME has no bin/drift in it"
[ -d "$root/lib/drift/module" ] || fail "$ASSET_NAME has no lib/drift/module in it"
[ -d "$root/lib/drift/web" ] || fail "$ASSET_NAME has no lib/drift/web in it"

if [ -z "$RELEASE" ]; then
  # Installing from --from: the program says which version it is.
  RELEASE="$("$root/bin/drift" --version 2>/dev/null | awk '$1 == "drift" { print $2; exit }')" ||
    true
  [ -n "$RELEASE" ] || fail "the drift program in $ASSET_NAME does not run on this machine"
  if [ "$FORCE" -eq 0 ] && [ "$INSTALLED" = "$RELEASE" ]; then
    up_to_date
  fi
fi

( cd "$root" && find . \( -type f -o -type l \) -print ) | sed 's|^\./||' | sort > "$TMP/files"

if [ -n "$INSTALLED" ]; then
  say "Installing drift $RELEASE over $INSTALLED"
else
  say "Installing drift $RELEASE"
fi
while IFS= read -r rel; do
  dest="$PREFIX/$rel"
  mkdir -p "$(dirname "$dest")"
  rm -f "$dest"
  cp -p "$root/$rel" "$dest"
done < "$TMP/files"

# Files the previous version installed and this one does not.
receipt_files | sort > "$TMP/old" || true
if [ -s "$TMP/old" ]; then
  comm -23 "$TMP/old" "$TMP/files" | while IFS= read -r stale; do
    [ -n "$stale" ] || continue
    rm -f "$PREFIX/$stale"
  done
fi

{
  printf 'version %s\n' "$RELEASE"
  printf 'target %s\n' "$TARGET"
  sed 's/^/file /' "$TMP/files"
} > "$RECEIPT"

chmod 755 "$BIN_DIR/drift"
prune_dirs

# --- PATH --------------------------------------------------------------------

# The rc line keeps $PATH unexpanded on purpose: the shell that reads the file
# expands it.
# shellcheck disable=SC2016
path_line_for() {
  case "$1" in
    */fish) printf 'set -gx PATH "%s" $PATH\n' "$BIN_DIR" ;;
    *) printf 'export PATH="%s:$PATH"\n' "$BIN_DIR" ;;
  esac
}

rc_file_for() {
  case "$1" in
    */fish) printf '%s\n' "$HOME/.config/fish/config.fish" ;;
    */zsh) printf '%s\n' "$HOME/.zshrc" ;;
    */bash)
      if [ "$OS" = macos ] && [ -f "$HOME/.bash_profile" ]; then
        printf '%s\n' "$HOME/.bash_profile"
      else
        printf '%s\n' "$HOME/.bashrc"
      fi
      ;;
    *) printf '%s\n' "$HOME/.profile" ;;
  esac
}

say ""
case ":$PATH:" in
  *":$BIN_DIR:"*) ;;
  *)
    RC="$(rc_file_for "${SHELL:-/bin/sh}")"
    LINE="$(path_line_for "${SHELL:-/bin/sh}")"
    if [ -f "$RC" ] && grep -q -F "$BIN_DIR" "$RC" 2>/dev/null; then
      say "$BIN_DIR is already in $RC. Open a new shell to pick it up."
    elif [ "$MODIFY_PATH" -eq 0 ]; then
      say "Add $BIN_DIR to your PATH:"
      say "  $LINE"
    elif ask "Add $BIN_DIR to your PATH in $RC?"; then
      mkdir -p "$(dirname "$RC")"
      {
        printf '\n# added by the Drift installer\n'
        printf '%s\n' "$LINE"
      } >> "$RC"
      say "Added to $RC. Open a new shell, or run:"
      say "  $LINE"
    else
      say "Left your shell configuration alone. To use drift, add:"
      say "  $LINE"
    fi
    ;;
esac

say ""
say "Installed drift $RELEASE under $PREFIX."
say ""
say "Get started:"
say "  drift run path/to/game         play a game folder"
say "  drift build --web path/to/game  write it as a web page"
