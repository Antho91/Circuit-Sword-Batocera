# Phase 0 Findings Log

Running log for the Batocera hardware bring-up spike. Each task appends its
own dated section. This is the working record referenced by the design doc's
"Open gaps" section — update that doc once a gap here is resolved.

## Task 1: Flash & first boot

- Image: `batocera-bcm2837-43.1-20260530.img.gz` (Batocera v43.1, built
  2026-05-30). Already downloaded by the user, stored in the project root.
- Note: Batocera names this image by **SoC** (`bcm2837`, shared by Pi3 and
  CM3) rather than by exact board — a good early signal for CM3 device-tree
  support, to be confirmed in Step 3 below.

- **Step 3 (CM3 dtb check) — CONFIRMED PRESENT.** Mounted the boot (FAT32)
  partition of the image on macOS (`hdiutil attach`, partition `disk4s1`,
  labeled `BATOCERA`). Found `bcm2710-rpi-cm3.dtb` alongside
  `bcm2710-rpi-3-b.dtb`, `bcm2710-rpi-3-b-plus.dtb`,
  `bcm2710-rpi-zero-2*.dtb`. No blocking finding — same dtb the current
  RetroPie build uses.
- `config.txt` reviewed: standard RPi-firmware format, no `device_tree=`
  override (firmware auto-selects from CM3 SoM OTP, as expected). Notably
  already has a `[DPI]` section stub with a commented DPI overlay example,
  and `enable_uart=1` (commented, labeled "required for retroflag") — both
  good signs this config format is already exercised for handheld/DPI use
  elsewhere in the Batocera ecosystem. `disable_splash=1` and
  `dtoverlay=vc4-kms-v3d` are on by default; `dtparam=audio=on` is on
  (will need `audio=off` per the Circuit-Sword's USB-audio-only setup, see
  Task 4).

- **Step 5 (first boot) — RESOLVED, boots successfully.** Initial attempts
  showed "no HDMI signal" (monitor stayed in standby) with both the
  CS-overlay-modified image AND a completely stock, freshly-flashed image
  (via Raspberry Pi Imager) — ruling out our config.txt additions and ruling
  out a bad flash as causes. Root cause: Batocera's default `config.txt` ships
  `#hdmi_force_hotplug=1` **commented out**. On this board's HDMI chain, the
  EDID handshake apparently fails/isn't detected, so the firmware never
  drives an HDMI signal at all. **Fix confirmed working**: uncommenting
  `hdmi_force_hotplug=1` and adding `hdmi_safe=1` (tested via
  `docs/superpowers/plans/reference/sd-config-hdmitest.txt`, a minimal diff over the
  stock config — no other CS overlays included, to isolate the variable).
  This is NOT a CM3/Batocera compatibility issue — it's a standard HDMI
  hotplug-detection quirk, fixed by a one-line config change. Board now
  **boots successfully to Batocera on the real Circuit-Sword CM3 hardware.**
  - Bonus findings from this successful boot: **audio works** (out of the
    box, no config yet) and **Arduino controls work reasonably well**
    already — consistent with the Task-4-area finding that the Arduino
    Leonardo presents as a standard USB-HID gamepad (see
    `kite-arduino/CS_FIRMWARE/INPUT.ino`), auto-mapped by Batocera without
    custom driver work.
  - **Action for the real config**: `hdmi_force_hotplug=1` + `hdmi_safe=1`
    must be added permanently to the Circuit-Sword `config.txt` block (not
    just this diagnostic test file) — fold into Task 4's full overlay block
    and into `circuit-sword-external/board/circuitsword/` in Task 8.

## Task 2: Console access — obtained via HDMI + USB keyboard

`F4`-to-console and `Ctrl+Alt+F2` VT-switch were unreliable/didn't work on
this build; a working root console was eventually reached on the device's
own display (confirmed via a photographed terminal session, DPI output
active at this point). Default credentials confirmed working (`root`).

Baseline captured (`uname -a`, `/etc/os-release`):
```
Linux BATOCERA 6.12.62-v8 #1 SMP PREEMPT Sat May 30 20:16:26 2026 aarch64 GNU/Linux
NAME=Batocera.linux
PRETTY_NAME="Batocera.linux 43.1"
VERSION=43.1
ID=buildroot
VERSION_ID=2024.11
```

## Task 4 (partial): DPI panel — CONFIRMED WORKING

Full `sd-config.txt` (HDMI hotplug fix + gpio-poweroff + audio=off + sdio +
uart0 + 640x480 DPI block) applied successfully after a mixup where an
earlier copy step accidentally wrote `sd-config-hdmitest.txt` (missing the
CS block) instead — always diff the on-card `config.txt` against the
intended source file after copying, don't assume the copy took. Once the
correct file was actually on the card: **DPI panel outputs the desktop at
640x480 (DRM connector `DPI-1`)**, confirmed via console output. `gpio-poweroff`
and `uart0` overlays did not throw errors either (no negative dmesg evidence
seen).

## Task 5 (in progress): WiFi driver strategy

`dmesg | grep -i "8723\|sdio\|mmc"` on the device:
```
mmc-bcm2835 3f300000.mmc: mmc_debug:0 mmc_debug2:0
mmc-bcm2835 3f300000.mmc: DMA channel allocated
mmc1: new high speed SDIO card at address 0001
mmc0: sdhost-bcm2835 loaded - DMA enabled (>1)
mmc0: Host Software Queue enabled
mmc0: new high speed SDXC card at address aaaa
mmcblk0: mmc0:aaaa SR64G 59.5 GiB (quirks 0x00004000)
mmcblk0: p1 p2
mmcblk0p2: EXT4-fs (mmcblk0p2): mounted filesystem ... r/w
```

**Finding so far**: the SDIO **hardware** is detected (`mmc1: new high speed
SDIO card at address 0001` — this is the RTL8723BS responding on the SDIO
bus, our `dtoverlay=sdio` is correctly wired). But `ip link show` shows only
`lo` (no `wlan0`), `lsmod` shows nothing for 8723, and `rfkill list` is
empty — **no WiFi driver is bound to the device**. Unlike circuix-sword's
NixOS setup (in-tree driver, works automatically), Batocera's kernel does
NOT appear to ship/autoload an `rtl8723bs`/`r8723bs` module for this build.
**RESOLVED — `find / -iname "*8723*"` output (on device):**

Firmware present (`/overlay/base/lib/firmware/rtlwifi/` and
`/overlay/base/lib/firmware/rtw88/`): `rtl8723aufw_B.bin`,
`rtl8723aufw_B_NoBT.bin`, `rtl8723befw.bin`, `rtl8723befw_36.bin`,
`rtl8723bs_ap_wowlan.bin`, `rtl8723bs_bt.bin`, `rtl8723bs_nic.bin`,
`rtl8723bu_ap_wowlan.bin`, `rtl8723bu_nic.bin`, `rtl8723defw.bin`,
`rtl8723fw.bin`, `rtl8723fw_B.bin`, `rtw88/rtw8723d_fw.bin`.

Kernel modules present (`/lib/modules/6.12.62-v8/kernel/drivers/net/wireless/realtek/rtw88/`):
`rtw_8723d.ko`, `rtw_8723cs.ko`, `rtw_8723ds.ko`, `rtw_8723du.ko`,
`rtw_8723x.ko` — the **mainline rtw88 (mac80211) driver family**, matching
the module set the design doc's "Candidate: rtw88-rtl8723bs" note already
anticipated (`rtw_*` family, not `r8723bs`).

**Critically: no `rtw_8723bs.ko` (or any `r8723bs`/legacy-staging module) is
present anywhere on the system.** This confirms, definitively, the exact
limitation already flagged in `Retropie_source/FUTURE.md`: *"Mainline rtw88
supports 8723CS/DS but not 8723BS (SDIO)."* Batocera 43.1 ships rtw88 support
for the PCIe/USB RTL8723 variants (CS/DS/DU/X) but not the SDIO variant
(BS) our hardware uses — there is nothing to `modprobe` for our chip out of
the box.

**Task 5 conclusion — SUPERSEDED, better option found.** Initially concluded
an out-of-tree `rtw88-rtl8723bs` (MocLG fork) Buildroot package was required.
Cross-checking against `nixos-reference` (circuix-sword) revealed a simpler,
proven path instead: NixOS's RPi kernel build uses the **legacy in-kernel
staging `rtl8723bs` driver** (module `r8723bs`) for this exact chip — the
same driver the original RetroPie build already used successfully (see
`Retropie_source/CLAUDE.md`: "we build the in-kernel staging `rtl8723bs`
driver... into our custom kernel").

Confirmed via `gh api`: Batocera's bcm2837 board (`configs/batocera-bcm2837.board`
at the `batocera-43.1` tag) builds its kernel from
`github.com/raspberrypi/linux` at commit `a1073743767f9e7fdc7017ababd2a07ea0c97c1c`
(kernel 6.12.25 — matches our on-device 6.12.62 closely, same 6.12.x series).
That exact commit **has `drivers/staging/rtl8723bs/`** in-tree, with a
`Kconfig` exposing `CONFIG_RTL8723BS` (tristate, module name `r8723bs`,
depends on `WLAN && MMC && CFG80211`). Batocera's kernel config for bcm2837
simply doesn't enable it — Batocera ships the newer `rtw88` family instead
(sufficient for the OTHER Realtek chips it supports, but that family has no
8723BS variant, per the original finding above).

**Firmware is already present**: the earlier `find / -iname "*8723*"` output
already showed `rtl8723bs_nic.bin` and `rtl8723bs_bt.bin` present on the
image (`/overlay/base/lib/firmware/rtlwifi/`) — no firmware packaging work
needed, only enabling the kernel module.

**Revised Task 5 conclusion**: add a kernel config fragment
(`CONFIG_RTL8723BS=m`) to the Circuit-Sword board's kernel build via
Batocera's existing `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES` mechanism (the
bcm2837 board file already uses this pattern for its own fragment file) —
**no new out-of-tree Buildroot package needed**, no third-party fork
dependency (MocLG's rtw88-rtl8723bs is no longer needed). This is simpler,
lower-risk, and proven twice already on this exact hardware (RetroPie,
NixOS). Rebuilding the kernel (not just adding a module package) is
required either way since Batocera ships a prebuilt/pinned kernel per board
— the config fragment approach and the out-of-tree module approach both
need a real image rebuild, but the fragment approach avoids maintaining a
separate module source tree entirely.

Root filesystem confirmed auto-resized to fill the card (SR64G 59.5 GiB,
`mmcblk0p2` r/w, ext4) with no manual intervention — Task 7 (partition
auto-resize is native) looks resolved, pending final confirmation.

## Bonus finding: memory (zram)

Batocera ships a native `zramswap` user service; confirmed **already active
by default** on this build (~511 MB compressed swap shown via `free -h`/
`swapon --show`, no manual enable needed) — same mitigation the RetroPie
build relies on for its 1 GB RAM. Note: CLAUDE.md hard rule #4 (`-j2` max
for on-device builds, `-j3+` OOMs even with zram) is specific to the RetroPie
build's on-device DKMS rebuild. Under Buildroot, the `rtw88-rtl8723bs`
out-of-tree module is cross-compiled on the build machine, not on the 1 GB
CM3 device — that specific build-time RAM constraint mostly does not apply
to this project's driver build, though it would still matter for anything
genuinely compiled on-device (not currently planned).

## Task 6: Kernel version compatibility — RESOLVED

Batocera 43.1 ships kernel **6.12.62** (`uname -a` above). The two known API
breaks the current RetroPie DKMS build works around — 6.15's
`del_timer_sync`/`from_timer` renames, and 6.18's cfg80211 `link_id`
parameter changes — both post-date 6.12.62. Neither applies here: the
`compat.h` shims from `Retropie_source/wifi-driver/compat.h` are **not
needed** for the `rtw88-rtl8723bs` out-of-tree module build against this
kernel. (Will need re-checking whenever Batocera bumps its kernel version in
a future release — not a one-time-forever answer, but a non-issue for the
version in hand now.) No dtoverlay deprecation warnings were observed in the
dmesg excerpt captured so far either.
