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
