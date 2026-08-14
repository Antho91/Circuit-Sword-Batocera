# Circuit-Sword Persistent In-Game Status Bar — Design (Phase 4 follow-up)

Extends the Phase 4 quick-menu work (`docs/superpowers/specs/2026-08-06-quickmenu-design.md`, `docs/superpowers/specs/2026-08-10-quickmenu-everywhere-restyle-design.md`). Adds a second, independent overlay: a permanent top status bar (battery, WiFi, volume, brightness) visible the entire time a game is running — not opened by a button, never pauses anything, never captures input.

## Context

The original manufacturer firmware (`github.com/kiteretro/Circuit-Sword`, `cs-hud/src/`) ran exactly this kind of always-on top bar, via the Raspberry Pi's legacy DispmanX layer API — a single `cs-hud.service` running continuously from boot, compositing battery/WiFi/mute/volume/brightness icons on top of whatever was on screen, including a running emulator (`display_manager.c`: `IMAGE_LAYER_T battery_layer`, `DISPMANX_DISPLAY_HANDLE_T`). This project's own later RetroPie-based rewrite (`Retropie_source/cs-hud_new/`) lost this capability entirely — the 64-bit KMS stack it targets has no DispmanX-equivalent overlay layer (`Retropie_source/CLAUDE.md`: "no DispmanX/overlay layer exists... A HUD cannot draw on top of a running emulator. Hard platform limitation.") — and fell back to a full-screen VT-switch on-demand menu instead.

This Batocera port's `labwc` compositor (confirmed on real hardware during the original quickmenu design work) makes a real overlay possible again, via `wlr-layer-shell-unstable-v1`. This design brings back the original always-on bar using that mechanism — not a new idea, a restoration of something the hardware used to do before the KMS transition removed it.

Confirmed with the user via brainstorming:
- Always visible for the entire time a game is running (matches the original's behavior), not a flash-on-change notification.
- All four original icons: battery (+ charging), WiFi, volume, brightness.
- Full-width bar at the top of the screen (matches the original layout).
- A new, separate binary from `circuitsword-quickmenu` — architecturally a different kind of process (passive, never grabs input, never pauses the game) and keeping it separate keeps `circuitsword-quickmenu` focused on its own job (interactive menu, pauses the game, grabs input for navigation).

## Architecture

`circuitsword-statusbar` is a second small Wayland client, sharing the same class of primitives `circuitsword-quickmenu` already proved out (raw `libwayland-client`, no toolkit) but a distinct binary and process:

```
Game starts (daemon's existing retroarch_running() goes True)
  ▼
daemon's new statusbar_thread launches circuitsword-statusbar
  ▼
circuitsword-statusbar connects to labwc, binds wl_compositor/wl_shm/
zwlr_layer_shell_v1, creates a layer-shell surface on layer=overlay,
anchored to the TOP edge only (not all four edges like quickmenu),
keyboard_interactivity=none, and never touches evdev at all --
purely passive, draws and nothing else
  ▼
polls battery/WiFi/volume/brightness every 2s, redraws only on change
  ▼
Game exits (retroarch_running() goes False)
  ▼
daemon kills circuitsword-statusbar
```

Unlike `circuitsword-quickmenu`, this surface reserves no exclusive screen space (`exclusive_zone` not set/0) — RetroArch's fullscreen surface underneath is unaffected; the bar just draws on top of it via the overlay layer, the same proven mechanism, just anchored differently.

When the user also presses MODE while playing, `circuitsword-quickmenu` opens on the same overlay layer, full-screen and opaque, on top of everything including the status bar. The two processes do not need to know about each other for this to work correctly — quickmenu's existing full-screen coverage naturally occludes the bar. This is a real assumption about same-layer z-ordering between two independent overlay-layer clients, not yet exercised on hardware, and is called out in Testing below rather than assumed.

## Components

### `rpi-circuitsword.py` (extended)

New `statusbar_thread(stop_event)`, following the same shape as the existing `fan_thread`/`battery_bridge`/`backlight_bridge` threads (started alongside them in `main()`):

- Polls `retroarch_running()` (already defined, currently unused since today's MODE-gating change — this gives it a new purpose) on a slower cadence than the MODE-button poll, e.g. 1s — imperceptible startup/teardown latency for a passive display element, unlike a button-press response.
- On the rising edge (game just started) and no bar process currently tracked: launch `circuitsword-statusbar` via `subprocess.Popen`, same `QUICKMENU_ENV` (`WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR`) the quickmenu launch already uses.
- On the falling edge (game just exited): terminate the tracked process.
- If the tracked process is found dead (`poll()` returns non-`None`) while a game is still running: treat it the same as "no process running" and relaunch on the next tick — a crash mid-session self-heals within one poll interval, no watchdog/timeout machinery needed (unlike quickmenu, there is no input-driven interaction that could hang; the process either runs or it's dead, nothing in between to time out).

### `circuitsword-statusbar` (new)

Standalone C program, same toolkit-free approach as `circuitsword-quickmenu` (raw `libwayland-client`, no cairo/pango/glib):

- Reuses the existing `zwlr_layer_shell_v1` / `xdg-shell.xml` protocol plumbing already vendored and generated for `circuitsword-quickmenu` (`package/batocera/utils/circuitsword-quickmenu/protocols/`) — the new package depends on the same generated headers rather than re-vendoring the XML.
- Reuses `qm_font.c`'s pure drawing primitives (`qm_fill_rect`, `qm_draw_text`, etc.) directly — no fork, the existing 5×7 bitmap font renderer is generic over any `qm_fb`, not specific to the full-screen menu layout.
- New, much smaller render function (not `qm_render` from `quickmenu.c`, which draws the 3-item selectable menu): draws four fixed-position icon+text groups left-to-right across a thin top bar — battery percentage + a charging glyph when applicable, WiFi connected/disconnected glyph, volume percentage, brightness percentage.
- Reuses `qm_settings.c`'s existing `qm_wifi_get()`, `qm_volume_get()`, `qm_brightness_get()` — same source of truth as the quickmenu overlay and Batocera's own tools, no duplicated logic.
- New `qm_battery_get(int *percent, int *charging)` in `qm_settings.c`, reading `/sys/class/power_supply/battery/capacity` and `/sys/class/power_supply/battery/status` (the exact path confirmed working this session via SSH: `Discharging`/`Charging` string values, compare against `"Charging"`). Returns -1 on any read failure, same error convention as the other `qm_*_get` functions.
- No `zwlr_layer_surface_v1` keyboard interactivity, no evdev open at all — this file has no input module and no event loop beyond "redraw if changed, sleep."
- Colors: reuses the same `QM_COLOR_*` constants from `quickmenu.h` (already matching Batocera's black/dark-red palette as of today's change) — the bar looks like a strip of the same design language, not a separate palette to maintain.

### Buildroot package

New `package/batocera/utils/circuitsword-statusbar/` directory, following the exact same package shape as `circuitsword-quickmenu/` (`Config.in`, `circuitsword-statusbar.mk`, dependencies on `wayland` + `wayland-protocols`). Depends on `circuitsword-quickmenu`'s package only informally (shares source files at the Makefile level, e.g. compiling `qm_font.c`/`qm_settings.c` from the neighboring package directory) — exact dependency wiring (symlink vs. relative path vs. small shared static lib) is an implementation-plan-level decision, not a design-level one; whichever keeps the Buildroot package graph simplest.

## Data Flow

Game start → `statusbar_thread` detects `retroarch_running()` → launches `circuitsword-statusbar` → connects to `labwc`, creates top-anchored overlay-layer surface → every 2s: reads battery/WiFi/volume/brightness, redraws only if any value changed since the last read → game exit → `statusbar_thread` detects `retroarch_running()` went False → terminates the process.

## Error Handling

- **Cannot connect to `labwc` / layer-surface creation fails**: `circuitsword-statusbar` exits immediately, non-zero. No visible glitch — nothing was drawn. `statusbar_thread` sees the process as not running and retries on its next poll (which will keep failing the same way until whatever blocked the connection is fixed — this is a self-announcing failure via the daemon's existing stderr logging convention, not a silent one).
- **Process crashes mid-session**: self-heals on the next `statusbar_thread` poll tick (relaunch), as described above.
- **Battery sysfs read fails** (`qm_battery_get` returns -1): bar keeps showing the last successfully read value, same "hold last-known-good" convention already used by the daemon's own `read_battery_percent()`.
- **No watchdog/timeout**: unlike quickmenu, there is no interactive session to time out — the process is either alive (keep it) or dead (relaunch it).

## Testing

**Verifiable off-device**: `qm_battery_get()`'s parsing logic (given fixture file contents, host-unit-testable the same way `qm_font.c` is); the new render function's pure drawing output (host-testable via the same `qm_fb`-malloc pattern as `tests/test_qm_font.c`); `statusbar_thread`'s launch/kill/relaunch-on-crash logic (host-unit-testable in isolation, same monkeypatching pattern as `tests/test_quickmenu_logic.py`).

**Needs on-device validation**: the bar actually composites above a running fullscreen RetroArch client via a TOP-anchored (not full-screen-anchored) overlay-layer surface — related to, but not identical to, the full-screen case `circuitsword-quickmenu` already proved, so must be confirmed separately, not assumed; visual layout/sizing on the real DPI panel; z-ordering when `circuitsword-quickmenu` opens on top of an already-visible status bar (both on the overlay layer — expected to just work via full-screen occlusion, but unverified); WiFi connected/disconnected icon accuracy against real network state changes; any visible flicker on the 2s redraw-on-change cadence during actual gameplay.

## Out of Scope (this phase)

- WiFi signal-strength bars (original had multiple signal-strength icon states) — this design shows connected/disconnected only, reusing the existing boolean `qm_wifi_get()`. Signal strength would need a new primitive (RSSI via NetworkManager/`iw`) not currently present anywhere in this project.
- Mute icon (the original bar had one) — not requested by the user for this version; volume already covers the same information (0% reads as effectively muted).
- Any visibility toggle, auto-hide, or fade behavior — the bar is unconditionally visible for the entire duration a game is running, matching the original and the user's explicit choice.
- Showing the bar inside EmulationStation itself (outside a running game) — explicitly scoped to "while an emulator is running" per the user's clarification; ES already has its own native status indicators.
- Any change to `circuitsword-quickmenu` itself — the two overlays are independent; this design adds a new component, it does not modify the existing one beyond sharing already-existing, unmodified source files (`qm_font.c`, `qm_settings.c`, the vendored protocol headers).
