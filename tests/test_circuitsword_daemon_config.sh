#!/bin/sh
# Host test for circuitsword-daemon-config's get/set logic against a
# scratch config file. Does not require a running daemon for the
# get/set-file-rewrite behavior; the socket-ping half is tested
# separately with a stub listener.
set -e

SCRIPT="$(cd "$(dirname "$0")/.." && pwd)/batocera-build/build/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config"
# Fall back to a direct path if the relative layout above doesn't resolve
# (this test may be run from different working directories).
if [ ! -f "$SCRIPT" ]; then
    SCRIPT="/Users/bas/Circuit-Sword-Batocera/batocera-build/build/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config"
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

CONFIG_FILE="$TMPDIR/circuitsword.conf"
SOCK_PATH="$TMPDIR/circuitsword-joystick.sock"

failures=0
check() {
    if [ "$1" = "$2" ]; then
        echo "  ok   $3"
    else
        echo "  FAIL $3 (expected [$2], got [$1])"
        failures=$((failures + 1))
    fi
}

# --- get: key not present in a fresh/missing file AND no daemon socket
# to fall back to -> exit 1, no output ---
rm -f "$CONFIG_FILE"
if CONFIG_FILE="$CONFIG_FILE" SOCK_PATH="$SOCK_PATH" "$SCRIPT" get fan_on_temp 2>/dev/null; then
    check "exit 0" "exit 1" "get on missing file and no daemon fails"
else
    check "ok" "ok" "get on missing file and no daemon fails"
fi

# --- get: key not in the file, but the daemon's GET_CONFIG reply has it
# -> falls back to the daemon instead of failing (this is the fix for a
# fresh device showing no defaults in the CIRCUITSWORD menu until a
# setting is touched once) ---
python3 -c "
import socket, os
sock_path = '$SOCK_PATH'
if os.path.exists(sock_path):
    os.remove(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(5.0)
conn, _ = srv.accept()
conn.recv(64)
conn.sendall(b'fan_on_temp=58.0,fan_off_temp=50.0,fan_poll_interval_s=3,switch_debounce_ms=800,fan_on=0,fan_enabled=1\n')
conn.close()
" &
STUB_PID=$!
sleep 0.3

rm -f "$CONFIG_FILE"
got=$(CONFIG_FILE="$CONFIG_FILE" SOCK_PATH="$SOCK_PATH" "$SCRIPT" get fan_on_temp)
wait "$STUB_PID"
check "$got" "58.0" "get falls back to daemon's GET_CONFIG when the file has nothing"

# --- set: appends a new key on first write ---
# (socket ping will fail since no daemon is listening -- test set's file
# behavior by pre-seeding the file directly instead of relying on `set`,
# which requires a live socket to succeed end-to-end; see the separate
# stub-listener test below for the socket-ping path.)
echo "unrelated_key=untouched" > "$CONFIG_FILE"
printf 'fan_on_temp=55\n' >> "$CONFIG_FILE"

got=$(CONFIG_FILE="$CONFIG_FILE" "$SCRIPT" get fan_on_temp)
check "$got" "55" "get reads an existing key"

got=$(CONFIG_FILE="$CONFIG_FILE" "$SCRIPT" get unrelated_key)
check "$got" "untouched" "get does not disturb unrelated keys"

# --- set: writes the key AND pings RELOAD_CONFIG on the socket ---
python3 -c "
import socket, os
sock_path = '$SOCK_PATH'
if os.path.exists(sock_path):
    os.remove(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(5.0)
conn, _ = srv.accept()
data = conn.recv(64)
conn.sendall(b'OK\n')
conn.close()
with open('$TMPDIR/received.txt', 'wb') as f:
    f.write(data)
" &
STUB_PID=$!
sleep 0.3  # let the stub listener bind before `set` connects

echo "fan_off_temp=40" > "$CONFIG_FILE"
CONFIG_FILE="$CONFIG_FILE" SOCK_PATH="$SOCK_PATH" "$SCRIPT" set fan_off_temp 42
wait "$STUB_PID"

got=$(CONFIG_FILE="$CONFIG_FILE" "$SCRIPT" get fan_off_temp)
check "$got" "42" "set rewrites the target key"

received=$(cat "$TMPDIR/received.txt")
check "$received" "RELOAD_CONFIG" "set pings RELOAD_CONFIG on the socket"

# --- joystick-status: sends STATUS, returns the raw reply ---
python3 -c "
import socket, os
sock_path = '$SOCK_PATH'
if os.path.exists(sock_path):
    os.remove(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(5.0)
conn, _ = srv.accept()
data = conn.recv(64)
conn.sendall(b'001100\n')
conn.close()
with open('$TMPDIR/received.txt', 'wb') as f:
    f.write(data)
" &
STUB_PID=$!
sleep 0.3

status=$(SOCK_PATH="$SOCK_PATH" "$SCRIPT" joystick-status)
wait "$STUB_PID"
check "$status" "001100" "joystick-status returns the daemon's reply"

received=$(cat "$TMPDIR/received.txt")
check "$received" "STATUS" "joystick-status sends STATUS on the socket"

# --- joystick-cmd: rejects an unknown command without touching the socket ---
if SOCK_PATH="$TMPDIR/no-such.sock" "$SCRIPT" joystick-cmd BOGUS 2>/dev/null; then
    check "exit 0" "exit 2" "joystick-cmd rejects an unknown command"
else
    check "ok" "ok" "joystick-cmd rejects an unknown command"
fi

# --- joystick-cmd: sends the requested command, succeeds on "OK" ---
python3 -c "
import socket, os
sock_path = '$SOCK_PATH'
if os.path.exists(sock_path):
    os.remove(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(5.0)
conn, _ = srv.accept()
data = conn.recv(64)
conn.sendall(b'OK\n')
conn.close()
with open('$TMPDIR/received.txt', 'wb') as f:
    f.write(data)
" &
STUB_PID=$!
sleep 0.3

if SOCK_PATH="$SOCK_PATH" "$SCRIPT" joystick-cmd INVERT_J1X; then
    check "exit 0" "exit 0" "joystick-cmd succeeds on OK reply"
else
    check "exit 1" "exit 0" "joystick-cmd succeeds on OK reply"
fi
wait "$STUB_PID"

received=$(cat "$TMPDIR/received.txt")
check "$received" "INVERT_J1X" "joystick-cmd sends the exact requested command"

# --- joystick-cmd: fails when the daemon replies with an error ---
python3 -c "
import socket, os
sock_path = '$SOCK_PATH'
if os.path.exists(sock_path):
    os.remove(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(5.0)
conn, _ = srv.accept()
conn.recv(64)
conn.sendall(b'ERR failed\n')
conn.close()
" &
STUB_PID=$!
sleep 0.3

if SOCK_PATH="$SOCK_PATH" "$SCRIPT" joystick-cmd CALIBRATE 2>/dev/null; then
    check "exit 0" "exit 1" "joystick-cmd fails on ERR reply"
else
    check "ok" "ok" "joystick-cmd fails on ERR reply"
fi
wait "$STUB_PID"

echo ""
echo "$([ "$failures" -eq 0 ] && echo PASSED || echo FAILED) ($failures failures)"
[ "$failures" -eq 0 ]
