#!/bin/bash
# /buildwifi -- rebuild after a WiFi-related change.
#
# On this board the RTL8723BS driver is enabled via CONFIG_RTL8723BS=m
# in the kernel defconfig (an in-tree Linux driver, no separate
# out-of-tree Buildroot package involved) -- so a driver-level WiFi
# change is really a kernel change, and this script just delegates to
# build-kernel.sh.
#
# If you only changed a *runtime* WiFi setting -- module options in
# board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/r8723bs.conf,
# or NetworkManager power-save in
# .../fsoverlay/etc/NetworkManager/conf.d/circuitsword-wifi-powersave-off.conf
# -- you do NOT need this script. Those are plain files copied into the
# rootfs at repackage time; run build-image.sh directly and it'll pick
# them up without touching the kernel at all (much faster).
set -euo pipefail
cd "$(dirname "$0")"
echo "WiFi driver = in-tree kernel driver on this board -> rebuilding the kernel."
echo "(Runtime-only config change? Ctrl-C and run build-image.sh instead -- see comments in this script.)"
exec ./build-kernel.sh
