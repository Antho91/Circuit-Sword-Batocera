# Batocera Custom Build — RTL8723BS WiFi Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a custom Batocera image for the Circuit-Sword (bcm2837/CM3) with the in-tree `rtl8723bs` kernel driver enabled, the confirmed-good `config.txt` baked in, and WiFi preseeded — so the very first boot has working WiFi and SSH access, without any manual post-flash console work.

**Architecture:** Clone `batocera-linux/batocera.linux` pinned to tag `batocera-43.1` (matches the kernel 6.12.x already tested in Phase 0). Rather than fighting Buildroot's `br2-external` isolation for every file, pragmatically edit the pinned clone's own `board/batocera/broadcom/bcm2837/` board files directly (config.txt, kernel config) — this is a local, pinned fork the user controls, so directness beats purity here. The `circuit-sword-external` tree (already scaffolded) still holds the modprobe.d WiFi-stability file and documentation, wired in via Buildroot's rootfs-overlay mechanism. Build via Batocera's existing Docker-based Makefile; this takes hours, so it runs as a background step, not something to rush past.

**Tech Stack:** Buildroot (via Batocera's Makefile + Docker), Linux kernel config fragments, genimage (boot partition assembly), Raspberry Pi Imager (flashing — `dd` hit macOS Full Disk Access permission issues in Phase 0).

## Global Constraints

- **No git repo in this project root** (`/Users/bas/Circuit-Sword Batocera` was deliberately stripped of git — stays local-only). The `batocera.linux` clone this plan creates in Task 1 has its OWN nested `.git` (that's normal and fine, same as `nixos-reference/`) — do not run `git init`/`git commit` in the **project root**, and do not touch `.git` inside the `batocera.linux` clone beyond normal `git clone`/`git checkout`.
- **Findings log**: append progress to `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md` (create in Task 1), same pattern as `PHASE0-FINDINGS.md` — this project doesn't use commit messages as the record.
- **Build host requirements** (confirmed available): Docker running, ≥8GB RAM, ≥50GB free disk space.
- **Pin to tag `batocera-43.1`** — confirmed via `gh api repos/batocera-linux/batocera.linux/tags` this tag exists and its `configs/batocera-bcm2837.board` file specifies kernel 6.12.25 (raspberrypi/linux commit `a1073743767f9e7fdc7017ababd2a07ea0c97c1c`), matching the 6.12.62 already tested on real hardware in Phase 0. Building from `master` would drift to kernel 6.18.x (confirmed via the same API call) and invalidate Phase 0's hardware-tested findings.
- **Driver decision (do not re-litigate)**: use the **in-tree** `drivers/staging/rtl8723bs/` (`CONFIG_RTL8723BS`, module `r8723bs`) already present in `raspberrypi/linux` at the pinned commit — confirmed via `gh api repos/raspberrypi/linux/contents/drivers/staging/rtl8723bs?ref=a1073743767f9e7fdc7017ababd2a07ea0c97c1c`. Do NOT build the `MocLG/rtw88-rtl8723bs` out-of-tree package — that was an earlier, superseded plan (see `docs/superpowers/plans/findings/PHASE0-FINDINGS.md`, Task 5, "SUPERSEDED" note). Firmware (`rtl8723bs_nic.bin`, `rtl8723bs_bt.bin`) is already present on the stock Batocera image — no firmware packaging needed.
- **WiFi module stability options carry over unchanged from the original RetroPie build** (`Retropie_source/settings/r8723bs.conf`), because this is now the SAME driver (`r8723bs`) RetroPie used, not the `rtw88` family which has different parameter names:
  ```
  options r8723bs rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0
  ```
- **Confirmed-good `config.txt` block** already exists at `circuit-sword-external/board/circuitsword/config.txt.fragment` — read this file directly, it is the exact content to bake in (do not retype from memory). It depends on `hdmi_force_hotplug=1` and `hdmi_safe=1` being set earlier in the same file (also already true of the fragment's header comment).
- **WiFi preseed mechanism** (from Phase 0): appending to `/boot/batocera-boot.conf`:
  ```
  wifi.import.enabled=1
  nas.wifi.ssid=<SSID>
  nas.wifi.key=<KEY>
  ```
  The user's real SSID/key must never be typed into chat or committed to any file in this repo — Task 5 handles this by having the user edit the file locally themselves, same as Phase 0.
- **Flashing**: use Raspberry Pi Imager with "Use custom" → the built `.img` file. `dd` requires macOS Full Disk Access grants that caused friction in Phase 0 — don't default to it.

---

### Task 1: Clone and pin batocera.linux, verify the build environment responds

**Files:**
- Create: `batocera.linux/` (git clone, at the project root `/Users/bas/Circuit-Sword Batocera/batocera.linux`)
- Create: `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md`

**Interfaces:**
- Produces: a working, correctly-pinned local clone other tasks edit directly.

- [ ] **Step 1: Create the findings log**

Create `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md`:

```markdown
# WiFi Driver Build — Findings Log

Running log for building a custom Batocera image with in-tree RTL8723BS
WiFi support. Companion to PHASE0-FINDINGS.md — same pattern, this repo has
no git so this file is the record instead of commit messages.

## Task 1: Clone & environment check
```

- [ ] **Step 2: Clone batocera.linux, pinned to the batocera-43.1 tag**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
git clone https://github.com/batocera-linux/batocera.linux.git
cd batocera.linux
git checkout batocera-43.1
git log -1 --oneline
```

Expected: checkout succeeds cleanly, `git log -1` shows a commit tagged
`batocera-43.1`.

- [ ] **Step 3: Confirm the pinned kernel matches Phase 0's tested hardware**

```bash
cat configs/batocera-bcm2837.board | grep -i "kernel\|BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION"
```

Expected: shows `# Kernel - Version: 6.12.25` and the tarball location
referencing commit `a1073743767f9e7fdc7017ababd2a07ea0c97c1c`. If this
differs (e.g. the tag was force-moved upstream), STOP and re-verify against
`docs/superpowers/plans/findings/PHASE0-FINDINGS.md`'s Task 6 kernel version
(6.12.62) before proceeding — a kernel mismatch invalidates the
`CONFIG_RTL8723BS` in-tree-presence assumption from the Global Constraints
section (re-check `gh api repos/raspberrypi/linux/contents/drivers/staging/rtl8723bs?ref=<new-commit>`
if it differs).

- [ ] **Step 4: Sanity-check the Docker build environment without a full build**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera.linux"
make vars 2>&1 | tail -30
```

Expected: prints Buildroot/Docker variable info without error (confirms
`make`, Docker, and the repo's own tooling are wired up correctly) — this
does NOT start a build, just validates the environment before committing to
an hours-long build in Task 4.

- [ ] **Step 5: Record findings**

Append to `WIFI-BUILD-FINDINGS.md` under Task 1: clone commit hash, kernel
version confirmation, and the `make vars` sanity-check result.

---

### Task 2: Enable the in-tree RTL8723BS kernel driver

**Files:**
- Modify: `batocera.linux/board/batocera/broadcom/bcm2837/linux-defconfig.config`
- Modify: `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md` (append Task 2 section)

**Interfaces:**
- Consumes: the pinned clone from Task 1.
- Produces: a kernel build that includes `r8723bs.ko`, for Task 4's build to pick up.

- [ ] **Step 1: Inspect the current kernel defconfig for context**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera.linux"
grep -n "RTL8723\|RTW88" board/batocera/broadcom/bcm2837/linux-defconfig.config board/batocera/broadcom/linux-defconfig-fragment.config
```

Expected: shows whatever `RTW88_*` lines already exist (for the OTHER
Realtek chips this board's stock config supports), and confirms
`CONFIG_RTL8723BS` is NOT already present anywhere (if it IS already
present, STOP — that means an earlier assumption was wrong, record this in
the findings file, and check whether it's already enabled as a module
before touching anything further).

- [ ] **Step 2: Add the RTL8723BS config line**

Open `board/batocera/broadcom/bcm2837/linux-defconfig.config` and append, on
its own line:

```
CONFIG_RTL8723BS=m
```

- [ ] **Step 3: Verify the line was added correctly**

```bash
grep -n "CONFIG_RTL8723BS" board/batocera/broadcom/bcm2837/linux-defconfig.config
```

Expected: one line, `CONFIG_RTL8723BS=m`.

- [ ] **Step 4: Record findings**

Append to `WIFI-BUILD-FINDINGS.md` under Task 2: confirmation of the exact
line added and its location.

---

### Task 3: Bake in the confirmed config.txt and WiFi stability modprobe file

**Files:**
- Modify: `batocera.linux/board/batocera/broadcom/bcm2837/boot/config.txt`
- Create: `batocera.linux/board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/r8723bs.conf`
- Modify: `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md` (append Task 3 section)

**Interfaces:**
- Consumes: `circuit-sword-external/board/circuitsword/config.txt.fragment` (Phase 0 output, read directly, do not retype).
- Produces: a boot partition and rootfs that need no manual post-flash editing (unlike Phase 0's manual SD-card-editing workflow).

- [ ] **Step 1: Confirm how Batocera assembles config.txt into the boot partition**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera.linux"
cat board/batocera/broadcom/bcm2837/genimage.cfg
ls board/batocera/broadcom/bcm2837/boot/
```

Expected: `genimage.cfg` shows a `boot.vfat` image built from `@files`
(Buildroot's staged `BINARIES_DIR`/boot-partition-staging directory), and
`board/batocera/broadcom/bcm2837/boot/config.txt` is the actual stock
config.txt Batocera ships for this board (confirmed present via `gh api` —
this is the file to modify, since it's staged into the boot partition by
Batocera's own `create-boot-script.sh` build step).

- [ ] **Step 2: Read the confirmed-good Circuit-Sword config.txt block**

```bash
cat "/Users/bas/Circuit-Sword Batocera/circuit-sword-external/board/circuitsword/config.txt.fragment"
```

This is the exact block to append — it already documents (in its own header
comments) that `hdmi_force_hotplug=1` and `hdmi_safe=1` must ALSO be set
earlier in the file.

- [ ] **Step 3: Edit the stock config.txt with both changes**

In `board/batocera/broadcom/bcm2837/boot/config.txt`:

1. Find the existing (commented-out) line `#hdmi_force_hotplug=1` and
   uncomment it (remove the leading `#`).
2. Find the existing (commented-out) line `#hdmi_safe=1` and uncomment it.
3. Append the full contents of `circuit-sword-external/board/circuitsword/config.txt.fragment`
   (everything from `avoid_warnings=2` through `framebuffer_height=480`,
   skip that file's own header comment block since it's Phase-0-planning
   context, not config.txt syntax) to the end of the file.

- [ ] **Step 4: Verify the edit**

```bash
tail -25 board/batocera/broadcom/bcm2837/boot/config.txt
grep -c "^hdmi_force_hotplug=1$\|^hdmi_safe=1$" board/batocera/broadcom/bcm2837/boot/config.txt
```

Expected: tail shows the Circuit-Sword block ending in
`framebuffer_height=480`; the grep count is `2` (both hotplug lines
uncommented, not still prefixed with `#`).

- [ ] **Step 5: Add the WiFi stability modprobe file to the board's own fsoverlay**

Create `board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/r8723bs.conf`:

```
options r8723bs rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0
```

(This directory structure mirrors exactly where the file lands on the
target rootfs — Buildroot's `BR2_ROOTFS_OVERLAY` copies it to
`/etc/modprobe.d/r8723bs.conf` verbatim; no additional wiring needed, this
board's `fsoverlay` directory is already referenced by
`configs/batocera-bcm2837.board`'s `BR2_ROOTFS_OVERLAY` line confirmed in
Task 1.)

- [ ] **Step 6: Record findings**

Append to `WIFI-BUILD-FINDINGS.md` under Task 3: confirmation both files are
in place, with their exact final paths.

---

### Task 4: Run the build

**Files:**
- Modify: `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md` (append Task 4 section)

**Interfaces:**
- Consumes: Tasks 1-3's changes to the pinned clone.
- Produces: a built `.img` file for Task 5 to flash.

- [ ] **Step 1: Start the build**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera.linux"
make BOARD=bcm2837 batocera 2>&1 | tee "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
```

This will run for **hours** (Buildroot compiles a full kernel, toolchain,
and rootfs from source). Run this as a background/long-running command —
don't block waiting on it turn-by-turn. Periodically check progress with:

```bash
tail -50 "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
```

- [ ] **Step 2: Confirm the build completed and locate the output image**

```bash
find "/Users/bas/Circuit-Sword Batocera/batocera.linux/output" -iname "*.img" -newer "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log" 2>/dev/null
```

Expected: one or more `.img` files under `output/.../images/`. If the build
failed instead, search the tail of `wifi-build.log` for the actual error
(Buildroot build failures usually name the exact failing package/step) —
this is a real failure to diagnose, not something to paper over; capture
the exact error text in the findings file before attempting any fix.

- [ ] **Step 3: Confirm the built kernel actually includes the module**

Buildroot stages kernel modules under the build's target/staging tree before
they're packed into the final image. Verify without needing the device:

```bash
find "/Users/bas/Circuit-Sword Batocera/batocera.linux/output" -iname "r8723bs.ko*" 2>/dev/null
```

Expected: at least one `r8723bs.ko` (or `.ko.zst`/`.ko.xz` if compressed —
the board file's `BR2_TARGET_ROOTFS_SQUASHFS4_ZSTD=y` suggests zstd
compression may apply to modules too) found under the build output. If
nothing is found, Task 2's kernel config change didn't take effect — check
whether the kernel build used a cached/stale `.config` (Buildroot kernel
rebuilds sometimes need `make linux-reconfigure` or a clean kernel-config
step if the kernel was already configured before Task 2's edit landed).

- [ ] **Step 4: Record findings**

Append to `WIFI-BUILD-FINDINGS.md` under Task 4: build duration, output
image path, and confirmation `r8723bs.ko` is present in the build output.

---

### Task 5: Flash, preseed WiFi, and boot-test on real hardware

**Files:**
- Modify: `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md` (append Task 5 section)

**Interfaces:**
- Consumes: the `.img` file from Task 4.

- [ ] **Step 1: Flash via Raspberry Pi Imager**

Same process as Phase 0 (avoids the `dd`/macOS Full Disk Access friction
hit there): open Raspberry Pi Imager → "Choose OS" → "Use custom" → select
the `.img` file located in Task 4 Step 2 → choose the SD card → Write.

- [ ] **Step 2: Preseed WiFi via batocera-boot.conf (user does this locally, not via chat)**

The user mounts the freshly-flashed SD card's boot partition and appends to
`batocera-boot.conf`:

```
wifi.import.enabled=1
nas.wifi.ssid=<the user's real SSID>
nas.wifi.key=<the user's real password>
```

(Same file/mechanism as documented in `PHASE0-FINDINGS.md` — the assistant
should prepare a placeholder-only template file for the user to copy and
personally edit, exactly as was done in Phase 0's
`sd-batocera-boot-wifi-snippet.txt`, never asking the user to paste
credentials into chat.)

- [ ] **Step 3: Boot on the Circuit-Sword and check WiFi**

```bash
# on the device, once booted (console access per Phase 0's Ctrl+Alt+F5 finding):
dmesg | grep -i "r8723bs\|rtl8723"
lsmod | grep r8723bs
ip link show
iwconfig 2>&1
```

Expected: `r8723bs` module loaded (in `lsmod`), a `wlan0`-style interface
present in `ip link show`. If the module loaded but no interface appears,
check `dmesg` for probe errors (firmware path mismatches are the most
common cause — cross-check against the firmware paths noted in
`PHASE0-FINDINGS.md`'s Task 5, `/lib/firmware/rtlwifi/rtl8723bs_nic.bin`).

- [ ] **Step 4: Confirm actual network connectivity and SSH**

```bash
ip addr show wlan0
ping -c 3 8.8.8.8
```

Then, from the Mac:

```bash
ssh root@<device-ip-shown-above>
```

Expected: ping succeeds, SSH connects (password `linux` unless already
changed).

- [ ] **Step 5: Confirm the DPI screen and other Phase-0-verified hardware still work**

Quick regression check — these were baked in via Task 3, confirm they
survived the rebuild:

```bash
dmesg | grep -i "gpio-poweroff\|sdio\|uart0"
```

Visually confirm the DPI panel shows the desktop (not just HDMI).

- [ ] **Step 6: Record findings**

Append to `WIFI-BUILD-FINDINGS.md` under Task 5: full `dmesg`/`lsmod`/`ip
addr` output confirming WiFi works, the device's IP, and confirmation SSH
access works. This is the plan's actual goal — mark it clearly.

---

## Exit criteria

This plan is done when: the image builds cleanly, `r8723bs.ko` is present
and loads on real hardware, `wlan0` gets an IP via the preseeded
credentials, and `ssh root@<device-ip>` succeeds from the Mac — with the DPI
screen, gpio-poweroff, and sdio/uart0 overlays still confirmed working
(no regression from Phase 0's manually-tested config).
