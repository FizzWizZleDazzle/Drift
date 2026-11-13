# Releases

Drift publishes releases on [GitHub](https://github.com/FizzWizZleDazzle/Drift/releases). Each one is a version tag `vX.Y.Z` and carries the `drift` command for Candela games, the source archive to build against, and ready-to-run examples. This page lists what a release contains and how a maintainer cuts one. To build your game against a release, see [Using a release](../getting-started/installation.md#using-a-release).

## What a release contains

| Asset | What it is |
|-------|------------|
| `drift-linux-x86_64.tar.gz` | The `drift` command for Linux, which `install.sh` installs |
| `drift-macos-aarch64.tar.gz` | The `drift` command for Apple silicon Macs, which `install.sh` installs |
| `drift-windows-x86_64.zip` | The `drift` command for Windows, which `install.ps1` installs |
| `drift-src.tar.gz` | The source tree at the tag, for `FetchContent` with a `URL` and `URL_HASH` |
| `drift-examples-linux-x86_64.tar.gz` | The example programs for Linux |
| `drift-examples-macos-aarch64.tar.gz` | The example programs for Apple silicon Macs |
| `drift-examples-windows-x86_64.zip` | The example programs for Windows |
| `drift-examples-web.zip` | The examples built for the browser, with an `index.html` linking them |
| `drift-docs.tar.gz` | This book, built |
| `sha256sums.txt` | The SHA-256 digest of every file above |

Asset names carry no version; the version is in the download URL, `https://github.com/FizzWizZleDazzle/Drift/releases/download/vX.Y.Z/<asset>`. `releases/latest/download/<asset>` always points at the newest release.

Each archive unpacks into one folder named after it. A `drift` archive holds `bin/drift` and, under `lib/drift/`, the `drift` Candela module, the candela standard library and the web runtime `drift build --web` copies; [the Candela book](https://github.com/FizzWizZleDazzle/Drift/blob/main/bindings/candela/docs/src/getting-started/install.md) covers installing it. The installers at the root of the repository, `install.sh` and `install.ps1`, depend on these asset names and on `sha256sums.txt`.

The desktop example archives hold a folder per example with its program, `drift.toml` and assets; examples load assets from the working directory, so run one from its folder:

```bash
tar xzf drift-examples-linux-x86_64.tar.gz
cd drift-examples-linux-x86_64/hello_sprites && ./hello_sprites
```

The web archive needs an HTTP server, since browsers do not load WebAssembly from `file://` pages:

```bash
unzip drift-examples-web.zip
cd drift-examples-web && python3 -m http.server 8000   # open http://localhost:8000
```

Check a download against the published digests with `sha256sum -c --ignore-missing sha256sums.txt` in the folder holding both.

The examples use the default GL / WebGL2 backend and keep the dev overlay on. The macOS programs are not signed, so macOS asks before opening them; clear the quarantine flag with `xattr -dr com.apple.quarantine drift-examples-macos-aarch64`. The Linux programs are built on Ubuntu 24.04 and need a C library at least that new.

## Versions

`project(drift VERSION ...)` in `CMakeLists.txt` is the version of the tree. The Candela parts, the `drift` module (`bindings/candela/drift/candela.toml`) and the host crate (`bindings/candela/host/Cargo.toml`), release with the engine and carry the same number. `scripts/bump-version.py` moves them together:

```bash
scripts/bump-version.py 0.4.0       # set every copy
scripts/bump-version.py --check 0.4.0
scripts/bump-version.py --current
```

Versions are plain `X.Y.Z`. CMake's `project(VERSION)` takes numbers only, so there are no prerelease tags.

Main always carries the version the next release will have. After a release goes out, the release workflow commits the next patch version to main by itself.

## Cutting a release

1. Check that `ci` is green on the commit you want to release.
2. For a patch release, main already names the right version. For a minor or major release, run `scripts/bump-version.py X.Y.0` on main and push that commit first.
3. Tag the commit and push the tag. The tag's message opens the release notes, so write the highlights there:

   ```bash
   git tag -a v0.4.0 -m "Short summary of what this release brings."
   git push origin v0.4.0
   ```

The `release` workflow then:

1. Fails at once if the tag disagrees with the version in the tree.
2. Builds every asset in the table above, on Linux, macOS, Windows and Emscripten. The web runtime is built once with Emscripten and packed into each `drift` archive.
3. Publishes the release. The notes are the tag's message, then the subject of every main commit since the previous release, then a comparison link.
4. Republishes the docs site from the tag.
5. Commits `Set the version to X.Y.(Z+1)` to main.

If a build leg fails, fix it on main, delete the tag locally and on GitHub, and tag the fixed commit again. If only the docs or the version bump fail, re-run that job from the workflow run page; the bump job sees when main is already at or past the next version and does nothing.

## The candela version

`drift` embeds candela, built from source by Cargo. Until the candela changes Drift needs are released (lumen-fx/candela#246), CI and release builds check out one pinned candela commit and pass it as `-DDRIFT_CANDELA_SOURCE`. The pin is the `CANDELA_COMMIT` line in `.github/actions/candela/action.yml`, the one place every workflow takes it from. To move to a candela release, empty that line and set the release in `bindings/candela/host/Cargo.toml`.

## One-time repository setup

The workflows need two things configured in the repository settings.

**The `BUMP_DEPLOY_KEY` secret.** The version bump pushes to main over SSH with a deploy key, since a push made with the workflow token starts no `ci` run. Create a key pair, add the public half as a deploy key with write access, and the private half as an Actions secret:

```bash
ssh-keygen -t ed25519 -N "" -C "drift release bump" -f drift-bump
gh repo deploy-key add drift-bump.pub --repo FizzWizZleDazzle/Drift --allow-write --title "release bump"
gh secret set BUMP_DEPLOY_KEY --repo FizzWizZleDazzle/Drift < drift-bump
rm drift-bump drift-bump.pub
```

Without the secret, the release still publishes and the bump job fails with a message naming the command to run by hand.

**A tag rule on the `github-pages` environment.** Pages deploys only from refs the environment allows, and a release run is on the tag. Under Settings, Environments, `github-pages`, Deployment branches and tags, add a tag rule `v*` beside `main`.
