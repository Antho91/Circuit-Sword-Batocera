# Circuit-Sword MODE+Up/Down Volume Combo — Design

Adds volume control to the existing MODE-button hardware combo, alongside
the already-working MODE+Left/Right brightness combo. Confirmed on real
hardware by the user: holding MODE and pressing Left/Right already dims/
brightens the screen today, entirely through the Arduino Leonardo firmware
— no Linux code is involved in that path at all.

## Context

The on-board Arduino firmware (`Retropie_source/kite-arduino/CS_FIRMWARE/`,
carried over unchanged per the project's Hard Rule #5 on serial-protocol
continuity) implements MODE-held combos locally in `INPUT.ino`'s
`setModes()`: MODE+Left/Right adjusts brightness, MODE+Up/Down adjusts
volume, each maintained purely as internal Arduino state
(`cfg.bl_val`/`cfg.vol_val`).

Brightness needs nothing further because the Arduino drives the backlight's
PWM output directly in hardware — the combo is visible with zero Linux
involvement, matching what the user confirmed today.

Volume cannot work the same way: audio output on this hardware is a
digital/software path (ALSA), not something the Arduino can drive directly.
The old RetroPie build solved exactly this in
`Retropie_source/cs-hud_new/src/hardware.c`: a polling thread read the
Arduino's own volume register over serial (`CMD_GET_VOL`, command byte
`'e'`) and, on change, applied it via `amixer sset PCM %d%%` — the specific
ALSA card/control (`PCM`, card 1) was individually identified for that
build's USB sound chip (`Retropie_source/FUTURE.md`). It also wrote back
(`CMD_SET_VOL`, `'E'`) whenever software changed the volume through another
path, so the board's internal value never drifted out of sync and produced
a jarring "snap back" the next time the hardware combo was used.

This design ports that same bridge pattern to the Batocera daemon
(`rpi-circuitsword.py`), using Batocera's own `batocera-audio` abstraction
instead of hand-picking an ALSA mixer control (the same primitive
`circuitsword-quickmenu`'s existing volume row already uses via
`qm_volume_get`/`qm_volume_set` in `qm_settings.c`).

## Architecture

```
Arduino MODE+Up/Down combo (firmware-local, already exists)
  -> cfg.vol_val changes on the board, nothing sent over serial

rpi-circuitsword.py: new volume_bridge() thread, polling every
VOLUME_POLL_INTERVAL_S (1s, matches BACKLIGHT_POLL_INTERVAL_S):

  read board volume (CMD_GET_VOL 'e')      read system volume
       |                                    (batocera-audio getSystemVolume)
       v                                          v
  changed since last poll?                 changed since last poll?
       | yes                                      | yes
       v                                          v
  batocera-audio setSystemVolume <pct>     CMD_SET_VOL 'E' <pct> to Arduino
       |                                          |
       v                                          v
  update both "last known" trackers to prevent the other branch from
  re-detecting this same change as an independent, opposite-direction one
```

Both directions are needed, unlike backlight's one-directional bridge:
volume can change from two independent sources (the hardware combo, and
software UI such as `circuitsword-quickmenu`'s own volume row or Batocera's
own volume keys), and either one needs to reach the other so they never
show/apply stale values.

## Components

### `rpi-circuitsword.py` (extended)

New serial commands, alongside the existing `CMD_GET_VOLT`/`CMD_GET_BL`/
`CMD_SET_BL`/`CMD_GET_STATUS`:

```python
CMD_GET_VOL = b'e'   # -> 1 byte: board's internal volume %, 0-100
CMD_SET_VOL = b'E'   # <- 1 byte: volume % to store on the board
```

New pure decision function, matching the project's existing
host-testable-core convention (compare `statusbar_tick` from the persistent
status bar work):

```python
def volume_bridge_tick(last_board, last_system, board_now, system_now):
    """Decide what volume_bridge() should do this poll, given the
    previously-seen values and freshly-read current values.

    board_now / system_now are None when a read failed (treated as "no
    change this tick", matching read_backlight_sysfs()'s -1-on-failure ->
    "skip" convention in backlight_bridge()).

    Returns (new_last_board, new_last_system, action), where action is one
    of:
      None                    -- nothing changed, do nothing
      ("apply_to_alsa", pct)  -- board changed, push pct to batocera-audio
      ("apply_to_board", pct) -- system changed, push pct to the Arduino
    If both changed in the same tick, the board (hardware combo) wins --
    the physical button is the more recent, more intentional action; the
    system-side value is treated as already-stale and gets overwritten by
    the same "apply_to_alsa" outcome, avoiding a torn/ambiguous state.
    """
    board_changed = board_now is not None and board_now != last_board
    system_changed = system_now is not None and system_now != last_system

    if board_changed:
        return (board_now, board_now, ("apply_to_alsa", board_now))
    if system_changed:
        return (last_board, system_now, ("apply_to_board", system_now))
    return (last_board, last_system, None)
```

New thread, following `backlight_bridge()`'s exact shape:

```python
def read_board_volume() -> int:
    resp = serial_cmd(CMD_GET_VOL, 1)
    if resp is None:
        return -1
    v = resp[0]
    return v if 0 <= v <= 100 else -1


def read_system_volume() -> int:
    try:
        out = subprocess.run(
            ["/usr/bin/batocera-audio", "getSystemVolume"],
            capture_output=True, text=True, timeout=1,
        )
        return int(out.stdout.strip())
    except (subprocess.SubprocessError, ValueError, OSError):
        return -1


def set_arduino_volume(percent: int):
    percent = max(0, min(100, percent))
    serial_cmd(CMD_SET_VOL + bytes([percent]), 0)


def set_system_volume(percent: int):
    subprocess.run(
        ["/usr/bin/batocera-audio", "setSystemVolume", str(percent)],
        capture_output=True, timeout=1,
    )


VOLUME_POLL_INTERVAL_S = 1


def volume_bridge(stop_event: threading.Event):
    last_board = read_board_volume()
    last_system = read_system_volume()
    if last_board < 0:
        last_board = 50
    if last_system < 0:
        last_system = 50

    while not stop_event.is_set():
        board_now = read_board_volume()
        system_now = read_system_volume()
        board_arg = board_now if board_now >= 0 else None
        system_arg = system_now if system_now >= 0 else None

        last_board, last_system, action = volume_bridge_tick(
            last_board, last_system, board_arg, system_arg
        )
        if action is not None:
            kind, pct = action
            if kind == "apply_to_alsa":
                set_system_volume(pct)
                print(f"[rpi-circuitsword] volume -> {pct}% (from MODE+Up/Down combo)", file=sys.stderr)
            else:
                set_arduino_volume(pct)
                print(f"[rpi-circuitsword] volume -> {pct}% (from batocera-audio/quickmenu, synced to board)", file=sys.stderr)

        stop_event.wait(VOLUME_POLL_INTERVAL_S)
```

Registered in `main()`'s thread list alongside the existing
`fan_thread`/`battery_bridge`/`backlight_bridge`/`switch_monitor`/
`quickmenu_thread`/`statusbar_thread` threads.

### Cleanup (same file)

Remove the dead `BRIGHTNESS_STEP = 10  # % per button-combo press` constant
(`rpi-circuitsword.py:267`) — defined but never referenced anywhere in the
file, a leftover from an earlier, never-completed approach to this same
class of feature. Leaving it in place beside genuinely-used, newly-added
`VOLUME_POLL_INTERVAL_S` would misleadingly suggest it does something.

## Data Flow

MODE+Up/Down pressed on hardware → Arduino updates `cfg.vol_val` locally,
nothing sent over serial → next `volume_bridge()` poll (within 1s) reads
the changed value via `CMD_GET_VOL` → applies to ALSA via
`batocera-audio setSystemVolume` → both trackers updated.

Software volume changed (quickmenu's volume row, Batocera's own volume
keys) → next poll reads the changed system volume via
`batocera-audio getSystemVolume` → writes it back to the Arduino via
`CMD_SET_VOL` → both trackers updated, so a later hardware-combo press
starts from the correct baseline instead of jumping from a stale value.

## Error Handling

- **Serial read fails** (`CMD_GET_VOL` gets no response): `read_board_volume()`
  returns -1, `volume_bridge_tick` treats it as "no change this tick" (same
  convention as `read_backlight_sysfs()`'s existing -1-on-failure handling)
  — no crash, no spurious volume change, silently retried next poll.
- **`batocera-audio` call fails or times out**: caught, treated the same as
  a failed read — one missed tick, retried next poll. A 1s timeout on the
  subprocess call prevents a hung `batocera-audio` from blocking the whole
  thread indefinitely.
- **Firmware doesn't actually support `'e'`/`'E'`** (unconfirmed assumption,
  see Testing below): `read_board_volume()` permanently returns -1, the
  bridge never applies anything from the board side but also never crashes
  or logs spam beyond the existing per-change log line (which simply never
  fires) — the feature silently no-ops rather than breaking anything, and
  software-driven volume changes (quickmenu, Batocera's own controls)
  continue to work exactly as they do today regardless.

## Testing

**Verifiable off-device**: `volume_bridge_tick()`'s full decision matrix —
board-changed-only, system-changed-only, both-changed-same-tick (board
wins), neither-changed, and both-reads-failed (`None`/`None` inputs) — as a
new test class in `tests/test_quickmenu_logic.py`, following the exact
monkeypatching style already used for `TestStatusbarTick`.

**Needs on-device validation**: whether the currently-flashed Arduino
firmware actually answers `'e'`/`'E'` as expected (unconfirmed — the
project's serial-protocol-continuity assumption has held for every other
command so far, but has not been individually verified for these two);
audible correctness of `batocera-audio setSystemVolume` end to end on this
board's actual audio output; real-world round-trip latency feel (up to
~2s worst case: one poll interval for the board to notice the Arduino
changed, potentially another for the write-back) — acceptable for a
non-interactive ambient control, per the source RetroPie implementation's
identical polling cadence, but worth confirming it doesn't feel laggy in
practice.

## Out of Scope (this phase)

- Any on-screen volume feedback (OSD/toast) — matches the brightness
  combo's existing silent behavior; the status bar (if visible) already
  shows the current volume passively.
- Changing brightness's own combo path — already works entirely in
  firmware, untouched by this design.
- Renaming/restructuring the existing `CMD_GET_VOLT` (battery voltage)
  constant despite the naming proximity to the new `CMD_GET_VOL` (volume)
  — both names come directly from the existing/original protocol, kept as
  literal single-letter serial commands; not in scope to rename.
