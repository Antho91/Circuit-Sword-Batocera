# MODE+Up/Down Volume Combo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bridge the Arduino's existing MODE+Up/Down hardware volume combo into Batocera's software volume (and back), the same way `backlight_bridge()` already bridges brightness, using new `CMD_GET_VOL`/`CMD_SET_VOL` serial commands and `batocera-audio`.

**Architecture:** A new bidirectional poll loop, `volume_bridge()`, added to `rpi-circuitsword.py` alongside the existing `fan_thread`/`battery_bridge`/`backlight_bridge`/`switch_monitor`/`quickmenu_thread`/`statusbar_thread` threads. Each poll tick reads both the Arduino's internal volume register (`CMD_GET_VOL`) and Batocera's system volume (`batocera-audio getSystemVolume`); whichever changed since the last tick gets pushed to the other side. The decision of what to do each tick is a single pure function, `volume_bridge_tick()`, kept separate from I/O so it is host-testable without a real serial port or subprocess.

**Tech Stack:** Python 3 (existing daemon, `pyserial`, `subprocess`), `unittest` (host tests, no hardware).

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-10-mode-hold-volume-combo-design.md` — this plan implements it exactly; do not deviate from its architecture (bidirectional bridge, board wins on same-tick conflict, `batocera-audio` as the software-volume abstraction) or scope.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, detached HEAD at pinned commit `155c2d8d304cbb53db52e9479dcf683392821d5c`, tag `batocera-43.1`). Every task's file changes happen here and get real git commits in this repo.
- **`batocera-build/scripts/*.sh` (`build-image.sh`, `env.sh`) default `BATOCERA_SRC` to a stale, unpatched checkout** at `batocera-build/build/batocera.linux` inside the main project directory — NOT where this project's development happens. Any command in this plan that sources `env.sh` or invokes `make` in the build tree MUST explicitly `export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux` first. Do not use `batocera-build/scripts/build-image.sh` for this plan's verification steps — it triggers a multi-hour full image build, which this plan does not need.
- Per Hard Rule #7 in the project's root `CLAUDE.md` (Buildroot incremental-build staleness gotcha): `rpigpioswitch` is a plain-copy/no-compile package. A full image build will **not** pick up an edited-but-already-stamped source file in an already-built Docker output volume. Any verification step that needs the built artifact to reflect this change must force `PKG=rpigpioswitch-reinstall` (cheap — re-runs only `INSTALL_TARGET_CMDS`, no recompile), never rely on a bare full build.
- The main project directory (`/Users/bas/Circuit-Sword Batocera`) has **no git repo**, by deliberate choice. Never run `git init` there. Files under `tests/` and `docs/` in that directory are saved directly, not committed.
- After the build-tree change is committed (end of Task 1), regenerate the project's reproducible patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- No hardware in CI. Testing is off-device only: Python `unittest` for `volume_bridge_tick()`'s decision logic, plus an incremental `PKG=rpigpioswitch-reinstall` Buildroot check that the file installs cleanly. On-device validation (firmware `'e'`/`'E'` support, audible correctness, round-trip latency feel) is tracked in the findings log, never claimed as done by this plan.
- Findings log: `docs/superpowers/plans/findings/PHASE4-VOLUME-COMBO-FINDINGS.md` (created in Task 1).

---

## Task 1: `volume_bridge()` in `rpi-circuitsword.py` + tests + findings log

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-VOLUME-COMBO-FINDINGS.md`

**Interfaces:**
- Produces: `CMD_GET_VOL: bytes`, `CMD_SET_VOL: bytes`, `VOLUME_POLL_INTERVAL_S: int`,
  `volume_bridge_tick(last_board: int, last_system: int, board_now: int | None, system_now: int | None) -> tuple[int, int, tuple[str, int] | None]`
  (pure — no I/O), `read_board_volume() -> int` (-1 on failure), `read_system_volume() -> int`
  (-1 on failure), `set_arduino_volume(percent: int) -> None`, `set_system_volume(percent: int) -> None`,
  `volume_bridge(stop_event: threading.Event) -> None`. Nothing later in this plan consumes these
  (this is the only task), but they follow the exact names/signatures given in the design doc's
  Components section — use that code verbatim.

### Step 1: Add the two new serial commands

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` and find the existing `CMD_*` block (currently lines 38-41):

```python
CMD_GET_VOLT = b'c'    # -> 2 bytes: raw ADC voltage, low byte then high byte
CMD_GET_BL = b'q'      # -> 1 byte: brightness %
CMD_SET_BL = b'Q'      # <- 1 byte: brightness %
CMD_GET_STATUS = b's'  # -> 1 byte: status flags, bit 0 = mode button
```

Add two new lines immediately after `CMD_GET_STATUS`, so the block reads:

```python
CMD_GET_VOLT = b'c'    # -> 2 bytes: raw ADC voltage, low byte then high byte
CMD_GET_BL = b'q'      # -> 1 byte: brightness %
CMD_SET_BL = b'Q'      # <- 1 byte: brightness %
CMD_GET_STATUS = b's'  # -> 1 byte: status flags, bit 0 = mode button
CMD_GET_VOL = b'e'     # -> 1 byte: board's internal volume %, 0-100
CMD_SET_VOL = b'E'     # <- 1 byte: volume % to store on the board
```

### Step 2: Remove the dead `BRIGHTNESS_STEP` constant

- [ ] Find the line `BRIGHTNESS_STEP = 10  # % per button-combo press` (currently line 267, directly
  below the `BACKLIGHT_POLL_INTERVAL_S = 1` line in the "Backlight" section — confirm the exact
  line by searching for the text, since line numbers may have shifted). It is never referenced
  anywhere else in the file (verify with `grep -n BRIGHTNESS_STEP rpi-circuitsword.py` showing only
  this one line before deleting). Delete the line entirely.

### Step 3: Write the failing test for `volume_bridge_tick()`

- [ ] Read `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py` in full first, in
  particular the existing `TestStatusbarTick` class (its `setUp`/monkeypatching style is what this
  new class follows) and the `load_daemon()` helper at the top of the file (this is how the module
  under test — aliased as `cs` in existing tests — gets imported; the same `cs` alias is already
  set up once near the top of the file, e.g. `cs = load_daemon()` — reuse it, don't re-import).

  Add this new test class at the end of the file:

```python
class TestVolumeBridgeTick(unittest.TestCase):
    """volume_bridge_tick() is the single-decision core volume_bridge() loops
    on: mirror the Arduino's hardware-combo volume into ALSA, mirror ALSA's
    volume back into the Arduino when something else changed it, prefer the
    board on a same-tick conflict, and do nothing when neither changed or
    both reads failed."""

    def test_board_changed_only_applies_to_alsa(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 60, 50
        )
        self.assertEqual(last_board, 60)
        self.assertEqual(last_system, 60)
        self.assertEqual(action, ("apply_to_alsa", 60))

    def test_system_changed_only_applies_to_board(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 50, 70
        )
        self.assertEqual(last_board, 50)
        self.assertEqual(last_system, 70)
        self.assertEqual(action, ("apply_to_board", 70))

    def test_both_changed_same_tick_board_wins(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 80, 20
        )
        self.assertEqual(last_board, 80)
        self.assertEqual(last_system, 80)
        self.assertEqual(action, ("apply_to_alsa", 80))

    def test_neither_changed_does_nothing(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 50, 50
        )
        self.assertEqual(last_board, 50)
        self.assertEqual(last_system, 50)
        self.assertIsNone(action)

    def test_both_reads_failed_preserves_last_values(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, None, None
        )
        self.assertEqual(last_board, 50)
        self.assertEqual(last_system, 50)
        self.assertIsNone(action)
```

### Step 4: Run the test to verify it fails

- [ ] Run:
  ```bash
  BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux python3 "/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py" TestVolumeBridgeTick -v
  ```
  Expected: `AttributeError: module ... has no attribute 'volume_bridge_tick'` (or similar) — the
  function doesn't exist yet.

### Step 5: Implement `volume_bridge_tick()` and the I/O wrappers

- [ ] In `rpi-circuitsword.py`, find the "Backlight" section (the block starting with the comment
  `# Backlight: Batocera's native brightness UI writes ...`, currently ending around where
  `backlight_bridge()` is defined, just before the `# Power switch:` section). Add a new section
  immediately after `backlight_bridge()`'s closing line and before the `# Power switch:` comment
  block:

```python
# ============================================================
# Volume: the Arduino firmware's own MODE+Up/Down hardware combo adjusts
# a volume value it keeps internally (mirrors the already-working
# MODE+Left/Right brightness combo, which stays entirely in Arduino
# hardware and needs no bridge). Volume is different: audio output is a
# software/ALSA path the Arduino cannot drive directly, so this bridge
# polls both sides and mirrors whichever changed into the other -- same
# shape as backlight_bridge() above, but bidirectional, because volume
# (unlike backlight) can legitimately change from either side: the
# hardware combo, or software UI such as circuitsword-quickmenu's own
# volume row.
# ============================================================
VOLUME_POLL_INTERVAL_S = 1


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

- [ ] Confirm `subprocess` is already imported at the top of the file (it is, per the module's
  existing `import subprocess` — used elsewhere for `os.system`-style calls); no new import needed.

### Step 6: Run the test to verify it passes

- [ ] Run:
  ```bash
  BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux python3 "/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py" TestVolumeBridgeTick -v
  ```
  Expected: `OK`, 5 tests passing.

- [ ] Also run the full existing suite to confirm nothing else broke:
  ```bash
  BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux python3 "/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py" -v
  ```
  Expected: `OK`, all tests (existing + 5 new) passing.

### Step 7: Register `volume_bridge` in `main()`'s thread list

- [ ] Find the `threads = [...]` list in `main()` (currently lines 664-671):

```python
    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
        threading.Thread(target=statusbar_thread, args=(stop_event,), name="statusbar", daemon=True),
    ]
```

  Add `volume_bridge` as a new entry, immediately after `backlight`:

```python
    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=volume_bridge, args=(stop_event,), name="volume", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
        threading.Thread(target=statusbar_thread, args=(stop_event,), name="statusbar", daemon=True),
    ]
```

### Step 8: Verify the file still compiles/imports cleanly

- [ ] Run:
  ```bash
  python3 -c "
import ast
with open('/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py') as f:
    ast.parse(f.read())
print('OK: valid Python syntax')
"
  ```
  Expected: `OK: valid Python syntax`.

- [ ] Re-run the full test suite once more (Step 6's second command) to confirm the `main()` edit
  didn't break module import (the test file imports the whole module by path, so a syntax error
  anywhere in it fails every test, not just new ones).

### Step 9: Incremental Buildroot check (no full image build)

- [ ] Run, using this project's established incremental single-package pattern (`env.sh` sets
  `OUTPUT_DIR`/`DL_DIR`; `env.sh` itself requires `bash`, not `zsh`, hence the explicit `bash -c`):
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=rpigpioswitch-reinstall 2>&1 | tail -40
  '
  ```
  `rpigpioswitch` is a plain-copy/no-compile package, so `-reinstall` (re-runs only
  `INSTALL_TARGET_CMDS`) is the right target — cheaper than `-rebuild` and sufficient here, per
  Hard Rule #7's guidance for this package type.

  Expected: the reinstall step completes without error. Then verify the installed file inside the
  Docker output volume actually contains the new code (per Hard Rule #7's "verify via direct
  Docker-volume inspection, not just a clean build log" requirement):
  ```bash
  docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine grep -c "CMD_GET_VOL = b'e'" \
    /bcm2837/target/usr/bin/rpi-circuitsword.py
  ```
  Expected output: `1`.

### Step 10: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
  git commit -m "rpi-circuitsword: bridge MODE+Up/Down volume combo to batocera-audio"
  ```

### Step 11: Regenerate the patch capture

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- [ ] Verify the new commands are present in the regenerated patch:
  ```bash
  grep -c "CMD_GET_VOL = b'e'" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  grep -c "def volume_bridge_tick" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
  Expected: both `1`.

### Step 12: Write the findings log

- [ ] Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-VOLUME-COMBO-FINDINGS.md`:

```markdown
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
```

**Commit this file too** (it lives in the main project directory, which has
no git repo, so it is saved directly — no `git add`/`git commit` needed for
it, unlike the `rpi-circuitsword.py` change above).

---
