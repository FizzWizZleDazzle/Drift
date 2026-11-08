#!/usr/bin/env sh
# Write the notes for a release: scripts/release-notes.sh <tag> <out-file>
#
# Drift lands work on main without pull requests, so GitHub's generated notes,
# which list merged pull requests, would be empty. These come from the history
# instead: the message of an annotated tag, when the tag has one, then the
# subject of every first-parent commit since the previous vX.Y.Z tag, then a
# link to the full comparison. First-parent keeps a merged branch to its merge
# commit's subject. The automated version bumps release.yml commits are left
# out, since every release has one and none of them is news.
set -eu

tag="${1:?usage: release-notes.sh <tag> <out-file>}"
out="${2:?usage: release-notes.sh <tag> <out-file>}"
repo="${GITHUB_REPOSITORY:-FizzWizZleDazzle/Drift}"

previous="$(git describe --tags --abbrev=0 --match 'v[0-9]*.[0-9]*.[0-9]*' "${tag}^" 2>/dev/null || true)"

{
    if [ "$(git cat-file -t "$tag")" = "tag" ]; then
        body="$(git for-each-ref --format='%(contents)' "refs/tags/${tag}" | sed '/-----BEGIN PGP SIGNATURE-----/,$d')"
        if [ -n "$body" ]; then
            printf '%s\n\n' "$body"
        fi
    fi

    echo "## Changes"
    echo
    if [ -n "$previous" ]; then
        range="${previous}..${tag}"
    else
        range="$tag"
    fi
    git log --first-parent --format='- %s' "$range" | grep -v '^- Set the version to [0-9]' || true

    echo
    if [ -n "$previous" ]; then
        echo "Full comparison: https://github.com/${repo}/compare/${previous}...${tag}"
    else
        echo "First release; the list above is the whole history."
    fi
} > "$out"
