#!/bin/bash
# OPT-IN ONLY (BR_DOCKER_VOLUMES=0) -- the default build mode
# (BR_DOCKER_VOLUMES=1, see env.sh) puts the build in Docker named
# volumes instead and never needs this script at all. Only run this if
# you've deliberately set BR_DOCKER_VOLUMES=0 (e.g. to inspect
# intermediate build files in Finder, or the named-volume approach isn't
# working for you).
#
# One-time (then re-run any time to just remount): create the
# case-sensitive APFS disk image used for OUTPUT_DIR/DL_DIR/CCACHE_DIR,
# and mount it directly at $BATOCERA_BUILD_ROOT (project_root/output/ by
# default) via `hdiutil attach -mountpoint`, so it shows up right inside
# the project tree instead of under /Volumes.
#
# macOS-only -- on Linux/WSL2 just use a regular directory with 170GB+
# free (e.g. `mkdir -p output`) and skip this script entirely.
#
# Why case-sensitive: glibc's Buildroot build creates stamp files that
# collide on the default case-insensitive APFS, breaking the build
# part-way through. Confirmed on this project (see WIFI-BUILD-FINDINGS.md).
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

: "${DMG_PATH:=$REPO_ROOT/batocera-build/BatoceraBuild.dmg}"
: "${DMG_SIZE_GB:=170}"

if mount | grep -q " on $BATOCERA_BUILD_ROOT "; then
    echo "Already mounted at $BATOCERA_BUILD_ROOT."
    exit 0
fi

mkdir -p "$BATOCERA_BUILD_ROOT"
if [ -n "$(ls -A "$BATOCERA_BUILD_ROOT" 2>/dev/null)" ]; then
    echo "ERROR: $BATOCERA_BUILD_ROOT exists and is not empty, and nothing is mounted there." >&2
    echo "hdiutil requires an empty directory as a mountpoint. Move its contents aside first." >&2
    exit 1
fi

if [ ! -e "$DMG_PATH" ]; then
    mkdir -p "$(dirname "$DMG_PATH")"
    echo "Creating ${DMG_SIZE_GB}GB case-sensitive APFS disk image at $DMG_PATH ..."
    hdiutil create -size "${DMG_SIZE_GB}g" -fs "Case-sensitive APFS" -volname BatoceraBuild "$DMG_PATH"
fi

echo "Attaching at $BATOCERA_BUILD_ROOT (-owners on, required every time so Docker's container UID can chmod)..."
hdiutil attach "$DMG_PATH" -mountpoint "$BATOCERA_BUILD_ROOT" -owners on

mkdir -p "$OUTPUT_DIR" "$DL_DIR" "$CCACHE_DIR"
echo "Ready: $BATOCERA_BUILD_ROOT"
echo ""
echo "To resize later: hdiutil detach '$BATOCERA_BUILD_ROOT' && hdiutil resize -size <N>g '$DMG_PATH' && hdiutil attach '$DMG_PATH' -mountpoint '$BATOCERA_BUILD_ROOT' -owners on"
