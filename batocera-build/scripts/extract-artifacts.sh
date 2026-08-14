#!/bin/bash
# Copy the built image(s) out of the 'batocera-output-$BOARD' Docker
# named volume onto the host. Only needed when BR_DOCKER_VOLUMES=1
# (the default) -- named volumes live entirely inside Docker Desktop's
# Linux VM and aren't Finder-browsable, unlike the old host-bind-mount
# path where images landed directly under $OUTPUT_DIR.
#
# Usage: extract-artifacts.sh [destination-dir]
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh
require_docker

if [ "$BR_DOCKER_VOLUMES" != "1" ]; then
    echo "BR_DOCKER_VOLUMES is not 1 -- images already landed directly at:"
    echo "  $OUTPUT_DIR/$BOARD/images/batocera/images/$BOARD/"
    exit 0
fi

DEST="${1:-$REPO_ROOT/output/images}"
mkdir -p "$DEST"

VOL="batocera-output-$BOARD"
if ! docker volume inspect "$VOL" >/dev/null 2>&1; then
    echo "ERROR: Docker volume '$VOL' does not exist. Has a build run yet?" >&2
    exit 1
fi

echo "Copying images/ out of Docker volume '$VOL' to $DEST ..."
docker run --rm \
    -v "$VOL":/t:ro \
    -v "$DEST":/out \
    "$DOCKER_IMAGE" \
    sh -c "cp -r /t/images/batocera/images/$BOARD/. /out/"

echo "Done:"
ls -la "$DEST"
