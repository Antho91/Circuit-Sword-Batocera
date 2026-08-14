# Daemon Settings Surface — Design

Fourth and final sub-project of Phase 6. Exposes the hardware daemon's
four existing tunables (fan on/off thresholds, fan poll interval, switch
debounce) through a new "Daemon Settings" submenu in the existing
`circuitsword-quickmenu` overlay, with live reload (no restart needed)
and improved logging (both background daemon logs and a live status
readout in the UI).

## Context

`rpi-circuitsword.py`'s `load_config()` reads
`/userdata/system/configs/circuitsword.conf` (a simple `key=value` file)
into a `dict`, currently called ONCE at the start of `fan_thread()` and
`switch_monitor()` — each thread caches its own local copy for its whole
lifetime. There is no live-reload today: changing the file requires a
daemon restart to take effect. This is the only tunable surface in the
project; nothing else in this daemon is configurable.

Confirmed by reading `run_quickmenu_session()`'s docstring: **the
overlay already launches from ES itself** (MODE pressed with no game
running), not just in-game — "The overlay always launches -- from ES
with no game running, or in-game -- regardless of whether RetroArch
answers." This means no new launch mechanism is needed to make a
settings screen reachable outside a running game; adding a new item to
the existing menu is sufficient.

Building a genuinely native EmulationStation settings screen was
considered and explicitly declined this session: `batocera-emulationstation`
is a C++ project built from an external upstream git repo
(`package/batocera/emulationstation/batocera-emulationstation.mk`), and
this project has never patched ES's own UI source (only static config
files like `es_input.cfg`) — doing so would mean standing up a whole new
source-patch pipeline for a first-of-its-kind change. Reusing the
already-working `circuitsword-quickmenu` overlay avoids that entirely.

Low-battery warning/auto-shutdown thresholds were considered for
inclusion (since this is a natural place to expose them) and explicitly
declined again — this reopens and re-confirms the same decision made
earlier this session (`docs/superpowers/specs/2026-07-28-batocera-port-design.md`,
Phase 6 entry, "DECLINED 2026-08-11"): Batocera's own generic
`batocera-battery-checker` already handles visual low-battery warnings,
and no custom low-battery behavior exists in this daemon to expose
thresholds for. Adding config keys with no consumer would be a dead
setting — out of scope.

## Architecture

```
circuitsword-quickmenu: new "Daemon Settings" main-menu item (5th item)
    |
    | on entry: GET_CONFIG over the existing joystick IPC socket
    | (/var/run/circuitsword-joystick.sock -- reused as-is, not renamed,
    | even though it now also carries non-joystick commands -- adding a
    | command to a proven channel beats standing up a second socket)
    v
rpi-circuitsword.py: joystick_ipc_thread's command dispatch gains two
new entries:
    GET_CONFIG    -> returns current fan_on_temp/fan_off_temp/
                      fan_poll_interval_s/switch_debounce_ms + live
                      fan_on state, one line, comma-separated
    RELOAD_CONFIG -> re-reads circuitsword.conf, replaces the shared
                      config object under a lock, logs each changed key
                      (old -> new), replies OK/ERR
    |
    v
Shared, hot-reloadable config: fan_thread/switch_monitor no longer cache
a local `cfg` snapshot once at thread start -- they read a shared
`_current_config` dict (behind a lock) on every poll tick instead. Pure
in-memory read, no added file I/O in the hot paths (switch_monitor polls
every 50ms) -- only RELOAD_CONFIG actually touches the file.
```

Editing flow: quickmenu writes the new value directly to
`/userdata/system/configs/circuitsword.conf` (same file, same
`key=value` format, a location quickmenu already has write access to —
matches how it already writes brightness/volume elsewhere), THEN sends
`RELOAD_CONFIG` so the daemon picks it up immediately. If the file write
fails, the value is not sent and the old config stays in effect.

## Components

### `rpi-circuitsword.py` (modified)

- **Modified**: `load_config()`'s callers — `fan_thread()` and
  `switch_monitor()` stop calling `load_config()` once at their own
  start; instead both read from a new module-level shared object.
- **New**: `_config_lock = threading.Lock()`, `_current_config =
  load_config()` (populated once at daemon startup, in `main()`, before
  threads start — same as today's effective initial value, just shared
  instead of duplicated per-thread).
- **New**: `get_current_config() -> dict` — returns a shallow copy of
  `_current_config` under `_config_lock` (threads call this every poll
  tick instead of holding their own stale copy).
- **New**: `reload_config() -> dict` — calls `load_config()` fresh,
  compares each key against the current `_current_config` value, logs
  one line per CHANGED key (`"[rpi-circuitsword] config reload: fan_on_temp
  58.0 -> 60.0"`), replaces `_current_config` under the lock, returns the
  new config (used by both the `RELOAD_CONFIG` socket handler and
  `main()`'s existing startup log line, which already prints the loaded
  config once).
- **New**: `GET_CONFIG` / `RELOAD_CONFIG` entries in the existing
  command-dispatch table inside `joystick_ipc_thread` (the socket/thread
  itself is unchanged — same accept loop, same per-connection handling,
  just two more recognized command strings). `GET_CONFIG` replies with a
  single comma-separated line:
  `"fan_on_temp=58.0,fan_off_temp=50.0,fan_poll_interval_s=3,switch_debounce_ms=800,fan_on=0\n"`
  (the trailing `fan_on=0/1` is live state, read from the same variable
  `fan_thread` already tracks internally — exposed via a new small shared
  flag, same lock, not persisted to the config file). `RELOAD_CONFIG`
  replies `OK` (always succeeds — `load_config()` already tolerates a
  missing/malformed file by falling back to defaults, so there is no
  failure mode to report here beyond what `load_config()` already
  handles silently, same as today).

### `circuitsword-quickmenu` (modified)

- New main-menu item, 5th entry (`QM_ITEM_DAEMON_SETTINGS`). The main
  screen's 4 existing rows (WiFi/Volume/Brightness/Joystick) are all
  icon-only, by this session's own explicit earlier decision to replace
  text with real vector icons — a plain-text 5th row would break that
  established convention. This adds one more small hand-drawn icon
  (`QM_ICON_SETTINGS`, a simple gear shape) through the exact same
  pipeline Joystick's icon used (`icons/src/*.svg` +
  `tools/convert-icons.py`), rather than special-casing this one row as
  text.
- New submenu screen (`QM_SCREEN_DAEMON_SETTINGS`), same text-list
  pattern as the Joystick submenu: 4 editable rows (fan_on_temp,
  fan_off_temp, fan_poll_interval_s, switch_debounce_ms) plus one
  read-only live-status row combining CPU temperature (read directly
  from `/sys/class/thermal/thermal_zone0/temp`, same sysfs path the
  daemon's own `read_cpu_temp_c()` uses — quickmenu reads it directly,
  no daemon round trip needed for this one value, matching how it
  already reads brightness/battery sysfs directly elsewhere) and live
  fan on/off state (from `GET_CONFIG`'s reply). Up/Down selects a row;
  Left/Right adjusts the selected editable value by a fixed step (0.5°C
  for the two temperature fields, 1s for the poll interval, 50ms for the
  debounce, each clamped to a sane range); A commits the currently
  displayed values to the config file and sends `RELOAD_CONFIG`; B
  returns to the main screen. Status row refreshes on every submenu
  entry and after every successful commit.

## Data Flow

Same shape as the Joystick submenu: quickmenu never talks to the serial
port, only to the daemon's socket (for `GET_CONFIG`/`RELOAD_CONFIG`) and
directly to sysfs/the config file for everything else (matching the
existing `qm_settings.c` convention). The daemon's `_current_config`
becomes the single live source of truth both threads read from — no
more silently-stale per-thread snapshots.

## Error Handling

- Config-file write failure in quickmenu (permissions, disk full):
  abort the commit, leave the on-screen value as typed but don't send
  `RELOAD_CONFIG`, show a brief "Save failed" message — mirrors the
  Joystick submenu's existing failure-messaging gap being closed here
  rather than repeated (see Testing note below).
- Socket errors (`GET_CONFIG`/`RELOAD_CONFIG` unreachable): same
  defensive 0/-1-style return as every other `qm_joystick.c` function —
  never a crash, submenu just shows stale/default values and a "can't
  reach daemon" indicator.
- `reload_config()` cannot itself fail in a way that needs surfacing —
  `load_config()` already silently falls back to defaults on a
  missing/malformed file, same tolerance the daemon has always had.

## Testing

**Off-device**: `reload_config()`'s change-detection/logging logic
(comparing old vs new dict, which keys actually changed) is pure and
testable if extracted cleanly; the step-clamping math for each of the 4
editable fields is pure C, host-testable like the Joystick submenu's
render tests.

**Needs on-device validation (not yet done)**: does adjusting
`fan_on_temp` from the new screen actually change fan behavior within
one `fan_poll_interval_s` (3s) without a restart; does
`switch_debounce_ms` changing live actually affect the power-switch
poll loop's timing; whether the daemon's log output after a
`RELOAD_CONFIG` is genuinely useful for diagnosing "did my change take"
on a real device via `logread`.

## Out of Scope (this sub-project)

- Low-battery warning/auto-shutdown thresholds — explicitly declined
  again, no consumer exists for such a setting.
- A native EmulationStation C++ settings screen — explicitly declined,
  reuses the existing quickmenu overlay instead.
- Adding any NEW tunables beyond the 4 that already exist in
  `circuitsword.conf` today.
