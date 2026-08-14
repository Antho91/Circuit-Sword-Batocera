# MODE+Up/Down Volume Combo: Findings

Implements `docs/superpowers/specs/2026-08-10-mode-hold-volume-combo-design.md`.
Adds volume to the existing MODE hardware combo, alongside the already-working
MODE+Left/Right brightness combo (confirmed on real hardware, entirely
firmware-side, no Linux involvement).

## What was built

- `rpi-circuitsword.py`: two new serial commands (`CMD_GET_VOL = b'e'`,
  `CMD_SET_VOL = b'E'`), a pure decision function `volume_bridge_tick()`,
  and a new `volume_bridge()` thread that polls both the Arduino's internal
  volume register and Batocera's system volume (`batocera-audio`) every
  second, mirroring whichever side changed into the other -- the same
  shape as the existing `backlight_bridge()`, but bidirectional, since
  volume (unlike backlight) can change from either the hardware combo or
  software UI (e.g. `circuitsword-quickmenu`'s volume row).
- Removed the dead `BRIGHTNESS_STEP = 10` constant, an unused leftover from
  an earlier, never-completed approach to this same class of feature.

## Verified off-device

- `tests/test_quickmenu_logic.py`'s new `TestVolumeBridgeTick`: board-changed-
  only, system-changed-only, both-changed-same-tick (board wins), neither-
  changed, and both-reads-failed -- five cases, all passing.
- Full existing test suite still passes after the change (module still
  imports cleanly, `main()`'s thread-list edit didn't break anything).
- `rpi-circuitsword.py` still parses as valid Python (`ast.parse`).
- Incremental Buildroot reinstall (`PKG=rpigpioswitch-reinstall`) succeeds;
  the installed artifact in the Docker output volume was inspected directly
  and confirmed to contain the new code (not just a clean build log).

## Needs on-device validation (not yet done)

- **Firmware `'e'`/`'E'` support is unconfirmed.** The project's serial-
  protocol-continuity assumption (Hard Rule #5 in the root `CLAUDE.md`) has
  held for every other command so far, but these two specific command bytes
  have not been individually verified against the currently-flashed Arduino
  firmware. If unsupported, `read_board_volume()` will permanently return
  -1 and the bridge silently no-ops on the hardware-combo side (software
  volume changes continue to work regardless) -- confirm by testing MODE+
  Up/Down after flashing and checking `logread | grep rpi-circuitsword` for
  the `"volume -> ...% (from MODE+Up/Down combo)"` log line.
- **Audible correctness**: does `batocera-audio setSystemVolume` actually
  change the audible output level on this board's real audio hardware.
- **Round-trip latency feel**: up to ~2s worst case (one poll interval each
  direction). Matches the source RetroPie implementation's identical
  polling cadence, but not yet felt on real hardware -- confirm it doesn't
  feel laggy in practice.
