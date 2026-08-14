#!/bin/sh
# Phase 4 spike: run ON THE DEVICE, over SSH, while a game is running.
# Answers: is VT switching available, does it free DRM master, which card?
# Read-only apart from the VT switch itself, which is reverted at the end.
set -u

echo "=== 1. DRM nodes present ==="
ls -l /dev/dri/

echo "=== 2. is RetroArch running? (busybox-safe, no pgrep) ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in retroarch*) echo "RUNNING pid=${p#/proc/} comm=$c" ;; esac
done

echo "=== 3. which process holds each DRM node open ==="
for p in /proc/[0-9]*; do
    for fd in "$p"/fd/*; do
        t=$(readlink "$fd" 2>/dev/null) || continue
        case "$t" in /dev/dri/*) echo "${p#/proc/} $(cat "$p/comm" 2>/dev/null) -> $t" ;; esac
    done
done

echo "=== 4. current VT (before) ==="
fgconsole 2>/dev/null || echo "fgconsole: NOT AVAILABLE"

echo "=== 5. chvt availability ==="
command -v chvt || echo "chvt: NOT AVAILABLE"
command -v openvt || echo "openvt: NOT AVAILABLE"

echo "=== 6. python3 VT_ACTIVATE to tty6, then batocera-drminfo, then back ==="
python3 - <<'PYEOF'
import fcntl, struct, subprocess, sys, time
VT_GETSTATE, VT_ACTIVATE, VT_WAITACTIVE = 0x5603, 0x5606, 0x5607
try:
    f = open("/dev/tty0", "wb")
except OSError as e:
    print("open /dev/tty0 FAILED:", e); sys.exit(1)
buf = fcntl.ioctl(f, VT_GETSTATE, struct.pack("HHH", 0, 0, 0))
before = struct.unpack("HHH", buf)[0]
print("VT before =", before)
try:
    fcntl.ioctl(f, VT_ACTIVATE, 6)
    fcntl.ioctl(f, VT_WAITACTIVE, 6)
    print("VT_ACTIVATE(6) OK")
except OSError as e:
    print("VT_ACTIVATE(6) FAILED:", e); f.close(); sys.exit(1)
time.sleep(1)
r = subprocess.run(["/usr/bin/batocera-drminfo"], capture_output=True, text=True)
print("batocera-drminfo rc =", r.returncode)
print(r.stdout[:2000])
print(r.stderr[:2000])
try:
    fcntl.ioctl(f, VT_ACTIVATE, before)
    fcntl.ioctl(f, VT_WAITACTIVE, before)
    print("VT restored to", before)
except OSError as e:
    print("VT restore FAILED:", e)
f.close()
PYEOF

echo "=== 7. current VT (after) ==="
fgconsole 2>/dev/null || echo "fgconsole: NOT AVAILABLE"
echo "=== spike done ==="
