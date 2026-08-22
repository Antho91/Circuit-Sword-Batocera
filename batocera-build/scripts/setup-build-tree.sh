#!/bin/bash
# Corrected 2026-08-22: this script is NOT needed for normal setup
# anymore. batocera-build/build/batocera.linux (the full buildable
# dev-tree, buildroot included as plain files) is tracked directly in
# this repo now -- a plain `git clone` of this repo already gives you
# everything, ready to build. batocera-build/patches/ +
# batocera-build/overlay/ are kept only as a legacy record of what's
# inside that tree, not the source of truth anymore.
#
# This script now exists purely as a disaster-recovery / verification
# tool: rebuild a *fresh, standalone* checkout from those patch files
# in a scratch location outside this repo, e.g. to confirm the patches
# still apply cleanly against upstream after upstream has moved on.
# It refuses to run against this repo's own $BATOCERA_SRC (the default)
# -- that path holds real, committed history now, not a disposable
# checkout, and this script's whole model (reset + git-clean + re-apply
# patches) would destroy it. You must explicitly point BATOCERA_SRC
# somewhere else to use this script at all.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

REPO_ROOT="$(cd ../.. && pwd)"
PATCH_DIR="$REPO_ROOT/batocera-build/patches"
OVERLAY_DIR="$REPO_ROOT/batocera-build/overlay"
PINNED="$REPO_ROOT/batocera-build/PINNED_COMMITS.txt"
source "$PINNED"

echo "=== batocera.linux checkout: $BATOCERA_SRC ==="

DEFAULT_BATOCERA_SRC="$REPO_ROOT/batocera-build/build/batocera.linux"
if [ "$BATOCERA_SRC" = "$DEFAULT_BATOCERA_SRC" ]; then
    echo "ERROR: BATOCERA_SRC is this repo's own default dev-tree location" >&2
    echo "($DEFAULT_BATOCERA_SRC)." >&2
    echo "That directory holds real, committed project history now (git" >&2
    echo "log it yourself to see) -- it's not a disposable checkout, and" >&2
    echo "this script's model (reset to the pinned commit + git-clean +" >&2
    echo "re-apply patches) would destroy that history." >&2
    echo "" >&2
    echo "This script is only for rebuilding a fresh, standalone checkout" >&2
    echo "elsewhere, e.g. to verify the patch files still apply cleanly:" >&2
    echo "  export BATOCERA_SRC=/path/outside/this/repo/batocera.linux" >&2
    echo "  $0" >&2
    exit 1
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
