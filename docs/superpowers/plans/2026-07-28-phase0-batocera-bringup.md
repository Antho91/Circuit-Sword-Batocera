# Phase 0 — Batocera Hardware Bring-Up & Research Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get Batocera's `rpi3` image booting on the real Circuit-Sword CM3 hardware, answer the open Phase-0 questions from the design doc (in-tree vs out-of-tree WiFi driver, kernel version compatibility, first-boot partition resize), and scaffold the `circuit-sword` br2-external tree skeleton that later phases build into.

**Architecture:** Hands-on hardware bring-up, not software development — most tasks are "flash / boot / observe / record," not "write code / test / commit." Findings are captured in a running `PHASE0-FINDINGS.md` log (this project has no git repo — see Global Constraints) instead of git commits. Task 3 sets up an on-device diagnostic logger (a Batocera user service under `/userdata/system/services/`) that dumps `dmesg`/`journalctl`/`lsmod`/etc. to the FAT boot partition on every boot — from Task 4 onward, the user can pull the SD card, hand over (or copy into this repo) the latest `boot-*.log` file, and troubleshooting doesn't require a live session. The one code-producing task (Task 8) scaffolds the br2-external directory structure per Buildroot's standard convention.

**Tech Stack:** Batocera (Buildroot-based), Raspberry Pi CM3 firmware/bootloader, SD card imaging tools, serial console (UART) for headless debugging, `dmesg`/`lsmod`/`uname` for on-device diagnostics.

## Global Constraints

- **No git repo in this project.** `/Users/bas/Circuit-Sword Batocera` was deliberately stripped of git (see conversation history — user wants this local-only, no risk of accidental push). Every task that would normally end in `git commit` instead ends in "append findings to `PHASE0-FINDINGS.md`". Do not run `git init` without asking the user first.
- **One physical device, no CI/fleet.** All hardware-facing steps are manual, on the user's real Circuit-Sword unit. Do not assume repeatable/scriptable CI — each hardware step needs an explicit "what to look for" so a human (or the user reporting back) can confirm pass/fail.
- **Fan hard rule carries over**: never PWM the 2-wire fan. Not exercised in Phase 0 (fan control is Phase 3), but if any step touches `config.txt` fan-adjacent settings, do not introduce PWM.
- **`-j2` max for any on-device compilation** (1 GB RAM on the CM3). Not expected in Phase 0 (no on-device builds planned), but if a task ends up compiling anything on the device itself, cap parallelism.
- **Existing config.txt reference**: `Retropie_source/settings/boot/config.txt` — no explicit `device_tree=` line is set today, meaning the RPi firmware auto-selects `bcm2710-rpi-cm3.dtb` from the CM3's on-SoM revision OTP. Batocera's boot firmware must do the same, or the CM3-specific dtb must be confirmed present in Batocera's boot partition (Task 1).
- **Relevant existing overlays to reproduce** (from `Retropie_source/settings/boot/config.txt`): `dtoverlay=gpio-poweroff,gpiopin=39,active_low="y"`, `dtoverlay=sdio`, `dtoverlay=uart0,txd0_pin=32,rxd0_pin=33,pin_func=7`, plus `dtparam=audio=off` and `avoid_warnings=2`.
- **Design doc is the source of truth for scope**: `docs/superpowers/specs/2026-07-28-batocera-port-design.md`. If a Phase 0 finding contradicts an assumption in that doc, update the doc's "Open gaps" section — don't silently diverge.

---

### Task 1: Flash Batocera rpi3 image and attempt first boot on CM3

**Files:**
- Create: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (running findings log for all of Phase 0)

**Interfaces:**
- Produces: `PHASE0-FINDINGS.md` with a "Task 1" section other tasks append to.

- [ ] **Step 1: Create the findings log**

Create `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` with this starting content:

```markdown
# Phase 0 Findings Log

Running log for the Batocera hardware bring-up spike. Each task appends its
own dated section. This is the working record referenced by the design doc's
"Open gaps" section — update that doc once a gap here is resolved.

## Task 1: Flash & first boot
```

- [ ] **Step 2: Download the current stable Batocera `rpi3` image**

From https://batocera.org/download (or the version pinned by whatever
Batocera release the team decides to target — record the exact version/date
downloaded, Batocera doesn't use predictable version numbers across
architectures). Save the `.img.gz` file locally; do not extract yet.

Record in `PHASE0-FINDINGS.md` under Task 1: the exact filename and download
URL/date.

- [ ] **Step 3: Inspect the boot partition for a CM3 device tree before flashing**

Mount the downloaded image's boot partition (on macOS: `hdiutil attach` after
gunzip, or use `7z x`/`losetup` on Linux) and check for the CM3 dtb:

```bash
gunzip -k batocera-rpi3-*.img.gz
# macOS example — adjust for actual partition layout:
hdiutil attach -imagekey diskimage-class=CRawDiskImage batocera-rpi3-*.img
ls /Volumes/*/  | grep -i cm3
```

Expected: `bcm2710-rpi-cm3.dtb` (or `bcm2710-rpi-cm3-io1.dtb`) present
alongside the standard `bcm2710-rpi-3-b.dtb`. If it's **missing**, this is a
blocking finding — record it and flag to the user before continuing (Batocera
may need an explicit dtb added, which changes Phase 0 scope).

Record the result (present/absent, exact filename) in
`PHASE0-FINDINGS.md`.

- [ ] **Step 4: Flash to SD card**

Use Balena Etcher or `dd` to flash the (still-gzipped is fine for Etcher;
`dd` needs it decompressed) image to a spare SD card — **not** the card
currently running the working RetroPie build, so that build stays intact as
a fallback.

```bash
# dd example (macOS), replace /dev/rdiskN with the actual SD card device —
# verify with `diskutil list` first, this is destructive to the target card:
diskutil unmountDisk /dev/diskN
sudo dd if=batocera-rpi3-*.img of=/dev/rdiskN bs=4m status=progress
```

- [ ] **Step 5: First boot attempt on the Circuit-Sword hardware**

Insert the card into the Circuit-Sword's CM3 slot, power on. Since the DPI
panel isn't configured yet (that's Task 4), the panel will likely show
nothing or garbage — this step is about confirming the board **powers up and
the kernel starts**, checked via:

- A UART serial console cable on the same TX/RX pins the current build uses
  (`Retropie_source/settings/boot/config.txt` line 71:
  `dtoverlay=uart0,txd0_pin=32,rxd0_pin=33,pin_func=7` — but note this
  overlay isn't applied yet on stock Batocera, so use whatever UART pins are
  active by default on a stock Pi3 boot — check the Raspberry Pi's default
  UART pins (GPIO 14/15) if the custom pin mapping isn't active this early).
- Or: SD card activity LED / board power LED behavior as a coarse signal.

Expected: some sign of life (serial boot log, or LED activity matching a
normal Linux boot). If there is none at all, record exact symptoms (no
power, power but no activity, etc.) — this blocks the rest of Phase 0 and
needs hardware-level debugging before continuing.

- [ ] **Step 6: Record the finding**

Append to `PHASE0-FINDINGS.md` under Task 1: image version used, whether the
CM3 dtb was present, and the first-boot result (boots / doesn't boot, with
symptoms). This is the "commit" for this task — there is no git repo, this
file is the record.

---

### Task 2: Get a usable console (serial or HDMI) and confirm stable boot

**Files:**
- Modify: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (append Task 2 section)

**Interfaces:**
- Consumes: Task 1's boot result (must have at least partial sign of life to proceed).
- Produces: a confirmed working console method other tasks use for `dmesg`/`lsmod`/`uname` checks.

- [ ] **Step 1: Get a login prompt**

Two options, try in this order:

1. **HDMI + USB keyboard**: plug a standard HDMI monitor into the board (if
   the Circuit-Sword's HDMI header is accessible) and a USB keyboard.
   Batocera should boot to EmulationStation or a console login on HDMI
   without any custom config, since it doesn't know about the DPI panel yet.
2. **Serial console**: if HDMI isn't accessible on this hardware, use the
   UART pins confirmed in Task 1 Step 5. Connect via a USB-serial adapter at
   115200 baud: `screen /dev/tty.usbserial-XXXX 115200`.

Expected: a login prompt. Batocera's default credentials are `root` /
`linux` (record if this differs in the downloaded version).

- [ ] **Step 2: Confirm SSH as a backup/parallel access method**

Once logged in once, check if SSH is enabled by default:

```bash
systemctl status sshd 2>/dev/null || service sshd status
ip addr show   # get the DHCP-assigned IP if WiFi/ethernet somehow already works
```

Note: WiFi won't work yet (that's Task 4), so SSH is only useful if the
Circuit-Sword has a way to get ethernet/USB networking to the board, or if
testing later once WiFi is up. Record what's available now.

- [ ] **Step 3: Record baseline system info**

While logged in, capture:

```bash
uname -a
cat /etc/os-release 2>/dev/null || cat /usr/share/batocera/batocera.version
cat /proc/cpuinfo | grep -i "model\|hardware\|revision"
```

Append full output to `PHASE0-FINDINGS.md` under a new "Task 2" section —
this is the baseline for Task 5's kernel-compatibility check.

---

### Task 3: Set up boot-time diagnostic logging to the FAT boot partition

**Files:**
- Create (on the device, persisted on the userdata partition — survives reboots and OTA updates): `/userdata/system/services/circuitswordlog`
- Modify: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (append Task 3 section)

**Interfaces:**
- Consumes: working login from Task 2.
- Produces: `/boot/circuit-sword-logs/boot-<timestamp>.log` written fresh on every boot. From Task 4 onward, any step that says "check dmesg/lsmod/etc." can instead be satisfied by the user pulling the SD card, mounting the boot partition on their host machine, and copying the latest log into this repo (e.g. `docs/superpowers/plans/sd-logs/`) for Claude to `Read` directly — no live session needed.

Batocera's root filesystem is a **read-only SquashFS**; anything written directly to `/etc` or `/usr` on a running system is lost on reboot. Batocera's supported mechanism for persistent custom behavior is a **user service script** under `/userdata/system/services/` (the `userdata` partition is writable and survives reboots/updates) — it receives `start`/`stop` as `$1` and is enabled via `batocera-services`.

- [ ] **Step 1: Write the logging service script**

Create `/userdata/system/services/circuitswordlog` on the device (via the console/SSH session from Task 2), with this exact content:

```sh
#!/bin/sh
# Circuit-Sword Phase 0 diagnostic logger.
# Dumps kernel/system state to the FAT boot partition on every start, so the
# log can be read by pulling the SD card into another machine — no live
# session or SSH access needed for troubleshooting.

LOGDIR=/boot/circuit-sword-logs
mkdir -p "$LOGDIR"

case "${1}" in
start)
    STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || echo "boot-$(cut -d. -f1 /proc/uptime)")
    LOGFILE="$LOGDIR/boot-$STAMP.log"
    {
        echo "=== uname -a ==="; uname -a
        echo "=== os-release ==="; cat /etc/os-release 2>/dev/null
        echo "=== /boot/config.txt ==="; cat /boot/config.txt 2>/dev/null
        echo "=== dmesg ==="; dmesg
        echo "=== lsmod ==="; lsmod
        echo "=== ip addr ==="; ip addr 2>/dev/null
        echo "=== rfkill ==="; rfkill list 2>/dev/null
        echo "=== df -h ==="; df -h
        echo "=== journalctl -b (last 500 lines) ==="; journalctl -b -n 500 --no-pager 2>/dev/null
    } > "$LOGFILE" 2>&1
    # keep only the 10 most recent logs so the FAT boot partition doesn't fill up
    ls -t "$LOGDIR"/boot-*.log 2>/dev/null | tail -n +11 | xargs -r rm -f
    ;;
stop)
    # nothing to stop — this is a one-shot dump on start, not a daemon
    ;;
esac
```

- [ ] **Step 2: Make it executable and enable it**

```bash
chmod +x /userdata/system/services/circuitswordlog
batocera-services enable circuitswordlog
batocera-services start circuitswordlog
```

- [ ] **Step 3: Verify a log file was written**

```bash
ls -la /boot/circuit-sword-logs/
cat /boot/circuit-sword-logs/boot-*.log | head -30
```

Expected: at least one `boot-*.log` file exists with real content (not
empty, not a permission error).

- [ ] **Step 4: Reboot and confirm the service re-runs automatically**

```bash
sudo reboot
# after reboot:
ls -la /boot/circuit-sword-logs/   # should now show a second, newer log file
```

- [ ] **Step 5: Confirm the log is readable from the host machine**

Power off, remove the SD card, insert it into the machine you're running
Claude Code from. The boot partition (FAT) should mount automatically (e.g.
`/Volumes/circuit-sword-logs` or similar on macOS). Copy the newest
`boot-*.log` into this repo, e.g.:

```bash
mkdir -p "docs/superpowers/plans/sd-logs"
cp /Volumes/*/circuit-sword-logs/boot-*.log "docs/superpowers/plans/sd-logs/"
```

Confirm Claude can `Read` the copied file directly.

- [ ] **Step 6: Record findings**

Append to `PHASE0-FINDINGS.md` under Task 3: confirmation the service
persists across reboots, and the exact host-side mount path so this step
doesn't need re-discovering later. Note this service is a **Phase-0-only
diagnostic aid** — remove it (or fold a trimmed version into the real
`circuit-sword-external` rootfs overlay) once Phase 0 wraps up; it's not part
of the shipped design.

---

### Task 4: Apply Circuit-Sword boot overlays and bring up the DPI panel

**Files:**
- Modify: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (append Task 4 section)
- Modify (on the SD card's boot partition, not in this repo): `config.txt`

**Interfaces:**
- Consumes: working console access from Task 2, diagnostic logging from Task 3 (use it instead of a live session wherever convenient).
- Produces: a known-good `config.txt` overlay block, to be copied into the br2-external tree in Task 8.

- [ ] **Step 1: Locate Batocera's config.txt override mechanism**

Batocera's boot partition ships a `config.txt` that's regenerated/managed by
the system in some versions — check the Batocera wiki convention (typically
editable directly on the FAT boot partition, same as RetroPie) by mounting
the boot partition from another machine or via SSH:

```bash
mount | grep boot
cat /boot/config.txt   # or wherever it's mounted, confirm the path
```

Record the exact path and whether it's the standard RPi-firmware config.txt
or a Batocera-specific wrapper.

- [ ] **Step 2: Add the Circuit-Sword-specific overlay lines**

Append these lines (adapted from
`Retropie_source/settings/boot/config.txt`) to the boot partition's
`config.txt`:

```ini
# Circuit Sword hardware — Phase 0 bring-up
avoid_warnings=2
dtoverlay=gpio-poweroff,gpiopin=39,active_low="y"
dtparam=audio=off
dtoverlay=sdio
enable_uart=1
dtoverlay=uart0,txd0_pin=32,rxd0_pin=33,pin_func=7
```

Do **not** add the DPI-panel-specific lines yet (lines around 111-147 in the
reference file) until Step 3 confirms which panel variant is connected and
what the exact DPI timing block looks like — copy that block verbatim from
`Retropie_source/settings/boot/config.txt` lines 105-147 in this step, since
it's already known-good on this exact hardware:

```bash
sed -n '100,150p' "Retropie_source/settings/boot/config.txt"
```

Copy the relevant DPI block (matching the panel actually installed on the
test unit) into the boot partition's `config.txt` as well.

- [ ] **Step 3: Reboot and verify each overlay loaded**

```bash
sudo reboot
# after reboot, over serial or SSH:
dmesg | grep -i "gpio-poweroff\|sdio\|uart0"
vcdbg log msg 2>&1 | grep -i dpi   # or check /boot/config.txt was actually read without errors
```

Expected: no dtoverlay errors in `dmesg`/boot log, and the DPI panel shows a
real framebuffer image (even just a boot splash or console text) instead of
being blank/garbled.

- [ ] **Step 4: Record findings**

Append to `PHASE0-FINDINGS.md` under Task 4: exact config.txt block used,
whether DPI panel displays correctly, whether gpio-poweroff/sdio/uart0
loaded without error. If the DPI panel does NOT display correctly, capture
the symptom (blank / garbled / wrong resolution / rotated incorrectly) —
this becomes a blocking item for Phase 1, not Phase 0, but must be recorded
now while the context is fresh. Also copy the relevant `dmesg` excerpt from
the latest `/boot/circuit-sword-logs/boot-*.log` (Task 3) into the findings
entry, so it's preserved even if the live session is gone.

---

### Task 5: Determine WiFi driver strategy (in-tree vs out-of-tree)

**Files:**
- Modify: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (append Task 5 section)
- Modify: `docs/superpowers/specs/2026-07-28-batocera-port-design.md` (resolve open gap once answered)

**Interfaces:**
- Consumes: `dtoverlay=sdio` active from Task 4.
- Produces: a definitive answer recorded in the design doc's WiFi subsystem section, replacing the "Open question (Phase 0)" note.

- [ ] **Step 1: Check whether the rtl8723bs module is already present**

```bash
find / -iname "*rtl8723bs*" -o -iname "*r8723bs*" 2>/dev/null
modinfo r8723bs 2>&1
lsmod | grep -i 8723
```

- [ ] **Step 2: Check whether it loads and produces a wireless interface**

```bash
dmesg | grep -i "8723\|sdio"
ip link show
rfkill list
```

Expected outcomes, and what each means:
- Module present under `/lib/modules/$(uname -r)/kernel/drivers/net/wireless/`
  and a `wlan0`-style interface appears → **in-tree driver, works out of the
  box**, same situation as `nixos-reference`. No DKMS/br2-external kernel
  work needed for WiFi.
- Module absent entirely → **needs to be built**, either as an out-of-tree
  Buildroot package (via the br2-external `linux/` mechanism) using the same
  source the current RetroPie build uses
  (`Retropie_source/wifi-driver/`), or Batocera's kernel config needs the
  in-tree driver enabled (a kernel `.config` change, simpler than
  out-of-tree if that's all that's missing).
- Module present but fails to bind/produces errors → note the exact
  `dmesg` error; likely a firmware-file-location issue (compare against
  `Retropie_source/bt-driver/rtlbt_fw*` and check
  `/lib/firmware/rtlwifi/` or `/lib/firmware/rtl_bt/` paths Batocera expects
  vs. ships).

- [ ] **Step 3: If working, apply the stability fix and verify**

If step 2 produced a working `wlan0`, apply the known stability fix from
`Retropie_source/settings/r8723bs.conf`:

```bash
mkdir -p /etc/modprobe.d
cat > /etc/modprobe.d/r8723bs.conf <<'EOF'
options r8723bs rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0
EOF
rmmod r8723bs 2>/dev/null; modprobe r8723bs
iwconfig wlan0 2>&1   # confirm module reloaded with options applied
cat /sys/module/r8723bs/parameters/rtw_bw_mode  # should read 0
```

Note Batocera's network stack (likely `connman` or `wpa_supplicant`
directly, not NetworkManager — **confirm which one** with
`ps aux | grep -i "connman\|networkmanager\|wpa_supplicant"`) and find its
power-save equivalent to `wifi-powersave-off.conf`. Record the exact
mechanism found — this replaces the NetworkManager-specific config referenced
in the design doc if Batocera doesn't use NetworkManager.

- [ ] **Step 4: Record findings and update the design doc**

Append full findings to `PHASE0-FINDINGS.md` under Task 5. Then edit
`docs/superpowers/specs/2026-07-28-batocera-port-design.md`, in the "WiFi
(RTL8723BS) + Bluetooth" section, replace the "Open question (Phase 0)"
paragraph with the definitive answer and update open-gap item's status
(don't delete the gap — mark it resolved with a one-line pointer to this
finding).

---

### Task 6: Kernel version compatibility check

**Files:**
- Modify: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (append Task 6 section)
- Modify: `docs/superpowers/specs/2026-07-28-batocera-port-design.md` (resolve open gap #7)

**Interfaces:**
- Consumes: `uname -a` output captured in Task 2 Step 3.

- [ ] **Step 1: Compare kernel versions**

The current RetroPie build targets `KERNEL_BRANCH=rpi-6.18.y` (from
`Retropie_source/CLAUDE.md`). Compare against the Batocera kernel version
recorded in Task 2:

```bash
# on the Batocera device:
uname -r
```

- [ ] **Step 2: Check for the specific API changes the current build patches around**

The current DKMS `compat.h` shims exist because of two known kernel API
breaks (per `Retropie_source/FUTURE.md`): the 6.15 `del_timer_sync`/
`from_timer` renames and the 6.18 cfg80211 `link_id` parameter changes. If
Task 5 found the in-tree driver works, these don't matter (in-tree tracks
the running kernel by definition). If Task 5 found an out-of-tree module is
needed, check whether Batocera's kernel version is before or after 6.15/6.18
to know whether those specific shims from
`Retropie_source/wifi-driver/compat.h` need to be ported.

```bash
cat "Retropie_source/wifi-driver/compat.h"   # review what's patched, for reference
```

- [ ] **Step 3: Spot-check the other dtoverlays used**

Confirm `gpio-poweroff`, `sdio`, and `uart0` overlays (already verified
loading in Task 4) don't emit deprecation warnings in `dmesg` that would
indicate a version mismatch:

```bash
dmesg | grep -i "overlay\|deprecat"
```

- [ ] **Step 4: Record findings and update the design doc**

Append to `PHASE0-FINDINGS.md` under Task 6: Batocera's exact kernel
version, and whether it's compatible as-is or needs the compat shims ported.
Update open-gap item #7 in the design doc with the resolution.

---

### Task 7: Confirm first-boot partition auto-resize is native

**Files:**
- Modify: `docs/superpowers/plans/findings/PHASE0-FINDINGS.md` (append Task 7 section)
- Modify: `docs/superpowers/specs/2026-07-28-batocera-port-design.md` (confirm/remove the "expected to be native" note)

**Interfaces:**
- Consumes: a fully booted system from Task 4.

- [ ] **Step 1: Check filesystem size immediately after first boot**

```bash
df -h /
lsblk
```

The current RetroPie build has a dedicated `cs-firstboot-resize.service` that
grows the root partition to fill the SD card, then self-deletes (per
`Retropie_source/CLAUDE.md`). Batocera is expected to do this natively as
part of its own first-boot process, since its userdata partition needs to
fill whatever card size it's flashed to.

- [ ] **Step 2: Compare reported size against the actual SD card capacity**

```bash
diskutil info /dev/diskN   # or fdisk -l on Linux, from the host machine
# vs. df -h output from the device itself
```

Expected: the Batocera userdata/system partition already fills the card
without any manual intervention. If it does NOT (i.e., still shows the
original small image size), this is a real gap — Batocera might expect a
first-run wizard step or a specific boot flag to trigger the resize.

- [ ] **Step 3: Record findings**

Append to `PHASE0-FINDINGS.md` under Task 7. Update the design doc's
"Dropped / superseded, no porting needed" section — if resize is confirmed
native, leave as-is; if it's NOT automatic, move this item into the "Open
gaps" section instead with the specifics of what's missing.

---

### Task 8: Scaffold the `circuit-sword` br2-external tree

**Files:**
- Create: `circuit-sword-external/external.desc`
- Create: `circuit-sword-external/external.mk`
- Create: `circuit-sword-external/Config.in`
- Create: `circuit-sword-external/configs/circuitsword_defconfig`
- Create: `circuit-sword-external/board/circuitsword/readme.txt`
- Create: `circuit-sword-external/package/README.md`
- Create: `circuit-sword-external/README.md`

**Interfaces:**
- Consumes: nothing (structural scaffolding only — this is the skeleton later phases fill in).
- Produces: the directory layout that Phase 1+ tasks will add board config, kernel module packages, and overlay files into. The design doc's "Architecture" section names this `circuit-sword` br2-external tree — this task creates it as `circuit-sword-external/` at the repo root, sibling to `Retropie_source/` and `nixos-reference/`.

- [ ] **Step 1: Create the top-level `external.desc`**

Buildroot's `br2-external` mechanism requires this file to identify the tree.
Create `circuit-sword-external/external.desc`:

```
name: CIRCUITSWORD
desc: Circuit-Sword handheld board support for Batocera
```

- [ ] **Step 2: Create `external.mk`**

Empty for now — this is where package makefiles get included as they're
added in later phases. Create `circuit-sword-external/external.mk`:

```makefile
# Circuit-Sword br2-external tree — package includes added here as
# packages are added under package/ in later phases (WiFi stability config,
# cs-hud hardware daemon, etc.). Empty until Phase 1.
```

- [ ] **Step 3: Create `Config.in`**

Empty top-level Kconfig entry point, extended as packages are added. Create
`circuit-sword-external/Config.in`:

```
# Circuit-Sword br2-external tree — package Kconfig entries sourced here
# as packages are added in later phases. Empty until Phase 1.
```

- [ ] **Step 4: Create the board defconfig placeholder**

Create `circuit-sword-external/configs/circuitsword_defconfig` with a header
comment only — the actual defconfig content depends on Task 5's WiFi driver
finding and Task 1-4's confirmed boot overlays, which aren't known until
those tasks complete in this same Phase 0 run:

```
# Circuit-Sword Batocera defconfig
#
# Placeholder — to be filled in during Phase 1 once Task 1-7 findings
# (docs/superpowers/plans/findings/PHASE0-FINDINGS.md) confirm the base Batocera rpi3
# defconfig to start from, plus the WiFi driver strategy from Task 5.
```

- [ ] **Step 5: Create the board overlay directory with a readme**

Create `circuit-sword-external/board/circuitsword/readme.txt`:

```
Circuit-Sword board overlay files.

This directory holds boot-partition overlay files (config.txt fragment,
device tree overlays) and root-filesystem overlay files (systemd units,
modprobe.d configs) specific to the Circuit-Sword hardware.

Phase 0 (docs/superpowers/plans/2026-07-28-phase0-batocera-bringup.md) only
scaffolds this directory. The known-good config.txt block confirmed in
Task 4 of that plan should be copied here verbatim as the first real file,
at the start of Phase 1.
```

- [ ] **Step 6: Create a package directory placeholder**

Create `circuit-sword-external/package/README.md`:

```markdown
# Circuit-Sword packages

Buildroot packages specific to Circuit-Sword hardware go here (one directory
per package, each with a `.mk` and `Config.in`), added starting Phase 1+:

- WiFi stability modprobe config (from Task 5 findings)
- `cs-hud` hardware daemon (Phase 3)
- Bluetooth `rtk_hciattach` (Phase 2)

Empty as of Phase 0 — this is structural scaffolding only.
```

- [ ] **Step 7: Create the tree's top-level README**

Create `circuit-sword-external/README.md`:

```markdown
# circuit-sword-external

Buildroot `br2-external` tree for the Circuit-Sword Batocera port. Built
against a pinned Batocera version (see
`../docs/superpowers/specs/2026-07-28-batocera-port-design.md` for the
architecture rationale).

## Structure

- `external.desc` — br2-external identity
- `external.mk` / `Config.in` — top-level makefile/Kconfig includes, extended
  as packages are added
- `configs/` — board defconfigs
- `board/circuitsword/` — boot/rootfs overlay files (config.txt fragments,
  systemd units, modprobe configs)
- `package/` — Circuit-Sword-specific Buildroot packages

## Status

Scaffolded in Phase 0 (structure only, see
`../docs/superpowers/plans/2026-07-28-phase0-batocera-bringup.md`). First
real content lands in Phase 1.
```

- [ ] **Step 8: Verify the directory structure**

```bash
find circuit-sword-external -type f
```

Expected output (7 files):

```
circuit-sword-external/external.desc
circuit-sword-external/external.mk
circuit-sword-external/Config.in
circuit-sword-external/configs/circuitsword_defconfig
circuit-sword-external/board/circuitsword/readme.txt
circuit-sword-external/package/README.md
circuit-sword-external/README.md
```

- [ ] **Step 9: Record completion**

Append a final "Task 8" section to `PHASE0-FINDINGS.md` confirming the
scaffold is in place, and a one-line summary of Tasks 1-7's key findings
(boots: y/n, DPI: y/n, WiFi driver strategy chosen, kernel compat: y/n,
resize: native y/n) as a Phase 0 executive summary at the top of the file.

---

## Phase 0 exit criteria

Phase 0 is done when `PHASE0-FINDINGS.md` has a recorded, non-blocking answer
for all of: boots on CM3, DPI panel works, WiFi driver strategy is decided,
kernel compatibility is confirmed, partition resize is confirmed, and the
`circuit-sword-external/` skeleton exists. Any blocking finding (e.g., "does
not boot at all," "no CM3 dtb available") stops here for a scope
re-discussion with the user rather than proceeding into Phase 1.
