#!/bin/bash
# /all -- one-shot entry point for a clean checkout: output-dir setup +
# image build, in order. Equivalent to running setup-disk-image.sh
# (only if BR_DOCKER_VOLUMES=0) and build-image.sh yourself -- this
# just chains them so a fresh clone can do the whole thing with one
# command, mirroring Retropie_source/build.sh's `all` target.
#
# Corrected 2026-08-22: no longer runs setup-build-tree.sh.
# batocera-build/build/batocera.linux is tracked directly in this repo
# now (a plain `git clone` already gives you the full buildable
# dev-tree) -- setup-build-tree.sh actively refuses to run against it
# (see its own header comment), so calling it here would just fail.
#
# Default (BR_DOCKER_VOLUMES=1): no disk image needed at all -- the build
# lives in Docker named volumes. See env.sh for why.
#
# The actual build (last step) still runs in the background and logs to
# $LOG_FILE -- this script returns once it's kicked off, it does not wait
# for the multi-hour build to finish. Tail the log yourself, or use
# Claude Code's Monitor tool pointed at $LOG_FILE.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh

if [ "$BR_DOCKER_VOLUMES" = "1" ]; then
    echo "=== [1/2] BR_DOCKER_VOLUMES=1 -- no disk image needed, build lives in Docker named volumes ==="
    mkdir -p "$OUTPUT_DIR" "$DL_DIR" "$CCACHE_DIR"
elif [[ "$(uname)" == "Darwin" ]]; then
    echo "=== [1/2] setup-disk-image.sh (macOS, BR_DOCKER_VOLUMES=0) ==="
    ./setup-disk-image.sh
else
    echo "=== [1/2] setup-disk-image.sh skipped (not macOS -- using $BATOCERA_BUILD_ROOT directly) ==="
    mkdir -p "$OUTPUT_DIR" "$DL_DIR" "$CCACHE_DIR"
fi

echo "=== [2/2] build-image.sh ==="
./build-image.sh

if [ "$BR_DOCKER_VOLUMES" = "1" ]; then
    echo ""
    echo "Once the build finishes, run batocera-build/scripts/extract-artifacts.sh"
    echo "to copy the image out of the Docker named volume onto the host."
fi
