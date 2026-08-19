#!/bin/bash
# /buildimg -- full image build (kernel + all packages + WiFi driver +
# genimage). This is what actually produces a flashable
# batocera-bcm2837-*.img.gz. Slow on a cold cache (see BUILDING.md
# "Timing"); fast-ish on a warm ccache if only a few files changed.
#
# Runs in the background and logs to $LOG_FILE so a crashed terminal/SSH
# session doesn't kill a multi-hour build. Tail the log yourself, or use
# Claude Code's Monitor tool pointed at this script's log file.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh
require_src
require_disk
require_docker
sync_batocera_mk
seed_docker_volumes

cd "$BATOCERA_SRC"
echo "Logging to $LOG_FILE"
nohup make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 "$BOARD-build" \
    > "$LOG_FILE" 2>&1 &
PID=$!
echo "Started, PID $PID"
echo "$PID" > /tmp/circuitsword-build.pid

echo ""
if [ "$BR_DOCKER_VOLUMES" = "1" ]; then
    echo "Image will land inside the 'batocera-output-$BOARD' Docker named volume, at:"
    echo "  /images/batocera/images/$BOARD/batocera-$BOARD-*.img.gz"
    echo "Not Finder-browsable -- once the build finishes, run:"
    echo "  batocera-build/scripts/extract-artifacts.sh"
    echo "to copy it out to the host."
else
    echo "Image will land at:"
    echo "  $OUTPUT_DIR/$BOARD/images/batocera/images/$BOARD/batocera-$BOARD-*.img.gz"
fi
