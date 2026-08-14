# Persistent In-Game Status Bar: Findings

Implements `docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md`,
a follow-up to the Phase 4 quick-menu work. Restores a capability the
original manufacturer firmware (`github.com/kiteretro/Circuit-Sword`,
DispmanX-based) had and this project's own RetroPie-based rewrite lost
when it moved to 64-bit KMS.

## What was built

- `circuitsword-quickmenu`'s `qm_settings.c`/`quickmenu.h`: new
  `qm_battery_get()` (and its pure core, `qm_battery_parse()`), reading
  `/sys/class/power_supply/battery/{capacity,status}` -- the same sysfs
  path confirmed working live via SSH during this session's earlier
  charge-icon debugging.
- New `circuitsword-statusbar` Buildroot package: a second, independent
  Wayland client sharing `circuitsword-quickmenu`'s `qm_font.c` and
  `qm_settings.c` directly (compiled from the neighboring package
  directory, no shared library). Draws a top-anchored (not full-screen)
  `wlr-layer-shell-unstable-v1` overlay-layer surface -- battery, WiFi,
  volume, brightness, left to right -- with `exclusive_zone 0` and no
  input handling at all: purely passive, never affects the running game.
- `rpi-circuitsword.py`: new `statusbar_thread`/`statusbar_tick()`, using
  the already-existing `retroarch_running()` (unused since the
  quickmenu-everywhere-restyle change) to launch `circuitsword-statusbar`
  when a game starts, kill it when the game exits, and self-heal
  (relaunch) if the tracked process is found dead mid-session.

## Verified off-device

- `tests/test_qm_settings.c`: `qm_battery_parse()`'s clamping and
  error-sentinel logic.
- `tests/test_sb_render.c`: `sb_render()` paints a background and visibly
  changes output for charging-state and WiFi-state differences; extreme
  (0 and 100) values don't crash.
- `tests/test_quickmenu_logic.py`'s new `TestStatusbarTick`: launches on
  game-start, stays untouched while already running, stops on game-exit,
  self-heals if found dead mid-session, and never launches when nothing
  is running -- five cases, all passing.
- Incremental Buildroot compiles (`bcm2837-pkg PKG=circuitsword-statusbar-rebuild`
  and `PKG=circuitsword-quickmenu-rebuild`) both succeed: the new package
  compiles cleanly against real cross-toolchain headers and links, and
  `circuitsword-quickmenu` still compiles cleanly after sharing its files
  with the new package.
- Patch capture confirmed to contain all four changes via targeted `grep`.

## Needs on-device validation (not yet done)

- The top-anchored overlay-layer surface actually composites above a
  running fullscreen RetroArch client -- related to, but not identical
  to, the full-screen-anchored case `circuitsword-quickmenu` already
  proved; must be confirmed separately.
- Visual layout/sizing/legibility of the bar on the real DPI panel (the
  `SB_BAR_HEIGHT`/scale choices in `sb_wl.c`/`sb_render.c` are reasonable
  estimates, not yet seen on hardware).
- Z-ordering when `circuitsword-quickmenu` opens on top of an
  already-visible status bar -- both are overlay-layer clients; expected
  to just work via quickmenu's full-screen occlusion, but unverified.
- WiFi connected/disconnected icon accuracy against real network state
  changes.
- Any visible flicker on the 2-second redraw-on-change cadence during
  actual gameplay.
- `statusbar_thread`'s 1-second poll latency for start/stop feels
  imperceptible in design but hasn't been felt on real hardware.

## Known deferred items (from final review, not blocking)

- **Resource cost**: `statusbar.c`'s 2-second poll loop calls
  `qm_wifi_get()`/`qm_volume_get()`/`qm_brightness_get()` every wake, each
  of which shells out (`popen()`) to `batocera-settings-get`/
  `batocera-audio` -- several process spawns every 2s for the entire
  gameplay session, on a 1GB CM3. This is the same primitive
  `circuitsword-quickmenu` already uses, but that only runs for the short
  window the menu is open; the status bar runs for the whole session.
  Worth profiling on-device (CPU/battery impact) before assuming it's
  fine; the interval or the read mechanism may need tuning later.
- **Scope gap**: `retroarch_running()` (reused from the existing quickmenu
  thread) only detects `retroarch`-named processes, so the status bar
  (like the quickmenu overlay before it) will not appear for any
  standalone, non-libretro emulator if one is ever added to this image.
  Matches an existing, already-accepted limitation elsewhere in the
  codebase, not a new gap introduced here.
- **Icons vs. text**: the design's "battery/WiFi/volume/brightness icons"
  phrasing is implemented as short ASCII text labels (`BAT`, `WIFI`,
  `VOL`, `BRT`), consistent with this project's existing toolkit-free,
  ASCII-only 5x7 bitmap font (same font `circuitsword-quickmenu` uses) --
  not a functional gap, just worth stating explicitly since "icon" could
  be read literally.
- **Narrow-panel (320px) support**: the layout-overflow fix (final review,
  commit `dffc835b04`) only targeted the primary 640px-wide DPI panel.
  The alternative 320px panel variant still overflows by roughly 86px in
  worst-case values at scale 2. Deferred -- not exercised on real
  hardware, and out of scope for this plan's fix wave.
- **Minor, low-risk**: no backoff if `circuitsword-statusbar` keeps
  failing to launch (the daemon just retries every second indefinitely);
  a possible transient zombie process during the self-heal path between
  detecting a dead process and the next tick. Neither affects correctness
  of the common case.
