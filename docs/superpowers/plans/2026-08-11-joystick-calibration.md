# Joystick Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port RetroPie's `cs-configure.py` joystick calibration + invert/enable toggles to Batocera, as a new "Joystick" submenu inside `circuitsword-quickmenu`.

**Architecture:** A new Unix-domain-socket protocol (first-ever quickmenu↔daemon IPC in this project) lets `circuitsword-quickmenu` trigger Arduino serial commands it cannot issue directly (the daemon exclusively owns `/dev/ttyACM0`). The daemon gets a new `joystick_ipc_thread`; the quickmenu binary gets a new socket-client file and a new submenu screen with a calibration countdown flow.

**Tech Stack:** Python 3 (`socket`, `threading`), C (POSIX `<sys/socket.h>`/`<sys/un.h>`), the existing `qm_font.c`/`qm_icons.c`/`qm_wl.c` primitives.

## Global Constraints

- Firmware protocol is fixed/unchangeable: single-byte serial commands `J`/`(`/`)`/`[`/`]`/`{`/`}`/`j` over `/dev/ttyACM0`. `"OK"` (2 ASCII bytes) is the success reply for all but `j` (1 raw status byte) and `J` (also `"OK"`, after a ~10s delay).
- `circuitsword-quickmenu` must NEVER open `/dev/ttyACM0` directly — all serial access stays exclusively in `rpi-circuitsword.py`, reached only via the new Unix socket.
- Socket path, exact string, both sides: `/var/run/circuitsword-joystick.sock`.
- No live ADC/joystick-position preview during calibration — confirmed technically impossible (the Arduino's main loop is fully blocked for the whole 10s window). Don't attempt to build one.
- Per CLAUDE.md Hard Rule #7: `rpigpioswitch` (Task 1) is plain-copy — verify via `PKG=rpigpioswitch-reinstall` + direct Docker-volume inspection. `circuitsword-quickmenu` (Tasks 2-4) is a real compiled package — verify via `PKG=circuitsword-quickmenu-rebuild` + direct Docker-volume inspection of the rebuilt binary, never trust a clean build log alone.
- `-j2` max for any on-device builds.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (detached HEAD, currently at `fc062a5ce3`). After each task, regenerate the patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```

---

### Task 1: Daemon-side socket server

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`

**Interfaces:**
- Consumes: existing `_get_serial()`, `_serial_lock`, `serial_cmd(cmd, nbytes, retries, retry_delay_s)` (all already in the file).
- Produces: `joystick_calibrate() -> bool`, `joystick_toggle(cmd: bytes) -> bool`, `joystick_status() -> str` (6-char `"0"/"1"` string), `joystick_ipc_thread(stop_event)`. Task 3/4 (a different process, `circuitsword-quickmenu`) consume these only indirectly, through the socket protocol below — not as Python imports.

This file has no existing host-side test suite (confirm by checking `tests/` — none of the `test_*.c`/`.py` files cover `rpi-circuitsword.py`); verification for this task stays at the Buildroot-rebuild + Docker-volume-inspection level, matching every other function in this file, not a new host-test framework for one function.

- [ ] **Step 1: Read the file fresh**

Open `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` and confirm the current shape of `_get_serial()`, `_serial_lock`, `serial_cmd()`, and `main()`'s `threads = [...]` list (it should currently end with the `"joystick"`... no — currently ends with `"charging"`, `"backlight"`, `"volume"`, `"switch"`, `"quickmenu"`, `"statusbar"` per the prior sub-project's work) before editing — never edit blind from a description alone.

- [ ] **Step 2: Add the joystick socket path constant and the three serial-command wrapper functions**

Add near the top of the "In-game quick menu" section or in a new section just above `main()` (pick whichever placement keeps related code together, following this file's existing section-comment-block convention):

```python
# ============================================================
# Joystick calibration + invert/enable toggles (Phase 6).
#
# circuitsword-quickmenu never opens /dev/ttyACM0 directly -- this
# daemon remains the sole serial owner. It exposes joystick actions to
# the menu over a small Unix-domain-socket protocol instead.
#
# Firmware protocol (Retropie_source/kite-arduino/CS_FIRMWARE/):
#   J -> calibrateJoystick(), BLOCKS ~10s (CALIBTIME), then "OK"
#   ( ) [ ] -> invert Joy1 X / Joy1 Y / Joy2 X / Joy2 Y, each "OK"
#   { } -> toggle Joy1 enabled / Joy2 enabled, each "OK"
#   j -> 1-byte status: bit0=iscalib1, bit1=iscalib2, bit2=xinvert1,
#        bit3=yinvert1, bit4=xinvert2, bit5=yinvert2
# ============================================================
JOYSTICK_SOCK_PATH = "/var/run/circuitsword-joystick.sock"
JOYSTICK_CALIBRATE_TIMEOUT_S = 12.0  # CALIBTIME (10s) + margin
CMD_JOY_CALIBRATE = b'J'
CMD_JOY_INVERT_J1X = b'('
CMD_JOY_INVERT_J1Y = b')'
CMD_JOY_INVERT_J2X = b'['
CMD_JOY_INVERT_J2Y = b']'
CMD_JOY_TOGGLE_J1 = b'{'
CMD_JOY_TOGGLE_J2 = b'}'
CMD_JOY_STATUS = b'j'


def joystick_calibrate() -> bool:
    """Sends 'J' and waits up to JOYSTICK_CALIBRATE_TIMEOUT_S for the
    literal 2-byte "OK" reply. Does NOT use serial_cmd()'s retry/short-
    timeout logic -- that's wrong for a command that blocks the Arduino
    for ~10s: retrying would either give up too early or re-send J while
    a calibration is already running. Holds _serial_lock for the whole
    call, same lock every other serial user shares -- acknowledged
    tradeoff (see design doc), not a bug."""
    with _serial_lock:
        try:
            conn = _get_serial()
            conn.reset_input_buffer()
            conn.write(CMD_JOY_CALIBRATE)
            conn.timeout = JOYSTICK_CALIBRATE_TIMEOUT_S
            resp = conn.read(2)
            conn.timeout = 0.1  # restore the shared connection's normal timeout
            return resp == b"OK"
        except (serial.SerialException, OSError) as e:
            print(f"[rpi-circuitsword] joystick calibrate serial error: {e}", file=sys.stderr)
            return False


def joystick_toggle(cmd: bytes) -> bool:
    """Thin wrapper around the existing serial_cmd() helper, which
    already handles retry/timeout correctly for instant (non-blocking)
    commands."""
    resp = serial_cmd(cmd, 2)
    return resp == b"OK"


def joystick_status() -> str:
    """Reads the firmware's 1-byte joystick config and decodes its low
    6 bits (bit0=iscalib1 .. bit5=yinvert2) into a 6-character "0"/"1"
    string in that same order. Returns "000000" on read failure."""
    resp = serial_cmd(CMD_JOY_STATUS, 1)
    if resp is None:
        return "000000"
    byte = resp[0]
    return "".join("1" if (byte >> bit) & 1 else "0" for bit in range(6))


JOYSTICK_COMMANDS = {
    "CALIBRATE": lambda: joystick_calibrate(),
    "INVERT_J1X": lambda: joystick_toggle(CMD_JOY_INVERT_J1X),
    "INVERT_J1Y": lambda: joystick_toggle(CMD_JOY_INVERT_J1Y),
    "INVERT_J2X": lambda: joystick_toggle(CMD_JOY_INVERT_J2X),
    "INVERT_J2Y": lambda: joystick_toggle(CMD_JOY_INVERT_J2Y),
    "TOGGLE_J1": lambda: joystick_toggle(CMD_JOY_TOGGLE_J1),
    "TOGGLE_J2": lambda: joystick_toggle(CMD_JOY_TOGGLE_J2),
}


def joystick_ipc_thread(stop_event: threading.Event):
    import socket

    try:
        os.remove(JOYSTICK_SOCK_PATH)
    except OSError:
        pass  # didn't exist, or a real permission problem -- bind() below will surface that

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        srv.bind(JOYSTICK_SOCK_PATH)
    except OSError as e:
        print(f"[rpi-circuitsword] joystick socket bind failed, joystick "
              f"menu will be unavailable: {e}", file=sys.stderr)
        return
    srv.listen(1)
    srv.settimeout(1.0)  # keeps stop_event checkable, matches every other thread's shutdown shape

    while not stop_event.is_set():
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        try:
            with conn:
                conn.settimeout(15.0)  # generous: covers a CALIBRATE round trip
                data = b""
                while not data.endswith(b"\n"):
                    chunk = conn.recv(64)
                    if not chunk:
                        break
                    data += chunk
                command = data.decode("ascii", errors="replace").strip()

                if command == "STATUS":
                    reply = joystick_status()
                elif command in JOYSTICK_COMMANDS:
                    ok = JOYSTICK_COMMANDS[command]()
                    reply = "OK" if ok else "ERR failed"
                else:
                    reply = "ERR unknown command"

                conn.sendall((reply + "\n").encode("ascii"))
        except OSError as e:
            print(f"[rpi-circuitsword] joystick connection error: {e}", file=sys.stderr)

    srv.close()
    try:
        os.remove(JOYSTICK_SOCK_PATH)
    except OSError:
        pass
```

- [ ] **Step 3: Register the new thread in `main()`**

In the `threads = [...]` list, add one entry (position doesn't matter functionally — append after the existing `"statusbar"` entry to match this file's history of appending new threads at the end):

```python
        threading.Thread(target=joystick_ipc_thread, args=(stop_event,), name="joystick", daemon=True),
```

- [ ] **Step 4: Host syntax check**

Run: `python3 -m py_compile "package/batocera/utils/rpigpioswitch/rpi-circuitsword.py"`
Expected: no output, exit code 0.

- [ ] **Step 5: Buildroot reinstall**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=rpigpioswitch-reinstall
' 2>&1 | tail -40
```

- [ ] **Step 6: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  grep -c "joystick_ipc_thread\|joystick_calibrate\|JOYSTICK_SOCK_PATH" \
  /bcm2837/target/usr/bin/rpi-circuitsword.py
```

Expected: nonzero.

- [ ] **Step 7: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "$(cat <<'EOF'
rpi-circuitsword: add joystick calibration/invert/enable socket server

Exposes the Arduino firmware's J/(/)/[/]/{/} joystick commands to
circuitsword-quickmenu over a new Unix-domain socket
(/var/run/circuitsword-joystick.sock), since the daemon remains the sole
owner of /dev/ttyACM0. Calibration (J) gets its own long-timeout,
no-retry serial call -- it blocks the Arduino for ~10s, which the
existing serial_cmd() helper's retry logic is wrong for.
EOF
)"
```

- [ ] **Step 8: Regenerate patch capture**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 2: New joystick icon asset

**Files:**
- Create: `package/batocera/utils/circuitsword-quickmenu/icons/src/joystick.svg`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `tools/convert-icons.py`
- Modify (generated): `package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c`

**Interfaces:**
- Produces: `QM_ICON_JOYSTICK` (new `qm_icon_kind` enum member) — Task 4 draws it via the existing `qm_draw_icon_rgba(fb, x, y, QM_ICON_JOYSTICK, color)`.

- [ ] **Step 1: Read `icons/src/volume.svg` fresh to confirm the style convention**

Confirm it's a simple hand-authored SVG with a `viewBox="0 0 16 16"` and plain `fill`/`stroke` attributes (no Inkscape metadata) — this is the style to match, not the more verbose `wifi.svg` (which was converted from Batocera's own upstream art and keeps its original metadata).

- [ ] **Step 2: Create the new icon SVG**

Create `package/batocera/utils/circuitsword-quickmenu/icons/src/joystick.svg`:

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <circle cx="8" cy="12" r="3.2" fill="#ffffff"/>
  <path d="M8 12 V4" stroke="#ffffff" stroke-width="1.8" stroke-linecap="round"/>
  <circle cx="8" cy="4" r="2.1" fill="#ffffff"/>
</svg>
```

- [ ] **Step 3: Add the enum member to `quickmenu.h`**

Read the file fresh, find the `qm_icon_kind` enum (currently: `QM_ICON_BATTERY_EMPTY, QM_ICON_BATTERY_25, QM_ICON_BATTERY_50, QM_ICON_BATTERY_75, QM_ICON_BATTERY_FULL, QM_ICON_BATTERY_CHARGING, QM_ICON_WIFI, QM_ICON_VOLUME, QM_ICON_BRIGHTNESS, QM_ICON_TITLE, QM_ICON_BADGE_A, QM_ICON_BADGE_B, QM_ICON_BADGE_LEFT, QM_ICON_BADGE_RIGHT, QM_ICON_COUNT`), and insert `QM_ICON_JOYSTICK` right after `QM_ICON_BRIGHTNESS` and before `QM_ICON_TITLE`:

```c
typedef enum {
    QM_ICON_BATTERY_EMPTY,
    QM_ICON_BATTERY_25,
    QM_ICON_BATTERY_50,
    QM_ICON_BATTERY_75,
    QM_ICON_BATTERY_FULL,
    QM_ICON_BATTERY_CHARGING,
    QM_ICON_WIFI,
    QM_ICON_VOLUME,
    QM_ICON_BRIGHTNESS,
    QM_ICON_JOYSTICK,
    QM_ICON_TITLE,
    QM_ICON_BADGE_A,
    QM_ICON_BADGE_B,
    QM_ICON_BADGE_LEFT,
    QM_ICON_BADGE_RIGHT,
    QM_ICON_COUNT
} qm_icon_kind;
```

- [ ] **Step 4: Add the matching entry to `tools/convert-icons.py`**

Read the file fresh, find the `ICONS` list, insert at the same relative position (after the `QM_ICON_BRIGHTNESS` entry, before `QM_ICON_TITLE`):

```python
    ("QM_ICON_JOYSTICK", "joystick.svg", ICON_SIZE, ICON_SIZE),
```

- [ ] **Step 5: Regenerate `qm_icon_data.c`**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
pip install cairosvg pillow 2>&1 | tail -5   # only if not already available
python3 tools/convert-icons.py
```

Expected: the script exits 0 and `package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c` is modified (new `QM_ICON_JOYSTICK` entry present).

- [ ] **Step 6: Verify the new asset actually rasterized (not silently blank)**

```bash
grep -A3 "QM_ICON_JOYSTICK" package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c | head -10
```

Confirm the alpha byte array is not all-zero (spot-check a few values — a fully blank/all-zero array would mean CairoSVG silently failed to rasterize anything, the same class of bug the real-overlay-icons plan hit earlier this session with the badge SVGs).

- [ ] **Step 7: Run the host C test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass, including `test_qm_icons.c`'s "all icons draw at least one non-background pixel" loop (which iterates `QM_ICON_COUNT` and picks up the new icon automatically).

- [ ] **Step 8: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -40
```

- [ ] **Step 9: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/icons/src/joystick.svg \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c \
        tools/convert-icons.py
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add QM_ICON_JOYSTICK icon asset

New hand-drawn joystick icon (base + stick + ball, matching volume.svg's
simple style), baked through the existing tools/convert-icons.py
pipeline. Used by the upcoming Joystick submenu item.
EOF
)"
```

- [ ] **Step 10: Regenerate patch capture** (same command as Task 1 Step 8)

---

### Task 3: quickmenu-side socket client

**Files:**
- Create: `package/batocera/utils/circuitsword-quickmenu/qm_joystick.c`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`
- Test: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_joystick.c` (new)

**Interfaces:**
- Consumes: the socket protocol Task 1 defined (`CALIBRATE`/`INVERT_*`/`TOGGLE_*`/`STATUS` commands, `OK`/`ERR <reason>`/6-bit-string replies, all newline-terminated).
- Produces: `int qm_joystick_calibrate_poll(int *conn_fd)`, `int qm_joystick_toggle(const char *cmd)`, `int qm_joystick_status(char out[6])` — Task 4 calls all three directly.

- [ ] **Step 1: Add declarations to `quickmenu.h`**

Read the file fresh. Add a new section after the existing `qm_settings.c` section (after the `qm_battery_get` declaration, before the `qm_wl.c` section comment):

```c
/* ------------------------------------------------------------------ */
/* qm_joystick.c -- Unix-domain-socket client to rpi-circuitsword.py's */
/* joystick_ipc_thread. circuitsword-quickmenu never touches           */
/* /dev/ttyACM0 directly -- the daemon remains the sole serial owner.  */
/* ------------------------------------------------------------------ */
#define QM_JOYSTICK_SOCK_PATH "/var/run/circuitsword-joystick.sock"

/* One poll attempt of an in-progress CALIBRATE round trip. On the
 * first call, pass *conn_fd == -1: this function opens the connection,
 * sends "CALIBRATE\n", stores the new fd in *conn_fd, and does one
 * short (~200ms) recv() attempt. On subsequent calls (with the same
 * *conn_fd still set), it does another short recv() attempt on the
 * existing connection -- no new connect/send. Returns:
 *    1  = final reply "OK" was read (caller must close(*conn_fd))
 *    0  = final reply "ERR ..." was read, or a connection error
 *         occurred (caller must close(*conn_fd) if >= 0)
 *   -1  = still waiting, no reply yet this poll (caller keeps polling,
 *         *conn_fd stays open and valid) */
int qm_joystick_calibrate_poll(int *conn_fd);

/* Connects, sends `cmd` + "\n" (one of "INVERT_J1X"/"INVERT_J1Y"/
 * "INVERT_J2X"/"INVERT_J2Y"/"TOGGLE_J1"/"TOGGLE_J2"), reads one line
 * with a ~1s timeout, closes the connection. Returns 1 on "OK", 0 on
 * any failure (connection error, timeout, "ERR" reply). */
int qm_joystick_toggle(const char *cmd);

/* Connects, sends "STATUS\n", reads the 6-character bit-string reply
 * into out[0..5] (NOT null-terminated -- out must be at least 6 bytes),
 * closes the connection. Returns 0 on success, -1 on any failure
 * (connection refused, timeout, malformed reply -- caller treats -1 as
 * "unknown state", matching qm_wifi_get()'s -1-on-error convention). */
int qm_joystick_status(char out[6]);
```

- [ ] **Step 2: Write `qm_joystick.c`**

Create `package/batocera/utils/circuitsword-quickmenu/qm_joystick.c`:

```c
/* Unix-domain-socket client to rpi-circuitsword.py's joystick_ipc_thread.
 * See quickmenu.h for the function contracts. circuitsword-quickmenu
 * never opens /dev/ttyACM0 directly -- every joystick action is a short
 * socket round trip to the daemon, which remains the sole serial owner. */
#include "quickmenu.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static int qm_joystick_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, QM_JOYSTICK_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int qm_joystick_set_timeout(int fd, long ms)
{
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* Reads until '\n' or the socket times out/errors/closes. Always
 * NUL-terminates within buf (size >= 1). Returns the number of bytes
 * read into buf (excluding the NUL), or -1 on error/timeout with
 * nothing usable read. */
static int qm_joystick_read_line(int fd, char *buf, size_t bufsize)
{
    size_t n = 0;
    while (n + 1 < bufsize) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) {
            if (n == 0)
                return -1;
            break;
        }
        if (c == '\n')
            break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (int)n;
}

int qm_joystick_calibrate_poll(int *conn_fd)
{
    if (*conn_fd < 0) {
        int fd = qm_joystick_connect();
        if (fd < 0)
            return 0;
        if (qm_joystick_set_timeout(fd, 200) != 0) {
            close(fd);
            return 0;
        }
        static const char cmd[] = "CALIBRATE\n";
        if (send(fd, cmd, sizeof(cmd) - 1, 0) != (ssize_t)(sizeof(cmd) - 1)) {
            close(fd);
            return 0;
        }
        *conn_fd = fd;
    }

    char line[64];
    int n = qm_joystick_read_line(*conn_fd, line, sizeof(line));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;   /* still waiting */
        return 0;        /* real error */
    }
    return strcmp(line, "OK") == 0 ? 1 : 0;
}

int qm_joystick_toggle(const char *cmd)
{
    int fd = qm_joystick_connect();
    if (fd < 0)
        return 0;
    if (qm_joystick_set_timeout(fd, 1000) != 0) {
        close(fd);
        return 0;
    }

    char msg[32];
    int len = snprintf(msg, sizeof(msg), "%s\n", cmd);
    if (len <= 0 || send(fd, msg, (size_t)len, 0) != len) {
        close(fd);
        return 0;
    }

    char line[64];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n < 0)
        return 0;
    return strcmp(line, "OK") == 0 ? 1 : 0;
}

int qm_joystick_status(char out[6])
{
    int fd = qm_joystick_connect();
    if (fd < 0)
        return -1;
    if (qm_joystick_set_timeout(fd, 1000) != 0) {
        close(fd);
        return -1;
    }

    static const char cmd[] = "STATUS\n";
    if (send(fd, cmd, sizeof(cmd) - 1, 0) != (ssize_t)(sizeof(cmd) - 1)) {
        close(fd);
        return -1;
    }

    char line[64];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n != 6)
        return -1;
    memcpy(out, line, 6);
    return 0;
}
```

- [ ] **Step 3: Add `qm_joystick.c` to the Buildroot source list**

In `package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`, add it to `CIRCUITSWORD_QUICKMENU_SRCS` (read the file fresh — current list is quickmenu.c, qm_font.c, qm_icon_data.c, qm_icons.c, qm_input.c, qm_settings.c, qm_wl.c):

```
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_joystick.c \
```

(Insert alphabetically between `qm_input.c` and `qm_settings.c`, matching the list's existing alphabetical order.)

- [ ] **Step 4: Write a host-side test against a throwaway mock listener**

This project's existing `tests/test_*.c` files have no networking precedent, but Unix-domain sockets are simple enough to test directly on the host without inventing new infrastructure — the test starts its own tiny listener on a TEST-only socket path (never the real `/var/run/...` path), scripted to send back fixed strings.

Create `/Users/bas/Circuit-Sword Batocera/tests/test_qm_joystick.c`:

```c
/* Host-side tests for qm_joystick.c's socket client, against a throwaway
 * mock Unix-socket listener on a TEST-only path (never the real
 * /var/run/circuitsword-joystick.sock). Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "quickmenu.h"

#define TEST_SOCK_PATH "/tmp/qm-joystick-test.sock"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

/* Forks a one-shot mock server: accepts exactly one connection, reads
 * one line, ignores it, writes `reply`, closes. Returns the child pid;
 * caller must waitpid() after the client-side call completes. */
static pid_t spawn_mock_server(const char *sock_path, const char *reply)
{
    unlink(sock_path);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    bind(srv, (struct sockaddr *)&addr, sizeof(addr));
    listen(srv, 1);

    pid_t pid = fork();
    if (pid == 0) {
        int conn = accept(srv, NULL, NULL);
        char buf[64];
        recv(conn, buf, sizeof(buf), 0);   /* drain the request, ignore it */
        send(conn, reply, strlen(reply), 0);
        close(conn);
        close(srv);
        _exit(0);
    }
    close(srv);   /* parent doesn't need the listening socket */
    return pid;
}

int main(void)
{
    /* This test file talks to a TEST_SOCK_PATH mock server, not the
     * real QM_JOYSTICK_SOCK_PATH -- qm_joystick.c's functions always
     * connect to the real path, so these tests exercise the same
     * connect/send/recv logic by having the mock server listen there
     * temporarily is not an option (would collide with a real daemon).
     * Instead, verify the low-level line-reading/timeout behavior via
     * a minimal reimplementation check: connect to our own mock server
     * directly to prove the mock harness itself works, then rely on
     * qm_joystick_toggle()/qm_joystick_status() error paths (no
     * listener at all) for the parts that don't require redirecting
     * QM_JOYSTICK_SOCK_PATH. */

    printf("qm_joystick_toggle with no listener\n");
    {
        unlink(QM_JOYSTICK_SOCK_PATH); /* ensure nothing is there */
        int rc = qm_joystick_toggle("INVERT_J1X");
        check(rc == 0, "no listener -> returns 0, does not crash/hang");
    }

    printf("qm_joystick_status with no listener\n");
    {
        char out[6];
        int rc = qm_joystick_status(out);
        check(rc == -1, "no listener -> returns -1, does not crash/hang");
    }

    printf("qm_joystick_calibrate_poll with no listener\n");
    {
        int conn_fd = -1;
        int rc = qm_joystick_calibrate_poll(&conn_fd);
        check(rc == 0, "no listener -> returns 0 (connect failure), not -1");
    }

    printf("mock server round trip (proves the harness itself works)\n");
    {
        pid_t pid = spawn_mock_server(TEST_SOCK_PATH, "OK\n");
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, TEST_SOCK_PATH, sizeof(addr.sun_path) - 1);
        int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        check(rc == 0, "mock server accepts a connection");
        send(fd, "PING\n", 5, 0);
        char buf[16];
        memset(buf, 0, sizeof(buf));
        recv(fd, buf, sizeof(buf) - 1, 0);
        check(strcmp(buf, "OK\n") == 0, "mock server replies as scripted");
        close(fd);
        int status;
        waitpid(pid, &status, 0);
        unlink(TEST_SOCK_PATH);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

Note for the implementer: `qm_joystick_toggle`/`qm_joystick_status`/`qm_joystick_calibrate_poll` always connect to the REAL `QM_JOYSTICK_SOCK_PATH`, not a redirectable path — there's no dependency-injection seam in this small client, matching this codebase's existing preference for minimal indirection. This means the "no listener" tests above are the practical ceiling for host-testing the real client functions (they verify the defensive error-return paths, which is the part most worth protecting against regressions); the actual success-path parsing logic is exercised indirectly by the "mock server round trip" test proving the same connect/send/recv primitives behave as expected, and is otherwise verified on-device once Task 1's real daemon exists (Task 4's Buildroot rebuild + manual on-device test). If, while implementing, a cleaner way to point the client at a test path emerges (e.g. an environment-variable override with a documented default), that's a reasonable in-task improvement — but don't over-engineer a seam for its own sake if the tests above already give useful coverage.

- [ ] **Step 5: Add the test to `tests/run-c-tests.sh`**

Read the file fresh, add a compile+run block for `test_qm_joystick.c` following the exact pattern already used for `test_qm_settings.c` or similar (compiling against `qm_joystick.c` plus whatever else it needs from `quickmenu.h`'s declarations — check whether the existing pattern needs `qm_font.c`/`qm_icon_data.c` linked in too, or if `qm_joystick.c` is self-contained enough not to need them).

- [ ] **Step 6: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass, including the new `test_qm_joystick` tests.

- [ ] **Step 7: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -40
```

- [ ] **Step 8: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c "circuitsword-joystick.sock\|CALIBRATE"
```

Expected: nonzero.

- [ ] **Step 9: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_joystick.c \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add qm_joystick.c socket client

Talks to rpi-circuitsword.py's new joystick_ipc_thread over
/var/run/circuitsword-joystick.sock -- the daemon remains the sole
owner of /dev/ttyACM0, quickmenu never opens it directly.
EOF
)"
```

Also copy the new test file into the batocera.linux repo commit? No — per this project's established convention, `/Users/bas/Circuit-Sword Batocera/tests/` is NOT inside the batocera.linux git repo (it's a separate, uncommitted directory in the main project folder). Only the `.c`/`.h`/`.mk` files under `package/batocera/...` get committed to batocera.linux; `tests/test_qm_joystick.c` and the `run-c-tests.sh` edit stay in the main project directory, uncommitted (no git repo there), matching how every prior sub-project's host tests have been handled this session.

- [ ] **Step 10: Regenerate patch capture** (same command as Task 1 Step 8)

---

### Task 4: quickmenu screen-state UI (submenu + calibration flow)

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c`
- Test: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` (extend existing `qm_render()` coverage)

**Interfaces:**
- Consumes: `QM_ICON_JOYSTICK` (Task 2), `qm_joystick_calibrate_poll()`/`qm_joystick_toggle()`/`qm_joystick_status()` (Task 3).
- Produces: nothing further downstream — this is the last task in the plan.

- [ ] **Step 1: Read `quickmenu.h` and `quickmenu.c` fresh**

Confirm their current exact content before editing (both files have been touched by every sub-project this phase; line numbers drift).

- [ ] **Step 2: Extend `quickmenu.h`'s item/screen model**

In the `quickmenu.c — menu model + rendering` section, replace:

```c
#define QM_ITEM_WIFI       0
#define QM_ITEM_VOLUME     1
#define QM_ITEM_BRIGHTNESS 2
#define QM_ITEM_COUNT      3

typedef struct {
    int selected;      /* 0 .. QM_ITEM_COUNT-1 */
    int wifi_on;       /* 0 or 1 */
    int volume;        /* 0..100 */
    int brightness;    /* 0..100 */
} qm_state;
```

with:

```c
#define QM_ITEM_WIFI       0
#define QM_ITEM_VOLUME     1
#define QM_ITEM_BRIGHTNESS 2
#define QM_ITEM_JOYSTICK   3
#define QM_ITEM_COUNT      4

#define QM_SCREEN_MAIN     0
#define QM_SCREEN_JOYSTICK 1

#define QM_JOY_CALIBRATE      0
#define QM_JOY_INVERT_J1X     1
#define QM_JOY_INVERT_J1Y     2
#define QM_JOY_INVERT_J2X     3
#define QM_JOY_INVERT_J2Y     4
#define QM_JOY_TOGGLE_J1      5
#define QM_JOY_TOGGLE_J2      6
#define QM_JOY_COUNT          7

typedef struct {
    int selected;      /* 0 .. QM_ITEM_COUNT-1, main screen only */
    int wifi_on;       /* 0 or 1 */
    int volume;        /* 0..100 */
    int brightness;    /* 0..100 */
    int screen;        /* QM_SCREEN_MAIN or QM_SCREEN_JOYSTICK */
    int joy_selected;  /* 0 .. QM_JOY_COUNT-1, joystick screen only */
    char joy_status[6]; /* cached STATUS reply: index0=iscalib1,
                            1=iscalib2, 2=xinvert1, 3=yinvert1,
                            4=xinvert2, 5=yinvert2 -- refreshed on
                            submenu entry and after any successful
                            toggle */
} qm_state;
```

- [ ] **Step 3: Initialize the new state fields in `main()`**

Find the existing state-init block in `quickmenu.c`'s `main()`:

```c
    qm_state st;
    st.selected = QM_ITEM_WIFI;
    st.wifi_on = qm_wifi_get();
    if (st.wifi_on < 0) st.wifi_on = 0;
    st.volume = qm_volume_get();
    if (st.volume < 0) st.volume = 0;
    st.brightness = qm_brightness_get();
    if (st.brightness < 0) st.brightness = 0;
```

Add immediately after:

```c
    st.screen = QM_SCREEN_MAIN;
    st.joy_selected = 0;
    memcpy(st.joy_status, "000000", 6);
```

(Add `#include <string.h>` at the top of `quickmenu.c` if it isn't already included — check first, `qm_settings.c`/other files in this package already use it, but `quickmenu.c` itself may not yet.)

- [ ] **Step 4: Extend `qm_render()` with the joystick submenu branch**

Read the current ~90-line body of `qm_render()` fresh. Wrap the EXISTING main-screen rendering logic (background fill through the hint-line drawing) in `if (st->screen == QM_SCREEN_MAIN) { ... }`, add one new `else { /* QM_SCREEN_JOYSTICK */ ... }` branch, and add the new 4th main-menu item inside the existing item-loop's `if/else if` chain. Concretely:

1. In the main-screen item loop, extend the existing chain:
```c
        if (item == QM_ITEM_WIFI) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_WIFI,
                               st->wifi_on ? QM_COLOR_FG : QM_COLOR_DIM);
        } else if (item == QM_ITEM_VOLUME) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_VOLUME, fg);
            qm_draw_bar(fb, bar_x, y + (QM_ICON_SIZE - bar_h) / 2, bar_w, bar_h,
                        st->volume);
        } else if (item == QM_ITEM_BRIGHTNESS) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_BRIGHTNESS, fg);
            qm_draw_bar(fb, bar_x, y + (QM_ICON_SIZE - bar_h) / 2, bar_w, bar_h,
                        st->brightness);
        } else {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_JOYSTICK, fg);
        }
```
   (Note: the existing code's final `else` branch currently handles brightness implicitly since it was the last of 3 items — it must become an explicit `else if (item == QM_ITEM_BRIGHTNESS)` now that there's a 4th item, with `QM_ITEM_JOYSTICK` taking the new final `else`.)

2. After the whole existing main-screen body (background fill, title, item loop, hint line), add the joystick-submenu else-branch. Use the SAME `scale`/`margin` variables already computed at the top of the function (don't recompute them):

```c
    } else {
        /* QM_SCREEN_JOYSTICK: plain text list, not icon-based --
         * 7 distinct short-English-label actions don't warrant 7 new
         * hand-drawn icons (see design doc). */
        static const char *labels[QM_JOY_COUNT] = {
            "Calibrate", "Invert J1 X", "Invert J1 Y", "Invert J2 X",
            "Invert J2 Y", "Joy 1 Enabled", "Joy 2 Enabled",
        };
        /* Row index -> bit index in st->joy_status, per the firmware's
         * status-byte order (design doc's Components section). -1 means
         * "no indicator" (the Calibrate row is an action, not a toggle). */
        static const int status_bit[QM_JOY_COUNT] = { -1, 2, 3, 4, 5, 0, 1 };

        qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

        int row_h = QM_GLYPH_H * scale + 2 * scale;
        int y = margin;
        for (int item = 0; item < QM_JOY_COUNT; item++) {
            if (item == st->joy_selected)
                qm_fill_rect(fb, margin / 2, y - scale,
                             (int)fb->width - margin, row_h, QM_COLOR_SEL_BG);
            uint32_t fg2 = (item == st->joy_selected) ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text(fb, margin, y, labels[item], scale, fg2);
            if (status_bit[item] >= 0) {
                const char *mark = (st->joy_status[status_bit[item]] == '1') ? "[X]" : "[ ]";
                int mark_x = (int)fb->width - margin - qm_text_width(mark, scale);
                qm_draw_text(fb, mark_x, y, mark, scale, fg2);
            }
            y += row_h;
        }
    }
```

- [ ] **Step 5: Restructure `main()`'s input-handling switch around `st.screen`**

Read the current `while (!qm_quit) { ... }` loop fresh. Replace the single `switch (ev) { ... }` block with a screen-scoped dispatch. The MAIN-screen case keeps all existing logic unchanged, plus one new case for entering the joystick submenu:

```c
        int dirty = 1;
        if (st.screen == QM_SCREEN_MAIN) {
            switch (ev) {
            case QM_EV_B:
                qm_quit = 1;
                dirty = 0;
                break;
            case QM_EV_UP:
                st.selected = (st.selected + QM_ITEM_COUNT - 1) % QM_ITEM_COUNT;
                break;
            case QM_EV_DOWN:
                st.selected = (st.selected + 1) % QM_ITEM_COUNT;
                break;
            case QM_EV_A:
                if (st.selected == QM_ITEM_WIFI) {
                    int want = st.wifi_on ? 0 : 1;
                    if (qm_wifi_set(want) == 0)
                        st.wifi_on = want;
                } else if (st.selected == QM_ITEM_JOYSTICK) {
                    st.screen = QM_SCREEN_JOYSTICK;
                    st.joy_selected = 0;
                    char status[6];
                    if (qm_joystick_status(status) == 0)
                        memcpy(st.joy_status, status, 6);
                } else {
                    dirty = 0;
                }
                break;
            case QM_EV_LEFT:
            case QM_EV_RIGHT: {
                if (st.selected == QM_ITEM_JOYSTICK) {
                    if (ev == QM_EV_RIGHT) {
                        st.screen = QM_SCREEN_JOYSTICK;
                        st.joy_selected = 0;
                        char status[6];
                        if (qm_joystick_status(status) == 0)
                            memcpy(st.joy_status, status, 6);
                    } else {
                        dirty = 0;
                    }
                    break;
                }
                int delta = (ev == QM_EV_RIGHT) ? QM_STEP : -QM_STEP;
                if (st.selected == QM_ITEM_WIFI) {
                    int want = st.wifi_on ? 0 : 1;
                    if (qm_wifi_set(want) == 0)
                        st.wifi_on = want;
                } else if (st.selected == QM_ITEM_VOLUME) {
                    int want = qm_clamp_pct(st.volume + delta);
                    if (qm_volume_set(want) == 0)
                        st.volume = want;
                } else {
                    int want = qm_clamp_pct(st.brightness + delta);
                    if (qm_brightness_set(want) == 0)
                        st.brightness = want;
                }
                break;
            }
            default:
                dirty = 0;
                break;
            }
        } else {
            /* QM_SCREEN_JOYSTICK */
            switch (ev) {
            case QM_EV_B:
                st.screen = QM_SCREEN_MAIN;
                break;
            case QM_EV_UP:
                st.joy_selected = (st.joy_selected + QM_JOY_COUNT - 1) % QM_JOY_COUNT;
                break;
            case QM_EV_DOWN:
                st.joy_selected = (st.joy_selected + 1) % QM_JOY_COUNT;
                break;
            case QM_EV_A:
                if (st.joy_selected == QM_JOY_CALIBRATE) {
                    qm_run_calibration(wl, input_fd, fb);
                    char status[6];
                    if (qm_joystick_status(status) == 0)
                        memcpy(st.joy_status, status, 6);
                } else {
                    static const char *cmds[QM_JOY_COUNT] = {
                        NULL, "INVERT_J1X", "INVERT_J1Y", "INVERT_J2X",
                        "INVERT_J2Y", "TOGGLE_J1", "TOGGLE_J2",
                    };
                    if (qm_joystick_toggle(cmds[st.joy_selected])) {
                        char status[6];
                        if (qm_joystick_status(status) == 0)
                            memcpy(st.joy_status, status, 6);
                    }
                }
                break;
            default:
                dirty = 0;
                break;
            }
        }
```

Note: `QM_EV_B` now means two different things depending on `st.screen` (quit the whole overlay on main, return to main from the submenu) — this is intentional and matches the design doc; the `if/else` on `st.screen` is what makes each branch's `QM_EV_B` case independent, not a double-handling bug.

- [ ] **Step 6: Add the `qm_run_calibration()` helper**

Add this static function in `quickmenu.c`, above `main()` (needs `#include <time.h>` added to the file's includes if not already present):

```c
/* Runs the ~10s calibration round trip: repaints a countdown between
 * short (~200ms) socket polls, ignores input the whole time (matches
 * the original cs-configure.py tool's fully-blocking behavior -- there
 * is nothing meaningful to cancel back to mid-calibration, since the
 * Arduino is already committed to its own EEPROM-writing routine
 * regardless of what the Linux side does). */
static void qm_run_calibration(qm_wl *wl, int input_fd, qm_fb *fb)
{
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int conn_fd = -1;
    int result = 0;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec)
                        + (now.tv_nsec - start.tv_nsec) / 1e9;

        int remaining = 10 - (int)elapsed;
        if (remaining < 0) remaining = 0;

        qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);
        int scale = (fb->width >= 640) ? 4 : 2;
        qm_draw_text(fb, 8 * scale, fb->height / 2 - 20 * scale,
                     "Rotate all joysticks in a", scale, QM_COLOR_FG);
        qm_draw_text(fb, 8 * scale, fb->height / 2 - 10 * scale,
                     "circular motion now.", scale, QM_COLOR_FG);
        char remaining_msg[32];
        snprintf(remaining_msg, sizeof(remaining_msg),
                 "%d seconds remaining", remaining);
        qm_draw_text(fb, 8 * scale, fb->height / 2 + 10 * scale,
                     remaining_msg, scale, QM_COLOR_DIM);
        qm_wl_present(wl);

        if (elapsed > 13.0) {
            result = 0;
            if (conn_fd >= 0) close(conn_fd);
            break;
        }

        int poll_result = qm_joystick_calibrate_poll(&conn_fd);
        if (poll_result >= 0) {
            result = poll_result;
            close(conn_fd);
            break;
        }

        /* Keep the compositor connection serviced; ignore any input
         * that arrives during calibration (nothing to act on). */
        qm_wl_pump(wl, input_fd, 200);
    }

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);
    int scale = (fb->width >= 640) ? 4 : 2;
    qm_draw_text(fb, 8 * scale, fb->height / 2,
                 result ? "Calibration complete" : "Calibration failed",
                 scale, QM_COLOR_FG);
    qm_wl_present(wl);
    usleep(1500000);
}
```

Add `#include <unistd.h>` too if not already present (for `usleep`/`close`).

- [ ] **Step 7: Extend the host test suite's `qm_render()` coverage**

Read `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` fresh. After its existing `"qm_render at 640x480"` block, add a new block exercising the joystick screen:

```c
    printf("qm_render joystick submenu\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .screen = QM_SCREEN_JOYSTICK, .joy_selected = 0 };
        memcpy(st.joy_status, "101010", 6);
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");

        st.joy_selected = QM_JOY_COUNT - 1;
        memcpy(st.joy_status, "000000", 6);
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG,
              "last row selected, zeroed status still paints bg");

        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "joystick submenu draws something");
        check(rightmost < (int)fb->width,
              "joystick submenu's rightmost pixel ([X]/[ ] indicators) "
              "stays within panel width");
        free_fb(fb);
    }

    printf("qm_render joystick submenu at 640x480\n");
    {
        qm_fb *fb = make_fb(640, 480);
        qm_state st = { .screen = QM_SCREEN_JOYSTICK, .joy_selected = 3 };
        memcpy(st.joy_status, "110011", 6);
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "640x480 joystick submenu stays within panel width");
        free_fb(fb);
    }
```

(`memcpy` needs `#include <string.h>` in the test file — check if it's already there.)

- [ ] **Step 8: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass. Note: `qm_run_calibration()`'s actual socket-polling behavior is NOT host-testable (real sockets, real timing, real Wayland pump) — only `qm_render()`'s pure drawing logic for the joystick screen is covered here, matching this plan's established off-device/on-device split.

- [ ] **Step 9: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -40
```

- [ ] **Step 10: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c "Calibrate\|Rotate all joysticks"
```

Expected: nonzero.

- [ ] **Step 11: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add Joystick submenu (calibrate + invert/enable)

New 4th main-menu item opens a 7-action submenu (calibrate, 4 invert
toggles, 2 enable toggles) driven entirely over the new
qm_joystick.c socket client. Calibration shows a client-side 10s
countdown -- the Arduino's main loop is fully blocked during
calibration, so no live joystick-position preview is possible.
EOF
)"
```

- [ ] **Step 12: Regenerate patch capture** (same command as Task 1 Step 8)

- [ ] **Step 13: Write the findings log**

Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE6-JOYSTICK-CALIBRATION-FINDINGS.md`:

```markdown
# Joystick Calibration: Findings

Implements `docs/superpowers/specs/2026-08-11-joystick-calibration-design.md`.
Adds a Unix-domain-socket IPC layer between circuitsword-quickmenu and
rpi-circuitsword.py (the first in this project) so the menu can trigger
Arduino joystick-calibration/invert/enable commands without ever opening
/dev/ttyACM0 itself.

## What was built

- `rpi-circuitsword.py`: `joystick_ipc_thread` (9th daemon thread), a
  Unix-domain socket server at `/var/run/circuitsword-joystick.sock`
  dispatching 8 commands (CALIBRATE, 4x INVERT_*, 2x TOGGLE_*, STATUS)
  to the Arduino firmware's existing J/(/)/[/]/{/}/j serial protocol.
  `joystick_calibrate()` uses its own ~12s no-retry serial call (the
  shared `serial_cmd()` helper's retry logic is wrong for a command
  that blocks the Arduino for ~10s).
- `circuitsword-quickmenu`: new `QM_ICON_JOYSTICK` (hand-drawn SVG,
  baked via the existing tools/convert-icons.py pipeline); new
  `qm_joystick.c` socket client (`qm_joystick_calibrate_poll`,
  `qm_joystick_toggle`, `qm_joystick_status`); new 4th main-menu item
  opening a text-based 7-action "Joystick" submenu (`QM_SCREEN_JOYSTICK`)
  with `[X]`/`[ ]` state indicators for the 6 toggle rows; a dedicated
  `qm_run_calibration()` countdown flow for the Calibrate action.

## Verified off-device

- `tests/test_qm_joystick.c`: `qm_joystick_toggle`/`qm_joystick_status`/
  `qm_joystick_calibrate_poll` all return their documented failure
  sentinels (not a crash or hang) when no listener is present; a mock
  Unix-socket server round trip confirms the underlying connect/send/recv
  primitives work as expected.
- `tests/test_qm_font.c`'s extended `qm_render()` coverage: the joystick
  submenu renders without crashing at both 320x240 and 640x480, paints
  the background, and stays within panel bounds, across several
  `joy_selected`/`joy_status` combinations.
- Both `PKG=rpigpioswitch-reinstall` and `PKG=circuitsword-quickmenu-rebuild`
  Buildroot builds succeed; direct Docker-volume inspection confirms the
  new symbols/strings are present in both installed artifacts.

## Needs on-device validation (not yet done)

- **Real calibration accuracy**: physically rotate the joystick(s)
  during the 10s window, confirm the Arduino's own auto-detected
  iscalib1/iscalib2 and resulting min/mid/max feel correct in actual
  gameplay afterward.
- **Countdown timing feel**: confirm the client-side 10s countdown
  roughly matches the firmware's actual CALIBTIME -- if the firmware's
  "OK" reply consistently arrives noticeably before/after the on-screen
  countdown hits 0, note the actual observed offset.
- **Submenu navigation feel**: entering/backing out of the Joystick
  submenu, toggle indicators updating correctly after each action.
- **Serial-lock contention during a live ~10s calibration**: confirm
  whether the status bar's battery/volume/brightness readouts visibly
  stall for that window (expected/acceptable per the design doc's
  explicit tradeoff) and whether it's worse than expected in practice.
- **First-ever quickmenu<->daemon IPC surface, needs extra scrutiny**:
  does the socket file get created with usable permissions for both
  processes (same user? check actual permissions on-device); does a
  stale socket file from an unclean daemon shutdown get cleaned up
  correctly on the next boot (the code removes it before bind(), but
  this hasn't been exercised via an actual unclean-shutdown-then-reboot
  cycle on real hardware).
```

- [ ] **Step 14: Self-review**

Read the final combined diff across all 4 tasks (`git diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- package/batocera/utils/rpigpioswitch package/batocera/utils/circuitsword-quickmenu tools/convert-icons.py`) end to end. Confirm: `QM_ITEM_COUNT`/`QM_JOY_COUNT` are used consistently everywhere they appear (no stale `3`/`QM_ITEM_BRIGHTNESS`-as-final-else left over from before Task 4); the socket path string is byte-identical in both `rpi-circuitsword.py` and `quickmenu.h`; no leftover references to the earlier `qm_joystick_calibrate(void)` sketch from the design doc (the final shipped signature is `qm_joystick_calibrate_poll(int *conn_fd)`).
