#!/bin/sh
# Phase 4 v2 spike: run ON THE DEVICE, over SSH, WHILE A GAME IS RUNNING.
#
# Question: does labwc composite a wlr-layer-shell OVERLAY surface above a
# running fullscreen RetroArch client? If yes, circuitsword-quickmenu can
# be a plain Wayland client and no display hand-off is needed at all.
#
# Uses /usr/bin/labnag, labwc's own reference layer-shell client, which is
# already on this image -- nothing is built or installed here.
set -u

echo "=== 1. is RetroArch running? (busybox-safe, no pgrep) ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in retroarch*) echo "RUNNING pid=${p#/proc/} comm=$c" ;; esac
done

echo "=== 2. RetroArch's Wayland environment ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in
        retroarch*)
            tr '\0' '\n' < "$p/environ" 2>/dev/null | grep -E '^(WAYLAND_DISPLAY|XDG_RUNTIME_DIR|XDG_SESSION_TYPE)=' ;;
    esac
done

echo "=== 3. which process holds each DRM node ==="
for p in /proc/[0-9]*; do
    for fd in "$p"/fd/*; do
        t=$(readlink "$fd" 2>/dev/null) || continue
        case "$t" in /dev/dri/*) echo "${p#/proc/} $(cat "$p/comm" 2>/dev/null) -> $t" ;; esac
    done
done

echo "=== 4. labnag present? ==="
ls -l /usr/bin/labnag || echo "labnag: NOT PRESENT -- spike cannot run"

echo "=== 5. overlay-layer surface for 15 seconds (WATCH THE SCREEN) ==="
echo "Expect a bar reading 'QUICKMENU OVERLAY SPIKE' on top of the game."
XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 \
    /usr/bin/labnag -y overlay -k none -e top -t 15 \
        -m "QUICKMENU OVERLAY SPIKE" 2>&1
echo "labnag exit rc=$?"

echo "=== 6. game still running afterwards? ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in retroarch*) echo "STILL RUNNING pid=${p#/proc/}" ;; esac
done
echo "=== spike done ==="
