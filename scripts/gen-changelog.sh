#!/usr/bin/env bash
# Generate GitHub release notes for a version, grouping commits since the
# previous clean semver tag by Conventional Commit type.
#
#   Usage: scripts/gen-changelog.sh <version>   # writes markdown to stdout
#
# feat -> Added, fix -> Fixed, perf/refactor/docs/build/ci/chore/style/test ->
# Changed, everything else -> Other. Run from the repo root with full history
# and tags available (checkout with fetch-depth: 0).
set -euo pipefail

version="${1:?usage: gen-changelog.sh <version>}"
repo="${GITHUB_REPOSITORY:-unisic/unisic}"

# Previous release = highest clean vX.Y.Z tag. The anchored regex deliberately
# skips the legacy vX.Y.Z-build.N.M tags produced by the old workflow.
prev="$(git tag -l 'v*' | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n1 || true)"
if [[ -n "$prev" ]]; then
  range="${prev}..HEAD"
else
  range="HEAD"
fi

added=""; fixed=""; changed=""; other=""
while IFS=$'\t' read -r hash subj; do
  [[ -z "$subj" ]] && continue
  # Strip the "type(scope)!: " prefix for the human-readable line.
  if [[ "$subj" == *": "* ]]; then desc="${subj#*: }"; else desc="$subj"; fi
  # Isolate the base type (drop any (scope) and trailing '!').
  type="${subj%%:*}"; base="${type%%(*}"; base="${base%!}"
  entry="- ${desc} (${hash})"$'\n'
  case "$base" in
    feat)                                                added+="$entry" ;;
    fix)                                                 fixed+="$entry" ;;
    perf|refactor|docs|build|ci|chore|style|test|revert) changed+="$entry" ;;
    *)                                                   other+="$entry" ;;
  esac
done < <(git log ${range} --no-merges --pretty=format:'%h%x09%s')

{
  if [[ -z "$added$fixed$changed$other" ]]; then
    echo "No notable changes."
  else
    [[ -n "$added"   ]] && { echo "### Added";   echo; printf '%s\n' "$added"; }
    [[ -n "$fixed"   ]] && { echo "### Fixed";   echo; printf '%s\n' "$fixed"; }
    [[ -n "$changed" ]] && { echo "### Changed"; echo; printf '%s\n' "$changed"; }
    [[ -n "$other"   ]] && { echo "### Other";   echo; printf '%s\n' "$other"; }
  fi
  if [[ -n "$prev" ]]; then
    echo "**Full changelog:** https://github.com/${repo}/compare/${prev}...v${version}"
  fi
}
