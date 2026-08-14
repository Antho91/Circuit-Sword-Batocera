# Circuit-Sword In-Game Quick Menu — Design (Phase 4)

> **Revision note (2026-08-06, same day):** This design was originally written around a VT-switch + raw libdrm approach, based on the assumption that this hardware's KMS stack has no compositor (CLAUDE.md hard rule #6). An on-device spike (see "Corrected assumption" below) proved that assumption **false** for this Batocera 43.1 tree. This document is a full rewrite of the Architecture, Components, Data Flow, and Error Handling sections to reflect the real mechanism: a Wayland layer-shell overlay. The Context, scope, and settings-primitives sections are largely unchanged from the original. The abandoned libdrm/VT-switch implementation plan and its findings are preserved at `docs/superpowers/plans/2026-08-06-phase4-quickmenu.md` and `docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` for reference.

## Corrected assumption: this hardware runs a real Wayland compositor

CLAUDE.md hard rule #6 ("No DispmanX/overlay layer exists on this hardware's KMS stack") was carried over from the RetroPie build, where it was true — that build really did use bare KMS/DRM with no compositor. It does **not** hold for this Batocera 43.1 tree.

On-device investigation (SDD execution, Task 2 of the abandoned plan) found:

- This Batocera build runs **`labwc`** (a wlroots-based Wayland compositor, version 0.9.3) as EmulationStation's persistent windowing backend for the whole ES session. `package/batocera/core/batocera-resolution/batocera-resolution.mk:33` sets `BATOCERA_SCRIPT_TYPE=wayland-labwc` unconditionally — not board-specific, this is the default across the whole Batocera 43.x tree.
- Nothing in `package/batocera/` kills or stops `labwc` before an emulator launches.
- Confirmed live, with a game actually running: RetroArch itself is a **Wayland client**, not a DRM master. `/proc/<retroarch-pid>/environ` shows `XDG_SESSION_TYPE=wayland` and `WAYLAND_DISPLAY=wayland-0`; its open file descriptors include `/dev/dri/renderD128` (the GPU render node, for GL rendering) but **not** `/dev/dri/card0` (the KMS/modesetting node — that stays with `labwc`, continuously).
- `labwc` 0.9.3, as built in this exact tree, compiles and ships working support for the **`wlr-layer-shell-unstable-v1`** protocol — confirmed by inspecting the actual build output (`build/labwc.p/wlr-layer-shell-unstable-v1-protocol.h` etc.) — and even ships its own reference client that uses it: `clients/labnag.c`, labwc's confirmation-dialog helper, which draws a window on the topmost ("overlay") layer above everything else, including fullscreen clients.

This flips the constraint this design was originally built around: a **real overlay** is possible on this hardware, drawn by the compositor itself on top of the running game, with no display hand-off, no VT switching, and no risk of a blank-screen failure mode.

**CLAUDE.md hard rule #6 is corrected accordingly** (see the note added there): it applies only to the historical RetroPie build, not to this Batocera port.

## Context

The original RetroPie build's `cs-hud` had an in-game overlay (`menu.c`), reachable via the Arduino's MODE button, with 3 items: WiFi toggle, Volume, Brightness. Two of the original three items are already resolved elsewhere, for free:

- **Volume**: RetroArch's own Quick Menu already exposes this.
- **Brightness**: Phase 3 shipped a real Linux `backlight`-class kernel module (`circuitsword-backlight`), so Batocera's native EmulationStation brightness slider already works.
- **Battery status** (not part of the original 3): also already free via RetroArch's own Quick Menu, which reads `/sys/class/power_supply` the same way ES does.

Despite Volume and Brightness already working elsewhere, the user wants a single unified in-game menu with all three (WiFi, Volume, Brightness), reusing the same underlying primitives each already uses — not reimplementing them. WiFi toggle remains the one item with no existing in-game equivalent.

## Architecture

`circuitsword-quickmenu` is a small, standalone Wayland client. It connects directly to `labwc` (the same compositor RetroArch and EmulationStation are already clients of), requests a surface on the **overlay layer** via `wlr-layer-shell-unstable-v1` — the topmost compositing layer, drawn above even fullscreen clients — and draws into it with a raw `wl_shm` pixel buffer. No display hand-off of any kind: `labwc` never stops being the DRM master, and RetroArch's own Wayland surface is untouched underneath.

```
RetroArch running (Wayland client of labwc, game visible)
  │  MODE pressed (daemon detects via existing CMD_GET_STATUS poll)
  ▼
daemon sends PAUSE_TOGGLE to RetroArch's local network command port
  ▼
daemon launches circuitsword-quickmenu
  ▼
circuitsword-quickmenu requests an overlay-layer surface from labwc,
draws the menu (full-screen, opaque) into a wl_shm buffer, reads the
Arduino joystick directly via evdev (EVIOCGRAB, so RetroArch's still-
focused window doesn't also see menu navigation)
  │  B pressed, or a second MODE press relayed as SIGTERM
  ▼
circuitsword-quickmenu destroys its layer surface and exits
  ▼
daemon sends PAUSE_TOGGLE again — RetroArch resumes, screen already
showing the game (labwc never stopped compositing it)
```

Only ever one compositor (`labwc`) doing scanout, and it composites at most two Wayland surfaces (RetroArch's, and briefly ours). This is architecturally simpler than the abandoned VT-switch design: no VT state to track, no "could not regain DRM master" failure mode, no blank-screen risk if something goes wrong — worst case, `circuitsword-quickmenu` fails to create its surface and exits immediately, and the user never sees anything change.

**Chosen visual style:** full-screen, opaque, matching the original `cs-hud` look — not a smaller translucent panel. The game is paused underneath anyway, so there's nothing to see through, and this reuses the menu's rendering logic unchanged from the original design.

**Pausing:** the game is still paused (via the already-proven `PAUSE_TOGGLE` UDP command) while the menu is open, even though the overlay mechanism itself doesn't require it — chosen for predictability (no ambiguity about where input goes, no game audio/animation running unseen behind an opaque menu).

## Components

### `rpi-circuitsword.py` (extended)

Owns the MODE button end-to-end, as before — it already polls `CMD_GET_STATUS` continuously via serial.

- Detects MODE press only while RetroArch is actually running (checked via `/proc` scan — no `pgrep` on this image) — no-op from ES itself, since WiFi/Volume/Brightness are already reachable there.
- Debounces the MODE press (~50-100ms, tuned on-device).
- Tracks a busy/in-transition flag covering the full open/close sequence; ignores MODE presses that arrive mid-transition.
- **Open sequence**: confirm RetroArch's network command port answers (reachability probe, so we never pause blindly) → `PAUSE_TOGGLE` → launch `circuitsword-quickmenu`, wait for it to exit. **No VT switch step** — this is the key simplification versus the original design.
- **Close sequence** (triggered by `circuitsword-quickmenu` exiting, whether via its own B/back handling, a repeat MODE press relayed as SIGTERM, or a crash): `PAUSE_TOGGLE` again.
- **Watchdog**: if `circuitsword-quickmenu` doesn't exit within 5 seconds of a close signal (same 5s convention as this daemon's `main()` thread-join timeout) — or hangs/crashes — kill it and resume the game regardless. There is no VT to restore, so the failure surface here is strictly smaller than before: worst case is a killed helper process, never a blank screen.

### `circuitsword-quickmenu` (new)

Standalone C program, written directly against raw `libwayland-client` — no toolkit (no cairo/pango/glib, unlike labwc's own `labnag.c` reference client), matching this project's low-level, minimal-dependency preference.

- Connects to the compositor, binds `wl_compositor`, `wl_shm`, and `zwlr_layer_shell_v1` via `wl_registry`.
- Creates a `wl_surface`, requests a `zwlr_layer_surface_v1` on **layer = overlay**, anchored to all four edges (full screen), `keyboard_interactivity = none` (Wayland keyboard focus is irrelevant — joysticks never route through Wayland at all, so evdev is the only path regardless of compositor).
- Draws 3 items — **WiFi** (on/off), **Volume**, **Brightness** — full-screen, opaque, plus a bottom hint line ("A: SELECT   B: BACK   LEFT/RIGHT: ADJUST"), into a `wl_shm`-backed 32bpp XRGB8888 buffer. Rendering code (5×7 bitmap font, layout, redraw-on-change) is carried over unchanged from the abandoned plan's pure/portable drawing layer — only the backing buffer changes, from a KMS dumb buffer to `wl_shm` memory.
- Navigation via the Arduino's D-pad/A/B, read directly via evdev with `EVIOCGRAB` (unchanged from the original design — this was never dependent on the display mechanism).
- Each item reads/writes the same primitive its existing counterpart already uses:
  - **WiFi**: `batocera-settings-set wifi.enabled 0/1` then `/etc/init.d/S08connman reload` — the same path `batocera-wifi`'s own enable/disable case uses.
  - **Volume**: `batocera-audio getSystemVolume` / `setSystemVolume N` — this board is PipeWire-backed (`pactl set-sink-volume`), not ALSA `amixer`; `batocera-audio` is the same script ES itself calls.
  - **Brightness**: `/sys/class/backlight/circuitsword-backlight/brightness`, scaled by `max_brightness` the same way `batocera-brightness` does.
- Exits on B (back) or a repeat MODE signal relayed as SIGTERM from the daemon.

### New Buildroot package pieces specific to Wayland

- The `wlr-layer-shell-unstable-v1.xml` protocol definition is **not** part of the standard `wayland-protocols` package — it originates from wlroots/labwc's own protocol staging. It must be vendored into `circuitsword-quickmenu`'s own package directory (copied from labwc's fetched source, `protocols/wlr-layer-shell-unstable-v1.xml`), the same way other Wayland clients in the wider ecosystem vendor this file.
- Protocol C sources and client headers are generated at build time using `$(HOST_DIR)/bin/wayland-scanner` (already built as a host tool in this tree, since `labwc` itself depends on it) — `wayland-scanner private-code <xml> out.c` and `wayland-scanner client-header <xml> out.h`, for both `xdg-shell.xml` (from the `wayland-protocols` package, already a Buildroot dependency) and the vendored `wlr-layer-shell-unstable-v1.xml`.
- Package dependencies: `wayland` (for `libwayland-client`), `wayland-protocols` (for `xdg-shell.xml` and the host `wayland-scanner`), plus the vendored layer-shell XML.

## Data Flow

MODE press → daemon confirms RetroArch is running and no transition is in progress → reachability probe → `PAUSE_TOGGLE` → `circuitsword-quickmenu` launches, connects to `labwc`, creates its overlay-layer surface, reads current WiFi/volume/brightness state, draws, then reacts live to evdev input → exits (B or repeat MODE) → `PAUSE_TOGGLE` → game resumes, already visible (nothing to switch back).

## Error Handling

- **RetroArch not running**: no-op, no menu.
- **RetroArch's command port unreachable**: abort before doing anything visible — never pause blindly, never launch the menu if we can't guarantee we can resume.
- **`circuitsword-quickmenu` can't connect to `labwc`, or the layer-surface request fails**: exits immediately with a non-zero code; the daemon sees this as "could not run" and sends `PAUSE_TOGGLE` to resume right away. No visible glitch — nothing was ever drawn.
- **`circuitsword-quickmenu` crashes or hangs after its surface is up**: daemon's 5-second watchdog kills it and resumes regardless. Worst case here is strictly better than the abandoned design: there is no VT to fail to restore and no DRM master to fail to reclaim — `labwc` keeps compositing RetroArch's surface the entire time regardless of what our helper process does.
- **MODE press mid-transition**: ignored (busy flag), preventing an open/close race.

## Testing

**Verifiable off-device**: `circuitsword-quickmenu` compiles cleanly against Buildroot's `libwayland-client` and the generated protocol headers; the pure rendering/font code is host-unit-testable exactly as in the abandoned plan (backing buffer is a plain malloc'd `qm_fb`, agnostic to Wayland vs. libdrm); `rpi-circuitsword.py`'s debounce/busy-flag logic is unit-testable in isolation.

**Needs on-device validation, not claimed as done otherwise**: whether `labwc` actually composites the overlay-layer surface above a running fullscreen RetroArch client (this is the one load-bearing assumption of this whole redesign and should be spiked early, the same way the original VT-switch assumption was spiked — cheaply, before writing the full implementation); `EVIOCGRAB` behavior against a paused RetroArch that's still nominally focused; transition timing/any visible flicker on surface creation and destruction; debounce tuning for the MODE button.

## Out of Scope (this phase)

- Full WiFi network selection/password entry (on/off toggle of the already-configured network only).
- Any change to RetroArch's or ES's own existing Volume/Brightness/Battery display paths.
- Joystick calibration, low-battery warning/shutdown behavior, and any settings-menu-in-ES work — all deferred to Phase 6 (see the main design doc's phasing section).
