# Daemon Settings Surface: Findings

Implements `docs/superpowers/specs/2026-08-11-daemon-settings-design.md`.
Adds live-reloadable config + a "Daemon Settings" submenu in
circuitsword-quickmenu for the daemon's 4 existing tunables, reusing the
joystick sub-project's Unix-domain socket (GET_CONFIG/RELOAD_CONFIG,
alongside the existing joystick commands).

## What was built

- `rpi-circuitsword.py`: config moved from per-thread load-once snapshots
  to a shared, lock-protected `_current_config` that `fan_thread`/
  `switch_monitor` read every poll tick; `reload_config()` logs each
  changed key; `GET_CONFIG`/`RELOAD_CONFIG` added to the existing socket
  dispatch.
- `circuitsword-quickmenu`: new `QM_ICON_SETTINGS` (hand-drawn gear,
  baked via the existing icon pipeline); `qm_joystick.c` gains
  `qm_daemon_config_get/reload/write`; new 5th main-menu item opening a
  `QM_SCREEN_DAEMON_SETTINGS` submenu with 4 editable rows + 1 live
  status row (current fan on/off state).

## Verified off-device

- Host C test suite covers the new render branch at both panel sizes
  (renders without crashing, stays within panel bounds).
- Both `PKG=rpigpioswitch-reinstall` and `PKG=circuitsword-quickmenu-rebuild`
  Buildroot builds succeed; Docker-volume inspection confirms the new
  symbols/strings are present in both installed artifacts.

## Needs on-device validation (not yet done)

- **Live reload actually working**: change `fan_on_temp` from the new
  screen, confirm the fan's real on/off behavior changes within one
  `fan_poll_interval_s` without a daemon restart.
- **`switch_debounce_ms` live change**: confirm the power-switch poll
  loop's timing actually reflects a changed value without restart.
- **Logging usefulness**: confirm `logread | grep rpi-circuitsword`
  after a `RELOAD_CONFIG` clearly shows which keys changed and to what
  value -- this was the user's explicit "so we know what works and
  what doesn't" ask for this sub-project.
- **Live fan-state status row accuracy**: confirm the "Fan is currently:
  ON/OFF" row matches the fan's real physical state, including right
  after a fresh submenu entry (not stale from a previous session).
- **Socket command coexistence**: confirm `GET_CONFIG`/`RELOAD_CONFIG`
  requests don't interfere with in-flight joystick commands on the same
  socket (both share one accept loop, one connection at a time,
  sequential by construction -- should be fine, but not yet exercised
  on real hardware with both submenus used in the same session).
