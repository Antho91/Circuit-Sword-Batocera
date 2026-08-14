# Circuit-Sword on Batocera — design

Status: approved for planning (2026-07-28)

## Goal

Replace the current RetroPie-based Circuit-Sword image with a Batocera-based
image, as a **from-scratch replacement project** (not a dual-boot, not an
in-place migration of the existing repo). Preserve all hardware features
currently working on RetroPie, minimize custom code by reusing Batocera-native
mechanisms wherever a real equivalent exists, and avoid shipping half-finished
features — phased delivery is fine, silently-dropped features are not.

## Hardware target

CM3 (BCM2837, aarch64, 1 GB RAM) Circuit-Sword only, same scope as the current
RetroPie build. No multi-board/variant support (e.g. a hypothetical "Circuit
Shield") in scope for this project.

## Prior art considered

- **circuix-sword** (NixOS, github.com/jecaro/circuix-sword) — active project,
  MIT-licensed, covers WiFi/input/DPI/audio/battery already. Used here as a
  reference for what's achievable, not as the base — this project targets
  Batocera specifically. Its open gaps (no ES-equivalent frontend, no HDMI
  out, WiFi-after-reboot instability) map to points below.
- circuix-sword uses the RTL8723BS **in-tree** kernel driver (rtl8723bs has
  been mainline since Linux 4.12) rather than DKMS + `compat.h` shims — this
  removes the entire class of kernel-update breakage the current CS build
  works around (see FUTURE.md "WiFi driver: switch to the maintained rtw88
  port"). Whether Batocera's rpi3 target does the same needs confirming in
  Phase 0.

## Architecture

Own **`circuit-sword` br2-external tree**, built against a pinned Batocera
version. Batocera itself stays unmodified/upstream-trackable; all
Circuit-Sword-specific board config, kernel module tuning, packages, and
overlay files live in the external tree. Same philosophy as the current
Docker-based assembler, translated to Buildroot's native extension mechanism
(`BR2_EXTERNAL`).

Rejected alternative: hard-forking Batocera's repo directly — easier to start,
but loses the ability to track upstream Batocera updates over time.

## Subsystems

### Boot & base hardware — largely direct reuse

- **Board base**: Batocera's existing `rpi3` target (same SoC family as CM3;
  no new SoC port needed).
- **DPI display** (320×240 and 640×480 panels): existing `config.txt`
  DPI-overlay lines carry over — Batocera uses the same RPi firmware boot
  partition / config.txt mechanism as RetroPie.
- **Safe shutdown** (GPIO 39): `dtoverlay=gpio-poweroff` is a known, supported
  Batocera pattern — direct reuse.
- **Audio** (USB C-Media, stock `snd-usb-audio`): works on any modern kernel,
  no changes needed.
- **Fan** (2-wire blower, on/off only, never PWM): control logic lives in the
  hardware daemon (see below), not the boot config — no OS-level change.

### WiFi (RTL8723BS) + Bluetooth

- Same `dtoverlay=sdio` + UART BT overlay as today.
- **Stability fix carries over unchanged**: `rtw_power_mgnt=0 rtw_ips_mode=0
  rtw_bw_mode=0` (modprobe options, disables HT40 + power-save — the
  documented fix for "intermittent disconnects, worse on 40MHz channels") plus
  NetworkManager/wpa_supplicant powersave-off. These are module-parameter /
  network-stack settings, independent of whether the driver is DKMS or
  in-tree — shipped as a config file in the external tree.
- **RESOLVED (Phase 0)**: Batocera 43.1 (kernel 6.12.62) ships the mainline
  rtw88 (mac80211) driver family (`rtw_8723d/cs/ds/du/x.ko`) but **no
  `rtw_8723bs.ko`** — confirmed by `find` on a real device (see
  `docs/superpowers/plans/findings/PHASE0-FINDINGS.md`, Task 5). SDIO hardware
  detection works (`dtoverlay=sdio` correctly surfaces the chip on the mmc
  bus), but there is no matching driver out of the box.
  **Better path found** (by cross-checking `nixos-reference`): Batocera's
  bcm2837 kernel is built from `raspberrypi/linux`, which already contains
  `drivers/staging/rtl8723bs/` (`CONFIG_RTL8723BS`, module `r8723bs`) — the
  same legacy staging driver both the original RetroPie build and
  circuix-sword (NixOS) already use successfully on this exact chip.
  Firmware (`rtl8723bs_nic.bin`/`rtl8723bs_bt.bin`) is already present on
  the stock Batocera image. Fix: add a kernel config fragment
  (`CONFIG_RTL8723BS=m`) via Batocera's existing
  `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES` mechanism — no out-of-tree
  Buildroot package or third-party fork needed. Still requires a real image
  rebuild (kernel config change), but is simpler and lower-risk than
  packaging `MocLG/rtw88-rtl8723bs`.
- `rtk_hciattach` (Bluetooth attach, UART-based chip) is user-space — built as
  an ordinary package in the external tree. **Gap to verify**: Batocera's
  built-in Bluetooth pairing UI generally assumes a USB BT dongle; needs
  confirming it works unmodified with a UART-attached chip.

### Update mechanism

**Batocera's native OTA update replaces `cs-update`.** Batocera downloads a
new SquashFS boot image and replaces the boot partition wholesale (kernel,
drivers, everything), leaving the userdata partition (ROMs, configs, saves)
intact — triggered manually from the ES GUI or via SSH (`batocera-upgrade`).
This is strictly more capable than `cs-update`, which explicitly could NOT
touch the kernel/WiFi driver/network-config (a full reflash was required for
those) — that limitation disappears. Stays user-triggered, so the "never
auto-update" rule is preserved by construction (Batocera doesn't auto-update
either).

**Important caveat (found in Phase 0)**: this only holds if updates are
distributed from **our own rebuilt images** (Batocera version bumped and
rebuilt through `circuit-sword-external`). If the device ever pulls
Batocera's own stock/official update directly (batocera.org, unmodified),
that replaces our kernel with Batocera's un-forked one — losing the
`CONFIG_RTL8723BS` kernel config fragment (WiFi) and any other
Circuit-Sword-specific kernel/rootfs changes. The update channel presented
to the user must point at our own published images, not upstream Batocera's,
or WiFi (and anything else patched at the kernel/rootfs level) breaks on
next update.

**Gap to verify**: `cs-update` had automatic backup/rollback if a new cs-hud
build failed to start post-update. Need to confirm Batocera's OTA update has
equivalent protection (checksum/rollback) so a bad update can't brick a
non-technical user's device.

### Hardware daemon (fan / battery / shutdown / volume / backlight)

The core of `cs-hud`'s `poll_thread` (in `main.c`, backed by `hardware.c`) —
MODE-button detection, power-switch (GPIO 37) → safe shutdown, battery
voltage polling → `power_supply` sysfs + low-battery warning beep + critical
auto-shutdown with game auto-save, fan on/off thermal control, board-volume →
ALSA mirroring, brightness/backlight incl. HDMI-dock dim logic — is OS-agnostic
C talking to serial/GPIO/ALSA/sysfs. **Ported essentially unchanged**:
recompiled against Batocera's toolchain, shipped as a systemd/init service via
the external tree. This is safety-critical logic (thermal, battery,
safe-power-off) with no Batocera equivalent — stays custom regardless.

**Open design choice**: expose brightness as a proper Linux
`/sys/class/backlight/...` device (small kernel-side shim) instead of only
via the custom quick-menu, so Batocera's own ES brightness slider works
out of the box. Trades a bit of extra Phase-3 work for less custom UI
long-term. Decision deferred to Phase 3 planning.

### Input (Arduino Leonardo)

**No change needed.** Confirmed by reading `kite-arduino/CS_FIRMWARE/INPUT.ino`:
buttons/D-pad go over the Arduino's native USB-HID Gamepad library
(`Gamepad.press()` / `dPad1()`), a standard joystick device as far as the OS
is concerned — Batocera's `evmapy` will map it like any generic controller.
This is separate from the serial CDC channel (`/dev/ttyACM0`) the hardware
daemon uses for MODE button / battery / brightness / volume, which are not
part of the HID report.

### Quick-menu overlay (in-game, via MODE button)

The existing overlay (`menu.c`) has only 3 items: WiFi toggle, Volume,
Brightness.

- **Volume**: RetroArch's own Quick Menu already covers this — drop the
  custom volume item.
- **Brightness**: no Batocera equivalent (pure hardware PWM via Arduino
  EEPROM) — stays custom, unless the backlight-class shim above is built, in
  which case it might move into Batocera's native settings instead.
- **WiFi toggle mid-game**: no Batocera equivalent (ES network settings
  aren't reachable while a game is running). Stays custom.
- Net effect: the overlay shrinks from 3 items to 1 genuinely new one
  (WiFi), but all 3 land in one unified menu for convenience.
- **CORRECTED 2026-08-06**: the SDL2/KMSDRM approach mentioned above (and
  the VT-switch design that replaced it early in Phase 4) both assumed no
  compositor exists on this hardware, carried over from CLAUDE.md hard
  rule #6. On-device investigation during Phase 4 disproved this for
  Batocera: this tree runs `labwc` (a Wayland compositor) continuously,
  and RetroArch itself is a Wayland client of it. The actual design is a
  `wlr-layer-shell-unstable-v1` overlay — a real compositor-drawn overlay,
  no VT-switch, no display hand-off. See
  `docs/superpowers/specs/2026-08-06-quickmenu-design.md` and the
  corrected CLAUDE.md hard rule #6.

### Dropped / superseded, no porting needed

- **Samba ROM shares**: Batocera ships this natively — nothing to build.
- First-boot partition resize: standard behavior for this class of embedded
  distro; expected to be native to Batocera (confirm in Phase 0, not expected
  to need custom work).

### Open gaps needing a decision or investigation (tracked, not silently dropped)

1. Brightness: custom-only vs. Linux backlight-class shim (see above).
2. `cs-configure.py` (Kite's original Python2 serial config tool) — purpose
   unclear: end-user tool or factory/calibration-only? Needs clarifying before
   deciding whether it needs a Batocera port at all.
3. Bluetooth pairing UI compatibility with a UART-attached (not USB) BT chip.
4. Python runtime availability on Batocera's minimal image — `cs-configure.py`
   / `cs-tester.py` are Python; may need Python added as a runtime dependency,
   or a rewrite in shell/C, if they need to keep working.
5. SSH-on / default-credentials policy — current CS hard rule #7 treats this
   as a deliberate, discussed trade-off. Batocera's own defaults may differ;
   must be an explicit decision, not an inherited default.
6. OTA update rollback safety — needs verifying before it can be trusted to
   fully replace `cs-update`'s backup/rollback behavior.
7. ~~Kernel version compatibility~~ — **RESOLVED (Phase 0)**: Batocera 43.1
   ships kernel 6.12.62, which predates both known API breaks (6.15
   timer renames, 6.18 cfg80211 changes) — the old `compat.h` shims aren't
   needed for this version. Re-check whenever Batocera bumps its kernel.

## Phasing

1. **Phase 0 — Research spike**: boot Batocera's rpi3 image on real CM3
   hardware; resolve the in-tree-vs-out-of-tree WiFi driver question; check
   kernel version compatibility; confirm first-boot partition resize is
   native.
2. **Phase 1 — Core boot**: DPI display, Arduino input via evmapy, audio, safe
   shutdown. Target: playable without WiFi/BT/hardware-daemon.
3. **Phase 2 — Connectivity**: WiFi (with the stability fix) + Bluetooth.
4. **Phase 3 — Hardware daemon**: fan control, battery monitoring +
   `power_supply`, brightness/backlight (incl. the backlight-class decision).
5. **Phase 4 — Quick-menu overlay**: WiFi toggle + Volume + Brightness
   mid-game, via the Arduino MODE button. **Feature-complete as of
   2026-08-10, pending on-device validation.** Original design assumed no
   compositor exists on this hardware (CLAUDE.md hard rule #6, carried over
   from RetroPie) and planned a VT-switch + raw-libdrm hand-off; an
   on-device spike during implementation (2026-08-06) proved this false for
   Batocera — this tree runs `labwc` (a Wayland compositor) continuously,
   and RetroArch is itself a Wayland client of it. Redesigned around a real
   `wlr-layer-shell-unstable-v1` overlay instead — see
   `docs/superpowers/specs/2026-08-06-quickmenu-design.md` and
   CLAUDE.md hard rule #6 (corrected). The abandoned VT-switch plan and its
   findings remain at `docs/superpowers/plans/2026-08-06-phase4-quickmenu.md`
   / `PHASE4-QUICKMENU-FINDINGS.md` for reference. Beyond the original
   3-item scope, this phase grew to also include: a persistent in-game
   status bar (battery/WiFi/volume/brightness, always visible, not just on
   MODE press — `2026-08-10-persistent-statusbar-design.md`), a MODE+Up/Down
   hardware volume combo bridged to Batocera's software volume
   (`2026-08-10-mode-hold-volume-combo-design.md`), a full replacement of
   both overlays' text/pixel-art with real vector-sourced icons, a sharp
   SVG title wordmark, and colored button badges
   (`2026-08-10-real-overlay-icons-design.md`), and an 80%-translucent
   status-bar background so the game stays partially visible underneath it
   (`2026-08-10-translucent-statusbar-design.md`). A combined image with
   all of the above was built and its contents verified directly against
   the build artifacts on 2026-08-10; on-device testing (icon legibility,
   layout on real panels, and whether the translucent status bar actually
   renders as translucent under `labwc` — genuinely untested ground per
   `PHASE4-TRANSLUCENT-STATUSBAR-FINDINGS.md`) is the one remaining item
   before this phase can be marked fully done.
6. **Phase 5 — Polish & update flow**: OTA update tested end-to-end
   (incl. rollback-safety gap above; the manual-update procedure and
   `updates.enabled=0` default were substantially done during Phase 3's
   session as a tangent), boot splash (Batocera has this natively — likely
   free).
7. **Phase 6 — Hardware-daemon refinements + joystick calibration** (queued
   after Phase 4, added 2026-08-06): decouple charging-status detection
   from the 30s battery-percentage poll (GPIO read is instant, unlike the
   Arduino serial round-trip for voltage) — **DONE 2026-08-11**, see
   `docs/superpowers/specs/2026-08-11-charging-status-decouple-design.md`.
   Port the RetroPie original's low-battery warning (15%, beep, hysteresis
   re-arm at 20%) and auto-shutdown (5% after 3 consecutive low polls,
   guards against voltage-sag false shutdown) behavior — **DECLINED
   2026-08-11**: on-device code investigation found Batocera already ships
   a generic, always-on `batocera-battery-checker` daemon (unconditionally
   selected for every board via `batocera-system`'s `Config.in`, 5s poll)
   that shows a red flash-screen warning at ≤30%/≤10%/≤5% battery and
   already works against `circuitsword_battery`'s `capacity` sysfs
   attribute — judged sufficient as-is, no beep or extra warning UI added.
   Auto-shutdown was explicitly declined too: Batocera's only native
   auto-shutdown-on-low-battery logic lives in a separate background
   monitor that only runs during "fake suspend" (triggered by a short
   power-button press), not during active gameplay — so there is still no
   safety net if the battery hits 0% while playing, a real gap, but the
   user chose not to close it now. Re-propose only if this becomes a real
   problem in practice (revisit if the board is ever found dead/corrupted
   after running flat). A related, smaller gap was also found and
   similarly left open: `batocera-battery-checker`'s AC-vs-battery
   detection reads `/sys/class/power_supply/*/online`, a property
   `circuitsword_battery`'s kernel module doesn't expose (only
   PRESENT/STATUS/CAPACITY/TECHNOLOGY/SCOPE) — likely means Batocera
   always assumes battery power (affecting its CPU performance-mode
   switch, `batocera-power-mode battery` vs `ac`) even when charging;
   `GPIO_PIN_POWER_GOOD` (GPIO 38) already exists as an unused constant in
   `rpi-circuitsword.py` and would be the natural source for a future fix.
   A settings surface for hardware-daemon tunables
   (fan threshold, low-battery thresholds, currently SSH/config-file-only
   via `/userdata/system/configs/circuitsword.conf` — whether that becomes
   a real EmulationStation GUI screen or stays config-file-only is a design
   question for that phase); and `cs-configure.py`-equivalent joystick
   calibration (confirmed during scoping this isn't covered by Batocera's
   generic "Configure a Controller" — the Arduino firmware has its own
   ADC-based calibration routine, `CALIBTIME`/`DEADZONE` in
   `cs-firmware/CS_FIRMWARE/config.h`, that RetroPie's `cs-configure.py`
   drove over serial); and ~~a persistent battery-status indicator visible
   *during actual gameplay*~~ — **RESOLVED, superseded by Phase 4**: this
   item was scoped (2026-08-06) around a `gfx_widget_battery.c` RetroArch
   render-pass widget on the assumption that no compositor-level overlay
   existed. That assumption was itself corrected in Phase 4 (see CLAUDE.md
   hard rule #6) — `circuitsword-statusbar`, the persistent Wayland
   layer-shell overlay built in Phase 4
   (`2026-08-10-persistent-statusbar-design.md`), already renders battery
   (plus WiFi/volume) continuously during gameplay via `sb_render.c`, not
   just when RetroArch's own Quick Menu is open. No RetroArch source patch
   needed after all; this project still has no RetroArch source patches as
   of this writing, and none are pending.

## Out of scope

- Dual-boot / keeping RetroPie alongside Batocera.
- Supporting hardware variants beyond the current CM3 Circuit-Sword.
- Automatic/background updates (explicitly rejected, matches existing hard
  rule).
