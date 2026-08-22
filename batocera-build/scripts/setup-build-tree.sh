#!/bin/bash
# One-time: clone batocera.linux at the pinned commit, apply this
# project's local patches, and drop in the Circuit-Sword-specific
# overlay files (WiFi stability configs). Safe to re-run -- it detects
# an existing checkout and just re-applies patches on top after
# resetting to the pinned commit (so a botched local edit doesn't
# silently persist across a re-run).
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

REPO_ROOT="$(cd ../.. && pwd)"
PATCH_DIR="$REPO_ROOT/batocera-build/patches"
OVERLAY_DIR="$REPO_ROOT/batocera-build/overlay"
PINNED="$REPO_ROOT/batocera-build/PINNED_COMMITS.txt"
source "$PINNED"

echo "=== batocera.linux checkout: $BATOCERA_SRC ==="

# Safety guard: this checkout is meant to be disposable (this script's
# whole model is "reset to the pinned commit, re-apply patches"), so a
# real, committed development history here -- not just uncommitted
# patch-applied working-tree changes -- means this is NOT the checkout
# this script thinks it is. Re-running against one would silently
# detach HEAD and git-clean away every one of those commits. Detect it
# by checking whether HEAD has any commits beyond the pinned commit
# (patch-apply via `git apply` never creates commits, so a genuine
# disposable checkout always sits exactly at the pinned commit).
if [ -d "$BATOCERA_SRC/.git" ] && [ "${FORCE_SETUP_BUILD_TREE:-}" != "1" ]; then
    (cd "$BATOCERA_SRC" && git fetch origin "$BATOCERA_LINUX_COMMIT" >/dev/null 2>&1 || true)
    AHEAD_COMMITS=$(cd "$BATOCERA_SRC" && git rev-list --count "$BATOCERA_LINUX_COMMIT..HEAD" 2>/dev/null || echo "?")
    if [ "$AHEAD_COMMITS" != "0" ] && [ "$AHEAD_COMMITS" != "?" ]; then
        echo "ERROR: $BATOCERA_SRC has $AHEAD_COMMITS real commit(s) beyond" >&2
        echo "the pinned commit ($BATOCERA_LINUX_COMMIT) -- this looks like a" >&2
        echo "genuine development history, not a disposable patch-applied" >&2
        echo "checkout. Running this script would git-clean and reset-away" >&2
        echo "that history." >&2
        echo "" >&2
        echo "If this is really the dev-tree you mean to regenerate from" >&2
        echo "scratch (its history already captured elsewhere, e.g. pushed" >&2
        echo "to a remote, or you've confirmed batocera-linux.patch already" >&2
        echo "reflects everything you need), re-run with:" >&2
        echo "  FORCE_SETUP_BUILD_TREE=1 $0" >&2
        exit 1
    fi
fi

if [ ! -d "$BATOCERA_SRC/.git" ]; then
    echo "Cloning $BATOCERA_LINUX_REPO (this pulls the buildroot submodule too, several hundred MB) ..."
    mkdir -p "$(dirname "$BATOCERA_SRC")"
    git clone --recurse-submodules "$BATOCERA_LINUX_REPO" "$BATOCERA_SRC"
fi

cd "$BATOCERA_SRC"

echo "Resetting to pinned commit $BATOCERA_LINUX_COMMIT ($BATOCERA_LINUX_TAG) ..."
git fetch origin "$BATOCERA_LINUX_COMMIT" 2>/dev/null || git fetch origin --tags
git checkout --detach "$BATOCERA_LINUX_COMMIT"
git submodule update --init --recursive
(cd buildroot && git checkout --detach "$BUILDROOT_COMMIT")

echo "Discarding any prior local changes (this script is the source of truth for patches) ..."
git clean -fdx -e '*.dmg' >/dev/null
git checkout -- .
(cd buildroot && git clean -fdx >/dev/null && git checkout -- .)

echo "Applying batocera-linux.patch ..."
git apply --whitespace=nowarn "$PATCH_DIR/batocera-linux.patch"

echo "Applying buildroot.patch (inside the buildroot submodule) ..."
(cd buildroot && git apply --whitespace=nowarn "$PATCH_DIR/buildroot.patch")

echo "Copying loose patch files that Buildroot applies to downloaded sources at build time ..."
mkdir -p package/batocera/utils/xxd
cp "$PATCH_DIR/0001-remove-broken-K-R-forward-declarations.patch" \
   package/batocera/utils/xxd/
cp "$PATCH_DIR/0004-linux-user-fix-redefinition-of-struct-sched_attr.patch" \
   buildroot/package/qemu/

echo "Copying Circuit-Sword overlay files (WiFi stability configs) ..."
cp -R "$OVERLAY_DIR"/. .

echo "=== Done. Verifying with git status (should show only the patched/copied files) ==="
git status --short
(cd buildroot && git status --short)

echo ""
echo "Next: batocera-build/scripts/setup-disk-image.sh (macOS only), then"
echo "batocera-build/scripts/build-image.sh"
