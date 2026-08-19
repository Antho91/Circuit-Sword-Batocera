#!/bin/bash
# /buildkernel -- rebuild just the Linux kernel package (e.g. after
# editing board/batocera/broadcom/bcm2837/linux-defconfig.config or a
# kernel patch), then repackage the image. Much faster than a full
# build-image.sh run since every other package's ccache/stamps are
# untouched -- only the kernel recompiles and the final image gets
# regenerated around it.
#
# NOTE: this is also what build-wifi.sh calls, because on this board the
# RTL8723BS WiFi driver is an in-tree kernel driver
# (CONFIG_RTL8723BS=m in linux-defconfig.config), not a separate
# out-of-tree Buildroot package. If you only need to change a WiFi
# *runtime* setting (modprobe options, NetworkManager power-save), that
# lives in the fsoverlay/ files instead and does NOT need a kernel
# rebuild -- just run build-image.sh directly, it repackages fast.
set -euo pipefail
cd "$(dirname "$0")"
source ./env.sh
require_src
require_disk
require_docker
sync_batocera_mk
seed_docker_volumes

cd "$BATOCERA_SRC"

echo "Reconfiguring + rebuilding the kernel package ..."
make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 PKG=linux-reconfigure "$BOARD-pkg"
make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 PKG=linux-rebuild "$BOARD-pkg"

echo "Repackaging the image ..."
echo "Logging to $LOG_FILE"
nohup make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES="$BR_DOCKER_VOLUMES" O="$OUTPUT_DIR/$BOARD" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 "$BOARD-build" \
    > "$LOG_FILE" 2>&1 &
PID=$!
echo "Started, PID $PID"
echo "$PID" > /tmp/circuitsword-build.pid
