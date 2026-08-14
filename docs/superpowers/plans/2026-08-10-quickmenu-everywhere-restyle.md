# Quick Menu Always-On Trigger + Batocera-Style Palette — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Circuit-Sword's MODE button always open `circuitsword-quickmenu` (from EmulationStation as well as in-game), and restyle the overlay's palette to match Batocera's own black-background, dark-red-accent look.

**Architecture:** Two independent, mechanical changes to already-shipped Phase 4 code. (1) In `rpi-circuitsword.py`, `quickmenu_thread()` stops gating on `retroarch_running()` and always calls `run_quickmenu_session()`; inside that function, RetroArch's command-port reachability probe is decoupled from "launch the overlay" (now unconditional) and used only to decide "pause/resume", tracked via a local `paused_this_session` flag so a resume is never sent without a matching pause. (2) In `quickmenu.h`, six `QM_COLOR_*` macros change value — no logic, layout, or font changes.

**Tech Stack:** Python 3 (daemon, host-testable via `tests/test_quickmenu_logic.py`), C/gnu99 (`circuitsword-quickmenu`, host-testable via `tests/run-c-tests.sh`), Buildroot/`batocera.linux` package tree.

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-10-quickmenu-everywhere-restyle-design.md` — this plan implements it exactly; do not deviate from its color values or sequence.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, detached HEAD at pinned commit `155c2d8d304cbb53db52e9479dcf683392821d5c`, tag `batocera-43.1`). Every task's file changes happen here and get committed with real git commits in this repo (it's the one repo in this project that IS meant to be committed to — see `git log --oneline` for the existing Phase 4 commit style, e.g. `d36e93886a rpi-circuitsword: add the Phase 4 quick-menu thread`).
- The main project directory (`/Users/bas/Circuit-Sword Batocera`) has **no git repo**, by deliberate choice. Never run `git init` there. Files under `tests/` and `docs/` in that directory are saved directly, not committed.
- After all build-tree changes are committed, regenerate the project's reproducible patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
  This is the exact command used for every prior Phase 4 task (see `docs/superpowers/plans/2026-08-06-phase4-quickmenu-v2.md`) — same base commit, same exclusion of the `buildroot` submodule (unaffected by this work).
- No hardware in CI. Every task's testing steps are off-device (Python `unittest`, host `cc` compile via `tests/run-c-tests.sh`) — on-device validation is tracked in the findings log, not claimed as done by this plan.
- Findings log: `docs/superpowers/plans/findings/PHASE4-QUICKMENU-EVERYWHERE-RESTYLE-FINDINGS.md` (new file, created in Task 3) — follows the pattern of `PHASE4-QUICKMENU-FINDINGS.md`.

---

## Task 1: Daemon — always launch, decouple pause/resume, drop stale comment

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`
  - `run_quickmenu_session()` (currently lines 507-562)
  - `quickmenu_thread()` (currently lines 565-597)
  - stale comment block in the "Backlight" section header (currently lines 257-267) and inside `backlight_bridge()` (currently lines 315-324)
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`
  - Replace `TestSessionAbortsWhenUnreachable` (lines 175-200) — its assertion that `subprocess.Popen` must never be called when RetroArch is unreachable is the **old** contract; this task inverts it.
  - Add a new regression test class covering the reachable path (pause exactly once, resume exactly once).

**Interfaces:**
- Consumes: `retroarch_cmd_query(cmd: bytes, timeout_s: float = 0.5) -> bytes | None`, `send_pause_toggle() -> bool`, `ModeButton`, `QUICKMENU_BIN`, `QUICKMENU_ENV`, `QUICKMENU_WATCHDOG_S`, `QUICKMENU_POLL_INTERVAL_S`, `read_mode_button() -> bool` — all already defined above these functions in the same file, unchanged by this task.
- Produces: `run_quickmenu_session() -> None` (same signature, new internal behavior: always launches `circuitsword-quickmenu`, only pauses/resumes RetroArch when its command port answered at entry). `quickmenu_thread(stop_event: threading.Event) -> None` (same signature; no longer calls `retroarch_running()`).

- [ ] **Step 1: Update the now-outdated test to assert the new contract**

Open `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`. Replace the entire `TestSessionAbortsWhenUnreachable` class (lines 175-200) with:

```python
class TestSessionOpensWithoutPausingWhenUnreachable(unittest.TestCase):
    """MODE pressed with RetroArch's command port silent -- no game
    running (e.g. pressed from EmulationStation), or the port just isn't
    answering -- must still open the overlay. It must NOT send
    PAUSE_TOGGLE: there is nothing running to pause, and pausing here
    with no matching resume signal would be a bug."""

    def test_launches_but_does_not_pause_when_retroarch_does_not_answer(self):
        pause_calls = []

        real_query = cs.retroarch_cmd_query
        real_pause = cs.send_pause_toggle
        real_popen = cs.subprocess.Popen
        self.addCleanup(setattr, cs, "retroarch_cmd_query", real_query)
        self.addCleanup(setattr, cs, "send_pause_toggle", real_pause)
        self.addCleanup(setattr, cs.subprocess, "Popen", real_popen)

        cs.retroarch_cmd_query = lambda cmd, timeout_s=0.5: None
        cs.send_pause_toggle = lambda: pause_calls.append("pause") or True

        class FakeProc:
            def poll(self):
                return 0  # already exited: loop breaks on first check

            def terminate(self):
                pass

            def kill(self):
                pass

            def wait(self, timeout=None):
                pass

        launched = []

        def fake_popen(args, env=None):
            launched.append(args)
            return FakeProc()

        cs.subprocess.Popen = fake_popen
        cs.run_quickmenu_session()

        self.assertEqual(launched, [[cs.QUICKMENU_BIN]],
                          "the overlay must launch even with no game running")
        self.assertEqual(pause_calls, [],
                          "no PAUSE_TOGGLE may fire when nothing is running to pause")
```

Then add a new class right after it, covering the still-required reachable path (regression protection for the pause/resume tracking):

```python
class TestSessionPausesAndResumesWhenReachable(unittest.TestCase):
    """When RetroArch's command port does answer, behavior is unchanged
    from before this task: pause once on open, resume once on close."""

    def test_pauses_once_and_resumes_once_when_retroarch_reachable(self):
        pause_calls = []

        real_query = cs.retroarch_cmd_query
        real_pause = cs.send_pause_toggle
        real_popen = cs.subprocess.Popen
        real_read_mode = cs.read_mode_button
        self.addCleanup(setattr, cs, "retroarch_cmd_query", real_query)
        self.addCleanup(setattr, cs, "send_pause_toggle", real_pause)
        self.addCleanup(setattr, cs.subprocess, "Popen", real_popen)
        self.addCleanup(setattr, cs, "read_mode_button", real_read_mode)

        cs.retroarch_cmd_query = lambda cmd, timeout_s=0.5: b"GET_STATUS ok"
        cs.send_pause_toggle = lambda: pause_calls.append("pause") or True
        cs.read_mode_button = lambda: False

        class FakeProc:
            def __init__(self):
                self._polled_once = False

            def poll(self):
                if not self._polled_once:
                    self._polled_once = True
                    return None  # still running on first check
                return 0  # exited on second check

            def terminate(self):
                pass

            def kill(self):
                pass

            def wait(self, timeout=None):
                pass

        cs.subprocess.Popen = lambda args, env=None: FakeProc()
        cs.run_quickmenu_session()

        self.assertEqual(pause_calls, ["pause", "pause"],
                          "exactly one pause (open) and one resume (close) "
                          "when RetroArch is reachable")
```

- [ ] **Step 2: Run the tests to confirm they fail against the current (pre-change) daemon code**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py
```

Expected: `test_launches_but_does_not_pause_when_retroarch_does_not_answer` FAILS with `AssertionError: quickmenu must not be launched` is gone (that assertion no longer exists), but the test fails a different way — `run_quickmenu_session()` currently `return`s immediately when `retroarch_cmd_query` returns `None`, so `launched` stays `[]`, failing `self.assertEqual(launched, [[cs.QUICKMENU_BIN]], ...)`. `test_pauses_once_and_resumes_once_when_retroarch_reachable` should PASS already (this path is unchanged) — confirms the new test correctly isolates the behavior this task changes.

- [ ] **Step 3: Restructure `run_quickmenu_session()` in the daemon**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`, replace the entire function (currently lines 507-562):

```python
def run_quickmenu_session():
    """One full open/close cycle. Blocks until the menu is closed and the
    game has been resumed. Never returns with the game left paused."""
    # 1. Reachability BEFORE anything visible happens. If RetroArch's
    #    command port does not answer, abort: never pause a game we
    #    cannot prove we can un-pause.
    if retroarch_cmd_query(b"GET_STATUS") is None:
        print("[rpi-circuitsword] quickmenu: RetroArch command port silent, aborting",
              file=sys.stderr)
        return

    if not send_pause_toggle():
        print("[rpi-circuitsword] quickmenu: PAUSE_TOGGLE failed, aborting",
              file=sys.stderr)
        return

    proc = None
    try:
        env = dict(os.environ)
        env.update(QUICKMENU_ENV)
        proc = subprocess.Popen([QUICKMENU_BIN], env=env)
    except OSError as e:
        print(f"[rpi-circuitsword] quickmenu: launch failed: {e}", file=sys.stderr)

    if proc is not None:
        button = ModeButton()
        closing_since = None
        while True:
            rc = proc.poll()
            if rc is not None:
                if rc != 0:
                    print(f"[rpi-circuitsword] quickmenu exited rc={rc}", file=sys.stderr)
                break
            # A second MODE press is the close signal: SIGTERM the menu.
            if button.update(read_mode_button(), time.monotonic()) and closing_since is None:
                closing_since = time.monotonic()
                print("[rpi-circuitsword] quickmenu: MODE again -> closing", file=sys.stderr)
                proc.terminate()
            # Watchdog: 5s after a close signal, kill it regardless.
            if closing_since is not None and \
                    (time.monotonic() - closing_since) >= QUICKMENU_WATCHDOG_S:
                print("[rpi-circuitsword] quickmenu: watchdog expired, killing",
                      file=sys.stderr)
                proc.kill()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
                break
            time.sleep(QUICKMENU_POLL_INTERVAL_S)

    # 2. Resume -- unconditionally, whatever happened above. There is no
    #    display to hand back: labwc composited RetroArch's surface the
    #    entire time, so the game is already visible again the moment the
    #    overlay surface is destroyed.
    send_pause_toggle()
```

with:

```python
def run_quickmenu_session():
    """One full open/close cycle. Blocks until the menu is closed. The
    overlay always launches -- from ES with no game running, or in-game
    -- regardless of whether RetroArch answers. Only pause/resume is
    conditional: RetroArch is paused on open only if its command port
    answers at entry, and resumed on close only if this session actually
    paused it. Never returns with a game left paused that this session
    itself paused."""
    # Reachability BEFORE pausing. If RetroArch's command port does not
    # answer -- no game running (e.g. MODE pressed from ES), or the port
    # is silent for some other reason -- skip pausing, but still open
    # the menu: there is nothing to pause, not a reason to refuse to open.
    paused_this_session = False
    if retroarch_cmd_query(b"GET_STATUS") is not None:
        if send_pause_toggle():
            paused_this_session = True
        else:
            print("[rpi-circuitsword] quickmenu: PAUSE_TOGGLE failed, opening unpaused",
                  file=sys.stderr)

    proc = None
    try:
        env = dict(os.environ)
        env.update(QUICKMENU_ENV)
        proc = subprocess.Popen([QUICKMENU_BIN], env=env)
    except OSError as e:
        print(f"[rpi-circuitsword] quickmenu: launch failed: {e}", file=sys.stderr)

    if proc is not None:
        button = ModeButton()
        closing_since = None
        while True:
            rc = proc.poll()
            if rc is not None:
                if rc != 0:
                    print(f"[rpi-circuitsword] quickmenu exited rc={rc}", file=sys.stderr)
                break
            # A second MODE press is the close signal: SIGTERM the menu.
            if button.update(read_mode_button(), time.monotonic()) and closing_since is None:
                closing_since = time.monotonic()
                print("[rpi-circuitsword] quickmenu: MODE again -> closing", file=sys.stderr)
                proc.terminate()
            # Watchdog: 5s after a close signal, kill it regardless.
            if closing_since is not None and \
                    (time.monotonic() - closing_since) >= QUICKMENU_WATCHDOG_S:
                print("[rpi-circuitsword] quickmenu: watchdog expired, killing",
                      file=sys.stderr)
                proc.kill()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
                break
            time.sleep(QUICKMENU_POLL_INTERVAL_S)

    # Resume only if this session actually paused. There is no display to
    # hand back either way: labwc composited RetroArch's surface (if any)
    # the entire time, so a paused game is already visible again the
    # moment the overlay surface is destroyed.
    if paused_this_session:
        send_pause_toggle()
```

- [ ] **Step 4: Remove the outer `retroarch_running()` gate in `quickmenu_thread()`**

In the same file, in `quickmenu_thread()` (currently lines 565-597), replace:

```python
            else:
                try:
                    if retroarch_running():
                        run_quickmenu_session()
                    else:
                        # ES already exposes wifi/volume/brightness in its
                        # own settings -- nothing to do here.
                        print("[rpi-circuitsword] quickmenu: MODE with no game running, ignored",
                              file=sys.stderr)
                finally:
```

with:

```python
            else:
                try:
                    run_quickmenu_session()
                finally:
```

Leave `retroarch_running()` itself defined (unchanged) — it is still independently unit-tested by `TestRetroarchRunning` and is a coherent primitive even though `quickmenu_thread()` no longer calls it.

- [ ] **Step 5: Remove the stale "mode+left/right combo" comments**

In the same file, replace the section header comment above `BACKLIGHT_SYSFS_BRIGHTNESS` (currently lines 257-267):

```python
# ============================================================
# Backlight: two write paths into the same Arduino command (CMD_SET_BL),
# both funneled through serial_cmd()'s lock so they can't race:
#   1. Batocera's native brightness UI writes
#      /sys/class/backlight/circuitsword-backlight/brightness -- this
#      thread polls that file for changes and pushes them to the Arduino.
#   2. A physical mode+left/right button combo (CMD_GET_STATUS bit 0 =
#      mode button, combined with directional input) adjusts brightness
#      directly, "blind" (no on-screen feedback) -- matches how
#      Retropie/nixos did it via their own HUD, just without the HUD.
# mode+up/down (volume) is explicitly deferred, see design doc.
# ============================================================
```

with:

```python
# ============================================================
# Backlight: Batocera's native brightness UI writes
# /sys/class/backlight/circuitsword-backlight/brightness -- this thread
# polls that file for changes and pushes them to the Arduino via
# CMD_SET_BL. Direct button-driven brightness (and volume) adjustment
# happens through the quickmenu overlay instead -- see
# package/batocera/utils/circuitsword-quickmenu/quickmenu.c -- not here.
# ============================================================
```

Then, inside `backlight_bridge()`, remove the now-fully-superseded comment block (currently lines 315-324, immediately before `stop_event.wait(BACKLIGHT_POLL_INTERVAL_S)`):

```python
        # mode+left/right combo: only act on the mode button's rising
        # edge combined with a directional read to avoid repeat-firing
        # every poll while both are held.
        # NOTE: directional (left/right) button state is read via the
        # same GPIO button-combo mechanism as the physical controller
        # buttons -- wiring that read is controller-hardware-specific
        # and out of scope for this bridge; this function assumes a
        # `read_direction_pressed()` helper exists once the controller
        # GPIO/evdev read is wired up in Task 8's on-device pass. Until
        # then this loop only handles the sysfs->Arduino direction.

        stop_event.wait(BACKLIGHT_POLL_INTERVAL_S)
```

with just:

```python
        stop_event.wait(BACKLIGHT_POLL_INTERVAL_S)
```

- [ ] **Step 6: Run the tests to confirm they pass**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py
```

Expected: all tests PASS, including both `TestSessionOpensWithoutPausingWhenUnreachable` and `TestSessionPausesAndResumesWhenReachable`, and everything else in the file (`TestConstants`, `TestModeButton`, `TestRetroarchRunning`) unaffected.

- [ ] **Step 7: Commit the build-tree change**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: always open quickmenu on MODE, decouple pause from launch"
```

(The test file under `/Users/bas/Circuit-Sword Batocera/tests/` is not part of any git repo — nothing to commit there, it's already saved on disk from Step 1.)

---

## Task 2: Quick-menu palette — match Batocera's black-background/dark-red look

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h` (lines 23-28)

**Interfaces:**
- Consumes: nothing new.
- Produces: `QM_COLOR_BG`, `QM_COLOR_FG`, `QM_COLOR_DIM`, `QM_COLOR_SEL_BG`, `QM_COLOR_BAR_BG`, `QM_COLOR_BAR_FG` — same macro names, same `QM_RGB(r,g,b)` shape, new values. Consumed by `quickmenu.c`'s `qm_render()` (unchanged, reads these by name) and by `tests/test_qm_font.c`'s `qm_render` checks (unchanged, compares pixels against the macro itself rather than a hardcoded value, so no test-file edit needed here).

- [ ] **Step 1: Confirm the current host C tests pass before touching colors (baseline)**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
bash tests/run-c-tests.sh
```

Expected: `PASSED (0 failures)`. This confirms the harness works before the change, so a later failure can only be attributed to this task's edit.

- [ ] **Step 2: Change the six color constants**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`, replace lines 23-28:

```c
#define QM_COLOR_BG       QM_RGB(0x10, 0x10, 0x18)
#define QM_COLOR_FG       QM_RGB(0xE0, 0xE0, 0xE0)
#define QM_COLOR_DIM      QM_RGB(0x80, 0x80, 0x88)
#define QM_COLOR_SEL_BG   QM_RGB(0x30, 0x50, 0x90)
#define QM_COLOR_BAR_BG   QM_RGB(0x30, 0x30, 0x38)
#define QM_COLOR_BAR_FG   QM_RGB(0x50, 0xC0, 0x70)
```

with:

```c
#define QM_COLOR_BG       QM_RGB(0x00, 0x00, 0x00)   /* black - matches Batocera's current on-device menu background */
#define QM_COLOR_FG       QM_RGB(0xDD, 0xDD, 0xDD)   /* es-theme-carbon systemInfoColor */
#define QM_COLOR_DIM       QM_RGB(0x80, 0x80, 0x80)   /* neutral gray, for unselected/secondary text against black */
#define QM_COLOR_SEL_BG    QM_RGB(0x8B, 0x00, 0x00)   /* es-theme-carbon baseColor - selection/header fill */
#define QM_COLOR_BAR_BG    QM_RGB(0x30, 0x30, 0x30)   /* dark neutral gray, for bar track against black */
#define QM_COLOR_BAR_FG    QM_RGB(0xBD, 0x47, 0x47)   /* es-theme-carbon groupColor - bar fill / active accent */
```

(Note the source alignment has a minor pre-existing inconsistency after `QM_COLOR_BG` — preserve it exactly as shown so the diff is minimal; do not reformat unrelated whitespace.)

- [ ] **Step 3: Run the host C tests to confirm the rendering code still compiles and passes with the new palette**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
bash tests/run-c-tests.sh
```

Expected: `PASSED (0 failures)` again — `qm_render` checks compare against `QM_COLOR_BG` by macro reference, so they pass automatically with any valid color value; a real compile failure here would mean a syntax mistake in Step 2, not a logic problem.

- [ ] **Step 4: Commit the build-tree change**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.h
git commit -m "circuitsword-quickmenu: match Batocera's black/dark-red palette"
```

---

## Task 3: Capture patch, findings log, final verification

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch`
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-EVERYWHERE-RESTYLE-FINDINGS.md`

**Interfaces:**
- Consumes: the two commits from Task 1 and Task 2 (already in `batocera.linux`'s git history at this point).
- Produces: nothing consumed by later tasks — this is the last task in the plan.

- [ ] **Step 1: Regenerate the patch capture**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

- [ ] **Step 2: Verify both changes landed in the regenerated patch**

```bash
grep -c "paused_this_session" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "0x8B, 0x00, 0x00" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Expected: both commands print a number `>= 1`. If either prints `0`, the corresponding Task 1 or Task 2 commit is missing from history or from the diff range — stop and investigate (likely an uncommitted change, or a commit made outside the `batocera.linux` repo root) before proceeding.

- [ ] **Step 3: Run the full off-device test suite one more time, end to end**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py
bash tests/run-c-tests.sh
```

Expected: both PASS with zero failures.

- [ ] **Step 4: Write the findings log**

Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-EVERYWHERE-RESTYLE-FINDINGS.md`:

```markdown
# Phase 4 Quick Menu — Always-On Trigger + Restyle: Findings

Follow-up to `docs/superpowers/plans/2026-08-06-phase4-quickmenu-v2.md` /
`PHASE4-QUICKMENU-FINDINGS.md`. Implements
`docs/superpowers/specs/2026-08-10-quickmenu-everywhere-restyle-design.md`.

## What changed

- `rpi-circuitsword.py`: `quickmenu_thread()` no longer gates MODE on
  `retroarch_running()` -- the overlay now opens from EmulationStation as
  well as in-game. `run_quickmenu_session()` decouples "launch" (always)
  from "pause/resume" (conditional on RetroArch's command port answering,
  tracked via `paused_this_session` so a resume is never sent without a
  matching pause).
- `quickmenu.h`: `QM_COLOR_*` palette changed from the original custom
  dark-blue/gray scheme to a black background with `es-theme-carbon`'s
  dark-red accent colors (`8B0000`, `BD4747`), fetched and read directly
  from `github.com/fabricecaruso/es-theme-carbon`'s `theme.xml` to ground
  real values rather than guessing.
- Stale "mode+left/right combo" comments (a brightness-adjustment scheme
  that was never finished) removed from `rpi-circuitsword.py` -- brightness
  adjustment happens via the quickmenu overlay, which already shipped in
  the prior phase.

## Verified off-device

- `tests/test_quickmenu_logic.py`: full suite passes, including the new
  `TestSessionOpensWithoutPausingWhenUnreachable` (overlay launches with
  no game running, no spurious PAUSE_TOGGLE) and
  `TestSessionPausesAndResumesWhenReachable` (exactly one pause, one
  resume, unchanged from the prior phase's behavior when a game is
  running).
- `tests/run-c-tests.sh`: `circuitsword-quickmenu`'s pure rendering code
  compiles and passes with the new palette constants.
- Patch capture (`batocera-build/patches/batocera-linux.patch`) confirmed
  to contain both changes via targeted `grep`.

## Needs on-device validation (not yet done)

- Actual visual appearance of the new black-background/dark-red palette
  on the real DPI panel -- expected to need a quick tuning pass, per the
  design doc.
- Overlay compositing correctly above EmulationStation itself via
  `labwc`'s overlay layer -- previously proven only above a running
  RetroArch instance, not yet exercised with MODE pressed from ES.
- No input-focus/evdev-grab conflict with ES's own controller handling
  when the overlay opens from ES (untested combination).
- Confirm no spurious PAUSE_TOGGLE/resume when opening from ES with a
  game paused-but-backgrounded, if that state is reachable on this image
  (edge case not covered by the unit tests, which only exercise "no game
  running at all" vs. "game running and reachable").
```

- [ ] **Step 5: No commit needed for this step's own files**

The findings log and patch file both live under `/Users/bas/Circuit-Sword Batocera`, which has no git repo by deliberate project choice — they are complete once saved to disk. The two commits already made inside `/Users/bas/batocera-build-wifi/batocera.linux` (Task 1 Step 7, Task 2 Step 4) are the only git commits this plan makes.

---

## Out of scope (carried over from the design doc)

- Any change to `quickmenu.c`'s menu items, navigation, or interaction model.
- Linking against EmulationStation's own C++/SDL rendering stack.
- Font changes.
- Any mode+button combo work beyond what already exists (e.g. mode+up/down volume) — remains deferred.
- Building and flashing a full image / on-device testing — this plan's scope is the off-device-verifiable code change only; on-device validation is tracked as open items in the findings log for a future session, the same way Task 15/16 were tracked separately in the prior phase.
