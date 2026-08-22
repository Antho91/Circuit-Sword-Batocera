#!/bin/bash
# Rebuild one package after editing its source/patches, then repackage
# the image. Handles CLAUDE.md Hard Rule #7 automatically: a full
# `bcm2837-build` does NOT pick up edited source in an already-built
# package, so this always forces a `-dirclean` on the target package
# first -- safe for every package type (patch-based/compiled or
# plain-copy), just possibly slower than the cheaper `-reinstall` for
# large plain-copy packages that don't actually need re-extraction.
#
# Usage:
#   rebuild-package.sh <package-name> [reinstall]
#
# The optional second argument switches from the default `dirclean`
# (safe for everything, forces a full re-extract + re-patch + rebuild)
# to the cheaper `reinstall` (only correct for plain-copy/config-file
# packages that don't need re-extraction/re-patching -- re-runs only
# INSTALL_TARGET_CMDS). Get this wrong and your edit silently won't
# apply -- when in doubt, leave it on the default.
#
# Example: after editing 007-fix-buttonimage-invertbuttons.patch (a
# patch-based, compiled package):
#   batocera-build/scripts/rebuild-package.sh batocera-emulationstation
#
# Example: after editing circuitsword-daemon-config (a plain-copy
# shell script, no compile step):
#   batocera-build/scripts/rebuild-package.sh rpigpioswitch reinstall
set -euo pipefail
cd "$(dirname "$0")"
SCRIPT_DIR="$(pwd)"
source ./env.sh
require_src
require_disk
require_docker

PKG_NAME="${1:?usage: rebuild-package.sh <package-name> [reinstall]}"
MODE="${2:-dirclean}"

case "$MODE" in
    dirclean|reinstall) ;;
    *)
        echo "ERROR: second argument must be 'dirclean' (default) or 'reinstall', got: $MODE" >&2
        exit 2
        ;;
esac

sync_batocera_mk
seed_docker_volumes

cd "$BATOCERA_SRC"

echo "Forcing a clean $MODE of $PKG_NAME ..."
make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 PKG="$PKG_NAME-$MODE" "$BOARD-pkg"

echo "Rebuilding $PKG_NAME ..."
make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 PKG="$PKG_NAME" "$BOARD-pkg"

echo "Repackaging the image ..."
echo "Logging to $LOG_FILE"
(
    set +e
    make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 "$BOARD-build"
    STATUS=$?
    if [ "$STATUS" -eq 0 ]; then
        echo ""
        echo "=== Build succeeded ==="
        if [ "$BR_DOCKER_VOLUMES" = "1" ]; then
            echo "=== Extracting artifacts automatically ==="
            "$SCRIPT_DIR/extract-artifacts.sh"
        fi
    else
        echo ""
        echo "=== Build FAILED (exit $STATUS) -- not extracting artifacts ==="
    fi
    exit "$STATUS"
) > "$LOG_FILE" 2>&1 &
PID=$!
echo "Started, PID $PID"
echo "$PID" > /tmp/circuitsword-build.pid

echo ""
if [ "$BR_DOCKER_VOLUMES" = "1" ]; then
    echo "On success, the image will be extracted automatically to:"
    echo "  $REPO_ROOT/output/images"
    echo "No manual extract-artifacts.sh needed -- check \$LOG_FILE for progress/completion."
else
    echo "Image will land at:"
    echo "  $OUTPUT_DIR/$BOARD/images/batocera/images/$BOARD/batocera-$BOARD-*.img.gz"
fi
