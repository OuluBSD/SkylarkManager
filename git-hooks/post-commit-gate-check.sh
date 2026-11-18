#!/bin/sh
# Optional post-commit hook template. Install as .git/hooks/post-commit.
set -eu
repo=$(git rev-parse --show-toplevel)
commit=$(git rev-parse --verify HEAD)
message=$(git log -1 --format=%B "$commit")
gate=$(printf '%s\n' "$message" | sed -n 's/.*gate-\([A-Za-z0-9._-]*\)-pass.*/\1/p' | head -n 1)
if [ -z "$gate" ]; then
	tag=$(git tag --points-at "$commit" --list 'gate-*-pass' | head -n 1)
	gate=$(printf '%s\n' "$tag" | sed -n 's/^gate-\(.*\)-pass$/\1/p')
fi
[ -n "$gate" ] || exit 0
track=${MA_TRACK_ID:?MA_TRACK_ID is required for a gate-tagged commit}
ensemble=${MA_ENSEMBLE:?MA_ENSEMBLE is required for a gate-tagged commit}
manager_root=${MA_SKYLARKMANAGER_ROOT:-/home/sblo/OuluBSD/SkylarkManager}
script=${MA_RECORD_GATE_SCRIPT:-"$manager_root/uppsrc/MultiAtlas/tools/record-gate-pass.sh"}
MA_TRACK_REPO=$repo
export MA_TRACK_REPO
exec "$script" "$ensemble" "$track" "$gate" "$commit"
