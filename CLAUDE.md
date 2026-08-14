# Circuit Sword on Batocera — Claude context

Guidance for AI coding agents working on this repository.

## What this project is

A ground-up replacement of the Circuit-Sword's current RetroPie-based image
with a **Batocera**-based image, targeting the same hardware: a Raspberry Pi
**Compute Module 3** (CM3, BCM2837, arm64, 1 GB RAM) Game Boy mod kit with a
DPI display, RTL8723BS WiFi/Bluetooth, and an on-board Arduino Leonardo for
controls/battery/backlight.

This is a **from-scratch replacement**, not a migration or dual-boot of the
existing RetroPie build. See the design doc for full rationale and scope:

- **Design**: `docs/superpowers/specs/2026-07-28-batocera-port-design.md`

The project's original RetroPie-based build (`Retropie_source/`) and an
independent NixOS reference project (`nixos-reference/`,
[jecaro/circuix-sword](https://github.com/jecaro/circuix-sword)) were kept as
read-only reference early in the project and removed once the Batocera port
had absorbed everything needed from them — the WiFi stability fix, DPI/Arduino
protocol details, and hardware-daemon design are already captured directly in
this file and in `docs/superpowers/specs/`; `nixos-reference/` can be
re-cloned from its GitHub URL above if needed again, but `Retropie_source/`
had no remote and is not recoverable.

## Directory layout

- `docs/superpowers/specs/` — design docs (this project's spec lives here).
- `batocera-build/` — the actual buildable tree: pinned commits, patches,
  and scripts. See `BUILDING.md` for the day-to-day build workflow.

## Hard rules — carried over from the RetroPie build, still apply

These came from real hardware constraints/incidents on this exact board, not
from RetroPie specifics — they apply regardless of OS:

1. **Never PWM the fan.** It's a 2-wire blower — on/off only, temperature-based.
2. **`-j2` max for on-device builds.** 1 GB RAM; `-j3+` OOMs even with zram.
3. **Updates stay manual/user-triggered.** Never add auto-update behavior.
4. **SSH-on / default-credentials policy needs an explicit decision**, not an
   inherited default — see open gap #5 in the design doc.
5. **WiFi (RTL8723BS) stability fix must carry over**:
   `rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0` (disables power-save + caps
   to 20MHz — HT40 causes intermittent drops on this chip) plus WiFi
   power-save disabled at the network-manager level. Originally sourced from
   the (since-removed) RetroPie build's `settings/r8723bs.conf` and
   `settings/wifi-powersave-off.conf`; the values above are the complete fix.
6. **CORRECTED 2026-08-06 — this rule was true for RetroPie, is FALSE for
   Batocera.** Originally: "No DispmanX/overlay layer exists on this
   hardware's KMS stack, a HUD cannot draw on top of a running emulator."
   That held for the old RetroPie build (bare KMS/DRM, no compositor). It
   does **not** hold for this Batocera 43.1 tree: on-device investigation
   (Phase 4) found this build runs `labwc` (a wlroots-based Wayland
   compositor) as EmulationStation's persistent windowing backend for the
   whole session (`batocera-resolution.mk:33`,
   `BATOCERA_SCRIPT_TYPE=wayland-labwc`, unconditional across the tree,
   not board-specific), and RetroArch itself runs as a Wayland client of
   it (confirmed live: `WAYLAND_DISPLAY` set in its environment, holds
   `/dev/dri/renderD128` but never `/dev/dri/card0`). `labwc` 0.9.3 ships
   working `wlr-layer-shell-unstable-v1` support and its own reference
   overlay client (`clients/labnag.c`). A real in-game overlay IS possible
   on this hardware via that protocol — see
   `docs/superpowers/specs/2026-08-06-quickmenu-design.md` for the design
   this enabled. Do not assume "no overlay" for anything Batocera-side
   without re-checking; the constraint was OS/compositor-specific, not a
   hardware limitation as originally believed.

7. **A `make bcm2837-build` (full image build) does NOT automatically pick up
   edited source files in already-built packages — added 2026-08-10.**
   Buildroot's incremental build only reacts to a package's `.stamp_built`
   being absent; once a package (including plain-copy/no-compile ones like
   `rpigpioswitch`, `batocera-system`, or `batocera-emulationstation`'s
   data files) has been built once in the persistent Docker output volume,
   editing its source files on disk and re-running a full image build
   silently ships the STALE version — confirmed on real hardware: a full
   rebuild+reflash today still shipped a `rpi-circuitsword.py` with none of
   that day's Python changes, and an `es_input.cfg` fix never took effect
   either, both traced back to this. **Any package whose source you edited
   must be explicitly force-refreshed before (or as well as) a full image
   build** — `bcm2837-pkg PKG=<name>-rebuild` if it has compiled output,
   or the cheaper `PKG=<name>-reinstall` if it's a plain-copy/config-file
   package (re-runs only `INSTALL_TARGET_CMDS`, not a full recompile).
   Verify the fix actually landed by inspecting the built artifact directly
   in the Docker volume (`docker run --rm -v batocera-output-bcm2837:/bcm2837
   alpine grep/strings ...`) — don't trust a clean build log alone.

## Testing & verification

There is no hardware in CI for this project either. Be explicit about what's
verified off-device (builds, shellcheck/lint, compilation) vs. what needs
on-device validation (DRM/KMS behavior, SDIO WiFi stability, Arduino serial
protocol, GPIO polarities, audio, first-boot flow) — flag it, don't claim it
works.

## Conventions

- Update the design doc's "open gaps" section when a gap is resolved or a new
  one is found — don't let decisions go untracked.
- Record *why* an idea was rejected or deferred so it doesn't get
  re-proposed — the original RetroPie build's `FUTURE.md` convention this
  followed.
