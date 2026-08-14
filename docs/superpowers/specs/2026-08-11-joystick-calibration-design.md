# Joystick Calibration — Design

Third sub-project of Phase 6. Ports RetroPie's `cs-configure.py` joystick
calibration + invert/enable toggles to Batocera, as a new "Joystick"
submenu inside the existing `circuitsword-quickmenu` overlay (MODE
button), instead of the original's SSH-only interactive TTY tool.

## Context

The Arduino Leonardo firmware (`Retropie_source/kite-arduino/CS_FIRMWARE/`)
has its own ADC-based joystick calibration built in, driven entirely over
the same serial link `rpi-circuitsword.py` already owns exclusively for
battery/backlight/volume/mode-button-status:

- `J` — blocks the Arduino's main loop for `CALIBTIME` = 10 seconds
  (`config.h`) while sampling ADC min/max/mid on whichever joystick axes
  are physically present, auto-detects per-axis presence, persists the
  result to the Arduino's own EEPROM, then replies literal ASCII `"OK"`
  (2 bytes).
- `(` / `)` / `[` / `]` — toggle Joy1 X-invert / Joy1 Y-invert / Joy2
  X-invert / Joy2 Y-invert; each replies `"OK"` (2 bytes), persisted to
  EEPROM.
- `{` / `}` — toggle Joy1 enabled / Joy2 enabled (manual override of what
  calibration auto-detected); same reply/persistence pattern.
- `j` — read current joystick config as one status byte: bit0=iscalib1,
  bit1=iscalib2, bit2=xinvert1, bit3=yinvert1, bit4=xinvert2,
  bit5=yinvert2. Instant reply, no blocking.

Confirmed by reading the firmware's `calibrateJoystick()` (`INPUT.ino`):
while the Arduino is inside this 10-second window it does not process any
other serial command — there is no live ADC readout possible during
calibration. The UI can only show a countdown, not a live joystick-
position preview.

Confirmed by reading `circuitsword-quickmenu`'s existing code
(`qm_settings.c`): WiFi/Volume/Brightness never talk to the daemon or the
serial port at all — they go through sysfs or Batocera's own CLI tools
(`batocera-audio`, `batocera-settings-get/set`) directly. There is no
existing channel between `circuitsword-quickmenu` (the menu's C process)
and `rpi-circuitsword.py` (the Python daemon that exclusively owns
`/dev/ttyACM0`). This project introduces the first one.

Also confirmed: `rpi-circuitsword.py`'s existing `serial_cmd(cmd, nbytes,
retries=3, retry_delay_s=0.025)` helper is unsuitable for `J` as-is — its
per-attempt read timeout is 0.1s, and retrying a command that blocks the
Arduino for 10 seconds would either give up too early or re-send `J`
while a calibration is already running. A separate, long-timeout,
no-retry serial call is needed for `J` specifically.

## Scope decisions (confirmed with the user)

- Hardware has at least one analog joystick — this sub-project is
  applicable, not moot.
- Trigger/UI: a new item in the existing `circuitsword-quickmenu` overlay
  (not the firmware's own hold-START-at-boot calibration path, which
  stays as an independent, untouched fallback; not an SSH/CLI script).
- Scope includes the full `cs-configure.py` joystick feature set:
  calibration + all 4 invert toggles + both enable toggles (NOT the
  volume-pot/digital-rocker toggles from the same tool — those are
  unrelated to joysticks and out of scope).
- Structured as its own "Joystick" submenu (one new main-menu item,
  opening a 7-action list), not 7 items flattened into the existing
  WiFi/Volume/Brightness list.
- IPC between `circuitsword-quickmenu` and `rpi-circuitsword.py`: a Unix
  domain socket (chosen over a file/polling scheme or named pipes) — a
  clean one-shot request/response primitive with no FIFO
  open()-ordering pitfalls and no polling latency.

## Architecture

```
circuitsword-quickmenu (qm_joystick.c, new)
    |
    | connect() to /var/run/circuitsword-joystick.sock
    | write one line: "CALIBRATE\n" | "INVERT_J1X\n" | "INVERT_J1Y\n" |
    |                 "INVERT_J2X\n" | "INVERT_J2Y\n" | "TOGGLE_J1\n" |
    |                 "TOGGLE_J2\n" | "STATUS\n"
    | read one line reply, close connection
    v
rpi-circuitsword.py: joystick_ipc_thread (new, 9th daemon thread)
    |
    | AF_UNIX/SOCK_STREAM server, bind+listen on
    | /var/run/circuitsword-joystick.sock, accept loop
    |
    | on each connection: read one line, dispatch:
    |   CALIBRATE   -> joystick_calibrate()   (new: long-timeout, no-retry
    |                                          serial call for 'J', ~12s)
    |   INVERT_*/TOGGLE_* -> serial_cmd(single-byte cmd, nbytes=2)
    |                        (reused as-is, verifies the real "OK" reply
    |                        -- unlike the existing fire-and-forget
    |                        SET_BL/SET_VOL pattern, these ARE checked,
    |                        since the UI needs to report real success)
    |   STATUS      -> serial_cmd(b'j', nbytes=1), decode the 6 bits into
    |                  a "011000"-style 6-char reply string (same bit
    |                  order as the firmware's status byte)
    |
    | reply: "OK\n" | "ERR <reason>\n" | (for STATUS) "<6 bits>\n"
    v
Arduino Leonardo firmware (unchanged, already ships this protocol)
```

All serial I/O still goes through the daemon's existing `_serial_lock` /
`_get_serial()` — `joystick_ipc_thread` is just a new caller of that same
serialized access, same as every other thread. The acknowledged trade-off
this introduces: during a ~10s `CALIBRATE`, every other serial-using
thread (battery poll, backlight bridge, volume bridge, mode-button-status
poll) blocks behind `_serial_lock` for that whole window. This is a rare,
user-triggered, one-shot action — accepted, not treated as a bug.

## Components

### `rpi-circuitsword.py` (modified)

- **New**: `JOYSTICK_SOCK_PATH = "/var/run/circuitsword-joystick.sock"`.
- **New**: `joystick_calibrate() -> bool` — sends `b'J'`, then reads with
  a dedicated long timeout (~12s, NOT the shared `serial_cmd()` helper's
  0.1s/retry-3x behavior) waiting for the literal 2-byte `b"OK"`.
  Returns `True`/`False`; no retry on failure/timeout (a stuck-or-slow
  calibration must not be re-triggered automatically).
- **New**: `joystick_toggle(cmd: bytes) -> bool` — thin wrapper around
  `serial_cmd(cmd, 2)` (existing helper, existing 0.1s/retry-3x
  semantics are fine here since these are instant commands), returns
  whether the reply was `b"OK"`.
- **New**: `joystick_status() -> str` — `serial_cmd(b'j', 1)`, decodes
  the returned byte's low 6 bits (bit0..bit5, same order as the
  firmware) into a 6-character `"0"`/`"1"` string; returns `"000000"` if
  the read fails (matches this daemon's existing last-known-safe-default
  convention elsewhere, e.g. `read_battery_percent`'s `last_known`
  fallback).
- **New**: `joystick_ipc_thread(stop_event)` — binds and listens on
  `JOYSTICK_SOCK_PATH` (removing any stale socket file left over from an
  unclean shutdown before binding, matching how `main()` already
  registers a `SIGTERM`/`SIGINT` handler for clean shutdown elsewhere in
  this file), `settimeout(1.0)` on the listening socket so
  `stop_event.is_set()` stays checkable (matching every other thread's
  shutdown shape). Accept loop: on each connection, read one newline-
  terminated line, dispatch to `joystick_calibrate()` /
  `joystick_toggle(cmd)` / `joystick_status()` per the mapping table
  below, write one line back, close the connection.

  | Socket command | Serial command | Handler |
  |---|---|---|
  | `CALIBRATE` | `J` | `joystick_calibrate()` |
  | `INVERT_J1X` | `(` | `joystick_toggle(b'(')` |
  | `INVERT_J1Y` | `)` | `joystick_toggle(b')')` |
  | `INVERT_J2X` | `[` | `joystick_toggle(b'[')` |
  | `INVERT_J2Y` | `]` | `joystick_toggle(b']')` |
  | `TOGGLE_J1` | `{` | `joystick_toggle(b'{')` |
  | `TOGGLE_J2` | `}` | `joystick_toggle(b'}')` |
  | `STATUS` | `j` | `joystick_status()` |

  Unrecognized command lines get `"ERR unknown command\n"`.
- **Modified**: `main()`'s `threads = [...]` list — adds
  `threading.Thread(target=joystick_ipc_thread, args=(stop_event,),
  name="joystick", daemon=True)` as a 9th entry.

### `circuitsword-quickmenu/qm_joystick.c` (new)

- `int qm_joystick_calibrate_poll(int *conn_fd)` — one poll attempt of an
  in-progress `CALIBRATE` round trip. On the first call, pass `*conn_fd
  == -1`: it connects to the socket, sends `"CALIBRATE\n"`, stores the
  new fd in `*conn_fd`, and does a single short (~200ms) `recv()`
  attempt. Unlike the other calls below, it does NOT block a single
  `read()` for the full ~10-12s: the caller (the render loop in
  `quickmenu.c`, `qm_run_calibration()`) calls this repeatedly, once per
  countdown repaint, reusing the same `*conn_fd` each time, up to an
  overall ~13s hard cap (safety margin over the daemon's own ~12s
  timeout) before giving up and reporting failure. Returns 1 (success,
  `"OK"` seen, caller closes `*conn_fd`), 0 (failure/timeout/`ERR` seen,
  caller closes `*conn_fd` if it's `>= 0`), or -1 (still waiting, caller
  keeps polling with the same `*conn_fd`).
- `int qm_joystick_toggle(const char *cmd)` — connects, sends `cmd` (one
  of the `INVERT_*`/`TOGGLE_*` strings) + `"\n"`, reads one line with a
  short (~1s) timeout, returns 1/0 for `OK`/anything else. Used for all 6
  instant toggle actions.
- `int qm_joystick_status(char out[6])` — connects, sends `"STATUS\n"`,
  reads the 6-character bit string into `out`, returns 0 on success, -1
  on any failure (connection refused, timeout, malformed reply) — caller
  treats -1 as "unknown state", per the existing `qm_wifi_get()`-style
  `-1`-on-error convention already used throughout `qm_settings.c`.
- Socket path constant shared with the Python side only by value
  (`"/var/run/circuitsword-joystick.sock"`) — no shared header, matching
  how e.g. `QM_BACKLIGHT_DIR` in `qm_settings.c` already duplicates a
  path Python-side code also knows, rather than introducing a shared
  config mechanism for one string.

### `circuitsword-quickmenu/quickmenu.c` / `quickmenu.h` (modified)

- New `qm_screen` concept: `QM_SCREEN_MAIN` (existing WiFi/Volume/
  Brightness/Joystick list) and `QM_SCREEN_JOYSTICK` (the new 7-action
  submenu). `qm_state` gains `int screen;` (starts at `QM_SCREEN_MAIN`),
  `int joy_selected;` (0..6, mirrors the existing top-level `selected`
  field's role), and `char joy_status[6];` (cached bits from
  `qm_joystick_status()`, refreshed each time `QM_SCREEN_JOYSTICK` is
  entered).
- Main menu: `QM_ITEM_COUNT` grows from 3 to 4, new
  `QM_ITEM_JOYSTICK = 3`. Rendered the same row-based way as the other
  three items, with a new hand-drawn `QM_ICON_JOYSTICK` (baked through
  the same `tools/convert-icons.py` pipeline as the other 14 existing
  icons — same fixed `QM_ICON_SIZE` 24×24, same alpha-blend draw path,
  no new rendering primitive needed). `A`/`Right` on this item transitions
  `st.screen = QM_SCREEN_JOYSTICK`, resets `joy_selected = 0`, and calls
  `qm_joystick_status()` to populate `joy_status` before the first
  submenu render.
- Joystick submenu (`QM_SCREEN_JOYSTICK`): a plain text list using the
  existing bitmap font (`qm_font.c`, the same font already used for the
  hint line's `SELECT`/`BACK`/`ADJUST` labels) — not 7 new hand-drawn
  icons, which would be disproportionate asset work for a list of short
  English labels. Seven rows: `"Calibrate"`, `"Invert J1 X"`, `"Invert J1
  Y"`, `"Invert J2 X"`, `"Invert J2 Y"`, `"Joy 1 Enabled"`, `"Joy 2
  Enabled"`. The 6 toggle rows (all but Calibrate) show a trailing
  `[X]`/`[ ]` state indicator drawn from the corresponding bit in
  `st.joy_status`. `Up`/`Down` move `joy_selected`; `B` returns to
  `QM_SCREEN_MAIN` (does not close the whole overlay); `A` on
  "Calibrate" enters the calibration flow below; `A` on any toggle row
  calls `qm_joystick_toggle()` with the matching command string, and on
  success (1) re-reads `qm_joystick_status()` to refresh the row's
  indicator (covers both a real state flip and the rare case where the
  hardware's actual state didn't match what was expected).
- Calibration flow: on `A` over "Calibrate", `quickmenu.c` enters a
  dedicated render mode (no new `qm_screen` enum value needed — a local
  bool/flag inside the input-handling function is enough, since nothing
  else needs to know this transient state) that repaints a countdown
  message — `"Rotate all joysticks in a circular motion now."` /
  `"<N> seconds remaining"`, `N` computed locally from a
  `clock_gettime(CLOCK_MONOTONIC, ...)`-based start time, NOT from any
  data the daemon sends back (confirmed impossible — see Context) —
  while polling `qm_joystick_calibrate_poll()` once per repaint.
  Input is never read during this window (matches the original tool's
  own fully-blocking behavior; there is nothing meaningful to cancel
  back to mid-calibration since the Arduino is already committed to its
  own 10-second EEPROM-writing routine regardless of what the Linux side
  does) — any events that arrive queue in the kernel's evdev buffer
  rather than being discarded outright, so they are explicitly drained
  (read and thrown away) right before this flow returns, to stop a
  queued press from replaying into the main loop and silently
  re-triggering an action (e.g. a second calibration). On return
  (success or failure), shows a brief one-line result ("Calibration
  complete" / "Calibration failed") for ~1.5s, then refreshes
  `joy_status` and returns to the `QM_SCREEN_JOYSTICK` list.

## Data Flow

`circuitsword-quickmenu` never touches `/dev/ttyACM0` — every joystick
action is a short-lived socket round-trip to `rpi-circuitsword.py`, which
remains the sole serial owner (same separation of concerns the rest of
this project already established: the daemon owns hardware, quickmenu
owns UI + OS-level settings it CAN reach directly). `joy_status` is
read-only cached UI state inside `quickmenu.c`, always refreshed from a
fresh `STATUS` query rather than locally predicted, so it can never drift
from the Arduino's real EEPROM-persisted config.

## Error Handling

- Socket connect failure in any `qm_joystick_*` call (daemon not running,
  socket file missing/stale) is treated as a normal failure return (0 /
  -1), never a crash — same defensive posture as every existing
  `qm_settings.c` getter.
- `joystick_calibrate()`'s ~12s daemon-side timeout and
  `qm_joystick_calibrate_poll()`'s ~13s client-side hard cap are deliberately
  offset (client waits slightly longer than the server can possibly take)
  so a real server-side timeout always produces a clean `ERR` reply
  instead of the client giving up first and leaving the daemon's
  in-flight serial call to finish into a socket nobody's reading from
  anymore — that stale write is harmless (the connection just gets
  dropped/reset) but the ordering avoids a race where it could happen on
  every single calibration.
- `joystick_ipc_thread` binding failure (e.g. permission issue, path
  doesn't exist) is logged once and does not crash the daemon — same
  degrade-gracefully posture as `charging_thread`'s edge-detection
  fallback from the previous sub-project.

## Testing

**Off-device**: the 6-bit-to-6-character status decode
(`joystick_status()`'s bit-unpacking) and the client-side countdown math
are pure logic, testable on the host without real hardware. Everything
else (socket accept loop, serial I/O, Wayland rendering) is verified via
Buildroot rebuild/reinstall + direct Docker-volume artifact inspection,
same pattern as every prior sub-project this phase.

**Needs on-device validation (not yet done)**: real calibration accuracy
against physically wired joystick(s); whether the 10-second client-side
countdown matches the firmware's actual `CALIBTIME` closely enough to
feel right; submenu navigation feel (entering/backing out, toggle
indicators updating); whether `_serial_lock` contention during a live
10s calibration causes any visible glitch elsewhere (e.g. the status
bar's battery/volume icons stalling for that window, which is expected
and acceptable but should be confirmed as not worse than expected).

## Out of Scope (this sub-project)

- The volume-pot / digital-volume-rocker toggles from `cs-configure.py`
  — unrelated to joysticks, not ported here.
- The firmware's own hold-START-at-boot calibration path — untouched,
  stays as an independent fallback.
- Any change to how the Arduino firmware itself calibrates or persists
  config (EEPROM) — this sub-project only adds a Linux-side way to
  trigger the firmware's existing, unmodified protocol.
- A live ADC/joystick-position preview during calibration — confirmed
  technically impossible (see Context), not a deferred feature.
