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
