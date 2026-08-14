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
