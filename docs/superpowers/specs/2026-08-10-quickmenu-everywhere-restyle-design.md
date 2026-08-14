# Circuit-Sword Quick Menu — Always-On Trigger + Batocera-Style Palette (Phase 4 follow-up)

Extends `docs/superpowers/specs/2026-08-06-quickmenu-design.md`. That design shipped and works on real hardware: MODE opens `circuitsword-quickmenu` (WiFi/Volume/Brightness, live left/right adjust) while a game is running, pausing RetroArch underneath. This follow-up changes two things the user asked for after using it:

1. MODE should open the same overlay from EmulationStation too, not only in-game.
2. The overlay's colors should visually match Batocera's own UI instead of the current custom dark-blue/gray palette.

## Context

Current behavior, from `rpi-circuitsword.py`'s `quickmenu_thread()`: MODE is a no-op unless `retroarch_running()` is true. The user's reasoning for changing this is predictability — "the button does the same thing everywhere" beats avoiding overlap with ES's own native WiFi/Volume/Brightness UI. Confirmed with the user: redundancy with ES's native settings screens is acceptable, not a concern.

Investigation this session found the remaining work is smaller than originally scoped: `quickmenu.c`/`quickmenu.h` already implement all 3 items (WiFi, Volume, Brightness) with exactly the live left/right-adjust interaction the user wants — confirmed by reading the full source, not by assumption. The user separately confirmed both of these as already correct: all three items in one overlay, and direct left/right live-adjust (not a two-step select-then-adjust flow). Nothing changes in `quickmenu.c`'s menu logic or item set.

The user's second ask ("reuse Batocera UI elements") was clarified via brainstorming to mean **visual-only** matching (colors/font style) — not linking against EmulationStation's own C++/SDL rendering stack. This preserves the toolkit-free `wl_shm` + custom bitmap-font approach from the original design, which was the user's own explicit preference; reusing ES's actual rendering code would be a much larger, architecturally different undertaking (a small standalone daemon-launched binary linking against ES's engine) that was never on the table here.

## Design

### 1. Daemon: decouple "should I open" from "should I pause"

In `rpi-circuitsword.py`, `quickmenu_thread()` currently gates the entire feature on `retroarch_running()`:

```
if retroarch_running():
    run_quickmenu_session()
else:
    # MODE press ignored
```

This becomes: **always** call the session function on MODE press. Inside `run_quickmenu_session()`, the RetroArch-reachability probe (currently used to decide both "should I pause" and, via early return, "should I even launch the overlay") is split into two independent concerns:

- **Launch**: unconditional. `circuitsword-quickmenu` runs whether or not a game is active.
- **Pause/resume**: conditional on RetroArch actually being reachable via its UDP command port, exactly as today. A new local flag (`paused_this_session`) records whether the opening `PAUSE_TOGGLE` was actually sent, so the closing side only sends the matching `PAUSE_TOGGLE` if the opening one happened. This avoids a resume-toggle firing with no matching pause when the menu was opened from ES with no game running, or when RetroArch is running but its command port doesn't answer.

Sequence, generalized (replaces the RetroArch-only sequence in the original design doc):

```
MODE pressed (daemon detects via existing CMD_GET_STATUS poll)
  │  busy/in-transition flag prevents re-entry, as today
  ▼
daemon probes RetroArch's command port
  │  reachable? → PAUSE_TOGGLE, set paused_this_session = True
  │  not reachable (ES, or RetroArch not running, or port not answering)? → skip, paused_this_session = False
  ▼
daemon launches circuitsword-quickmenu (always), waits for it to exit
  │  B pressed, or a second MODE press relayed as SIGTERM
  ▼
circuitsword-quickmenu exits
  ▼
if paused_this_session: daemon sends PAUSE_TOGGLE again (resume)
```

The 5-second watchdog (kill-and-resume-regardless on hang/crash) is unchanged, and now also correctly respects `paused_this_session` — it must not send a bare resume toggle when nothing was paused.

`quickmenu_thread()`'s outer `retroarch_running()` check is removed entirely; the reachability probe inside `run_quickmenu_session()` becomes the only conditional, and it now only governs pause/resume, never launch.

### 2. Stale comment cleanup

`rpi-circuitsword.py` lines ~257-267 describe a "MODE + left/right" direct-brightness-adjustment scheme that was never finished (the code's own NOTE at lines 318-324 admits it depends on an unimplemented `read_direction_pressed()` helper). This is now fully superseded by the quickmenu overlay's own brightness item. The stale comment block is removed/replaced with a short note that brightness adjustment happens via the quickmenu overlay, pointing at `quickmenu.c` — not left as dead, misleading documentation of a feature that doesn't exist.

### 3. Visual palette: match Batocera's default theme

Batocera's shipped default theme is `es-theme-carbon` (`package/batocera/emulationstation/es-theme-carbon/`, fetched from `github.com/fabricecaruso/es-theme-carbon` at build time — not vendored in-tree, so its actual `theme.xml` was fetched and read directly to ground real values rather than guessing). Its core palette (`theme.xml` `<variables>`):

| Theme variable | Hex | Role in es-theme-carbon |
|---|---|---|
| `baseColor` | `8b0000` | primary accent (headers, selection) |
| `gradientEndColor` | `6b2020` | accent gradient end |
| `backgroundColor` | `200000` | screen background |
| `bottomBarColor` | `8b0000c0` | bottom help-bar fill |
| `groupColor` / `groupSeparatorColor` | `bd4747` | brighter secondary accent |
| `systemInfoColor` | `ddddddd8` | body text |

New `QM_COLOR_*` constants in `quickmenu.h`, mapped from these (dropping alpha, since the overlay is drawn fully opaque per the original design's chosen style). Per the user's follow-up request, the background is plain black — matching the actual black background Batocera's menu currently shows on this device, rather than `theme.xml`'s literal (dark maroon) `backgroundColor` value:

```c
#define QM_COLOR_BG       QM_RGB(0x00, 0x00, 0x00)   /* black - matches Batocera's current on-device menu background */
#define QM_COLOR_FG        QM_RGB(0xDD, 0xDD, 0xDD)   /* systemInfoColor */
#define QM_COLOR_DIM       QM_RGB(0x80, 0x80, 0x80)   /* neutral gray, for unselected/secondary text against black */
#define QM_COLOR_SEL_BG    QM_RGB(0x8B, 0x00, 0x00)   /* baseColor - selection/header fill */
#define QM_COLOR_BAR_BG    QM_RGB(0x30, 0x30, 0x30)   /* dark neutral gray, for bar track against black */
#define QM_COLOR_BAR_FG    QM_RGB(0xBD, 0x47, 0x47)   /* groupColor - bar fill / active accent */
```

`QM_COLOR_DIM` and `QM_COLOR_BAR_BG` aren't literal theme variables (the theme has no direct equivalent for "dim text" or "unfilled bar track") — with a black background they're plain neutral grays rather than tinted toward the theme's maroon, since tinting toward red would look inconsistent against true black. This is a reasonable approximation, not a pixel-perfect port, consistent with "visual-only matching" as scoped. Only these six `#define`s in `quickmenu.h` change; no layout, sizing, or font changes.

Since this is a small, easily-tunable set of constants and the true test is how it looks on the actual DPI panel, the plan should treat these hex values as a starting point that gets a quick on-device look-and-adjust pass, not a value that must be pixel-matched before merge.

## Data Flow

Unchanged from the original design except for the removed outer `retroarch_running()` gate: MODE press → busy-flag check → reachability probe (pause decision only) → launch `circuitsword-quickmenu` unconditionally → user interacts (WiFi/Volume/Brightness, live left/right) → exit (B or repeat MODE) → resume only if this session actually paused.

## Error Handling

Carries over unchanged from the original design (no-op on busy/in-transition MODE presses, watchdog kill-and-resume on hang/crash, non-zero exit from `circuitsword-quickmenu` treated as "could not run"), with one addition: the watchdog's resume step must check `paused_this_session` before sending `PAUSE_TOGGLE`, so a crash while the menu was opened from ES (nothing paused) doesn't send a spurious resume toggle to a RetroArch instance that either isn't running or was never paused by this session.

## Testing

**Verifiable off-device**: `rpi-circuitsword.py`'s `run_quickmenu_session()` pause/resume-tracking logic is unit-testable in isolation (existing pattern in `test_quickmenu_logic.py`) — new test case: MODE with no RetroArch reachable must launch the overlay and must not call `send_pause_toggle()` at all. `quickmenu.c`/`quickmenu.h` changes are colors-only; existing build/compile verification is unaffected.

**Needs on-device validation**: actual visual appearance of the new palette on the real DPI panel (explicitly expected to need a quick tuning pass); confirming the overlay still composites correctly above ES itself via `labwc`'s overlay layer (same mechanism already proven above RetroArch, but not yet exercised above ES specifically); confirming no input-focus/evdev-grab conflict with ES's own controller handling when the overlay opens from ES.

## Out of Scope (this follow-up)

- Any change to `quickmenu.c`'s menu items, navigation, or interaction model — already correct as shipped.
- Linking against or reusing EmulationStation's actual rendering stack (C++/SDL) — explicitly ruled out per the user's own toolkit-free preference; visual matching is colors/style only.
- Font changes — the existing 5×7 bitmap font is unchanged.
- Any further mode+button combo work (e.g. mode+up/down volume) — remains deferred per the original Phase 3/4 scope.
