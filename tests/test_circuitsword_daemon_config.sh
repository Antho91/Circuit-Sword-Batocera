#!/bin/sh
# Host test for circuitsword-daemon-config's get/set logic against a
# scratch config file. Does not require a running daemon for the
# get/set-file-rewrite behavior; the socket-ping half is tested
# separately with a stub listener.
set -e

SCRIPT="$(cd "$(dirname "$0")/.." && pwd)/../batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config"
# Fall back to a direct path if the relative layout above doesn't resolve
# (this test may be run from different working directories).
if [ ! -f "$SCRIPT" ]; then
    SCRIPT="/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config"
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

# --- get: key not present in a fresh/missing file -> exit 1, no output ---
rm -f "$CONFIG_FILE"
if CONFIG_FILE="$CONFIG_FILE" SOCK_PATH="$SOCK_PATH" "$SCRIPT" get fan_on_temp 2>/dev/null; then
    check "exit 0" "exit 1" "get on missing file fails"
else
    check "ok" "ok" "get on missing file fails"
fi

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

echo ""
echo "$([ "$failures" -eq 0 ] && echo PASSED || echo FAILED) ($failures failures)"
[ "$failures" -eq 0 ]
