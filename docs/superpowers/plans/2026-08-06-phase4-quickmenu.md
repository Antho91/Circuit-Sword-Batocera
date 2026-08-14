# Circuit-Sword In-Game Quick Menu (Phase 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-game quick menu (WiFi toggle / Volume / Brightness) to the Circuit-Sword Batocera image, opened by the Arduino MODE button while a game is running, via a brief hand-off of display ownership from RetroArch to a small standalone libdrm program.

**Architecture:** The existing Phase-3 hardware daemon (`rpi-circuitsword.py`) already polls the Arduino's `CMD_GET_STATUS` byte over serial, so it owns the MODE button end-to-end. On a debounced MODE press with RetroArch running, it probes RetroArch's local UDP network-command port, sends `PAUSE_TOGGLE`, switches the active virtual terminal, and launches a new standalone C program `circuitsword-quickmenu` which takes DRM master, draws 3 items straight into a KMS dumb buffer, and reads the Arduino joystick via raw evdev. When that program exits (B button, a second MODE press relayed as SIGTERM, a crash, or a 5-second watchdog kill), the daemon switches the VT back and sends `PAUSE_TOGGLE` again. There is no overlay and never more than one process drawing — CLAUDE.md hard rule #6.

**Tech Stack:** Python 3 + pyserial + stdlib `socket`/`fcntl`/`subprocess` (daemon, already in the image); C99 + libdrm dumb buffers + raw Linux evdev (`<linux/input.h>`, no libevdev) for the menu program; Buildroot `generic-package` for packaging; Docker-based Buildroot cross-build.

## Global Constraints

Copied verbatim from `CLAUDE.md` and the design docs — these are non-negotiable for every task below:

1. **Never PWM the fan.** It's a 2-wire blower — on/off only, temperature-based.
2. **`-j2` max for on-device builds.** 1 GB RAM; `-j3+` OOMs even with zram. (Host cross-builds in Docker use the project's existing `BR2_JLEVEL=4` from `batocera-build/scripts/env.sh` — do not change it, and never run a build *on the device*.)
3. **Updates stay manual/user-triggered.** Never add auto-update behavior.
4. **WiFi (RTL8723BS) stability fix must carry over**: `rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0` plus WiFi power-save disabled at the network-manager level. This phase must not touch those overlay files.
5. **No DispmanX/overlay layer exists on this hardware's KMS stack.** A HUD or menu cannot draw on top of a running emulator. Don't try to "fix" this by finding an overlay approach that doesn't exist on this hardware.
6. **The main project directory `/Users/bas/Circuit-Sword Batocera` is deliberately NOT a git repo.** Never run `git init` or `git commit` there. All git operations happen in `/Users/bas/batocera-build-wifi/batocera.linux`, followed by a patch-regeneration step that writes a plain file into the project directory.
7. **No hardware in CI.** Every task states explicitly what is verified off-device vs. what still needs the physical device. Never claim hardware behavior is confirmed when it isn't.

**Fixed paths used throughout:**

| Thing | Path |
| --- | --- |
| Real build tree (git repo, HEAD detached at `batocera-43.1`) | `/Users/bas/batocera-build-wifi/batocera.linux` |
| Patch capture target | `/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch` |
| Patch base SHA (pinned commit, verified ancestor of HEAD) | `155c2d8d304cbb53db52e9479dcf683392821d5c` |
| Phase-3 daemon to extend | `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` |
| New Buildroot package | `<build tree>/package/batocera/utils/circuitsword-quickmenu/` |
| Host-side unit tests (non-git project dir) | `/Users/bas/Circuit-Sword Batocera/tests/` |
| Findings log | `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` |

**Patch regeneration command** (run verbatim, from the build tree, after every commit that touches the tree):

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

`batocera-build/scripts/setup-build-tree.sh` re-applies that file with `git apply --whitespace=nowarn "$PATCH_DIR/batocera-linux.patch"` after resetting to the pinned commit, so the patch must always be a diff **from the pinned commit to HEAD**, never a working-tree diff.

**Package build (compile-check) command**, from the build tree, with the project's env:

```bash
cd /Users/bas/Circuit-Sword Batocera/batocera-build/scripts
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

---

## Architecture note: "VT switch" vs. DRM-master hand-off — a refinement of the design doc's wording

The design doc (`docs/superpowers/specs/2026-08-06-quickmenu-design.md`, Architecture section) says "daemon switches VT". Investigation of this actual Batocera 43.1 / KMSDRM tree found:

- There is **no `chvt` call anywhere** in the ES↔emulator hand-off. `grep -rn "chvt\|VT_ACTIVATE\|openvt" package/batocera/core/batocera-configgen/ package/batocera/core/batocera-scripts/` returns nothing relevant. The only `/dev/tty0` references in the whole tree are `batocera-mount` writing fsck progress text.
- The real ES↔emulator hand-off is **process-exit / suspend based**: `package/batocera/emulationstation/batocera-emulationstation/S31emulationstation` has a `suspend` action that makes ES *quit* (`curl http://localhost:1234/quit`) and a `resume` action that unblocks a FIFO. DRM master is released because the previous owner's process/DRM fd goes away — not because of a virtual-terminal switch.
- Virtual terminals **do exist** (`/etc/inittab` runs gettys on tty4/tty5; `S33disablealtfn` uses `dumpkeys`/`loadkeys`), so a VT switch is *possible*, but whether it forcibly revokes a live RetroArch's DRM master on this kernel is **not established by anything in this tree**.

That matters because in Phase 4, unlike every existing hand-off, **RetroArch stays alive** (paused) and therefore still holds its DRM fd. So `drmSetMaster()` from `circuitsword-quickmenu` may return `EBUSY`.

**Therefore this plan does not assume the mechanism — Task 2 is a cheap on-device spike that measures it before any C code is written**, using the already-flashed Phase-3 image and the `batocera-drminfo` binary that is already on it. The plan then implements VT switch + `drmSetMaster()` with an explicit, verifiable failure path (Task 10's `qm_drm_open()` returns NULL on `EBUSY`, and Task 12's daemon treats a non-zero quickmenu exit as "close immediately and resume the game"), so a negative spike result degrades to "menu doesn't open, game resumes cleanly" rather than a bricked screen.

This is a refinement of the design doc's terminology ("VT switch" → "VT switch as the trigger for a DRM-master hand-off, whose viability against a live RetroArch is measured, not assumed"), not a contradiction of its architecture: still no overlay, still exactly one process drawing at a time.

---

## Second finding: RetroArch network commands are NOT enabled in this fork

`grep -rn "network_cmd\|network_remote\|55355" package/batocera/` over the whole build tree returns **zero hits**. RetroArch's network command interface defaults to *disabled*, so `PAUSE_TOGGLE` would silently go nowhere. Task 3 adds `network_cmd_enable` and `network_cmd_port` to `libretroRetroarchCustom.py`, which is the file configgen writes `retroarchcustom.cfg` from at every emulator launch.

---

## Third finding: this board's volume is PipeWire, not ALSA `amixer`

The design doc says "ALSA master volume control directly". On BCM2837, `package/batocera/core/batocera-system/Config.in:260` selects `BR2_PACKAGE_BATOCERA_AUDIO` (the PipeWire package) — `BATOCERA_AUDIO_ALSA` is selected only for `BR2_PACKAGE_BATOCERA_TARGET_BCM2835`. `package/batocera/core/batocera-audio/alsa/batocera-audio` drives volume through `pactl set-sink-volume @DEFAULT_SINK@`, not `amixer`. The design doc's *intent* — "reuse the same primitive the existing counterpart already uses, one source of truth per setting" — is honoured by calling `batocera-audio getSystemVolume` / `batocera-audio setSystemVolume N`, which is exactly what ES itself invokes. That is what Task 9 implements. Writing `amixer sset Master` directly would be a *second*, divergent source of truth and is explicitly rejected here.

---

### Task 1: Setup — confirm build-tree state and open the findings log

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md`
- Create: `/Users/bas/Circuit-Sword Batocera/tests/` (directory)

**Interfaces:**
- Consumes: nothing.
- Produces: a verified-clean build tree at HEAD `46854a272337983c7b63a486ae3038f2946ff328` with base SHA `155c2d8d304cbb53db52e9479dcf683392821d5c`; the findings-log file that every later task appends to.

- [ ] **Step 1: Confirm the build tree is clean and at the expected HEAD.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git status --short
git log --oneline -1
git merge-base --is-ancestor 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD && echo "BASE-SHA-OK"
```

Expected: `git status --short` prints nothing (clean tree); `git log --oneline -1` prints `46854a2723 es_input.cfg: swap Arduino Leonardo A/B button mapping`; the last command prints `BASE-SHA-OK`. If the tree is dirty, stop and resolve before continuing — every later task's patch regeneration depends on a clean commit history.

- [ ] **Step 2: Confirm libdrm is enabled in the bcm2837 configuration.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
grep -rn "BR2_PACKAGE_LIBDRM" package/batocera/core/batocera-drminfo/Config.in
grep -c "libdrm" package/batocera/core/batocera-drminfo/batocera-drminfo.mk
```

Expected: the first prints `2:       depends on BR2_PACKAGE_LIBDRM`, confirming the `depends on` idiom Task 6 copies; the second prints `1`. `batocera-drminfo` is the precedent package for a small libdrm-consuming C binary in this tree — Task 6 copies its `.mk` shape verbatim.

- [ ] **Step 3: Create the tests directory and findings log.**

```bash
mkdir -p "/Users/bas/Circuit-Sword Batocera/tests"
```

Write `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
# Phase 4 Quick Menu — Findings Log

Companion to docs/superpowers/specs/2026-08-06-quickmenu-design.md
and docs/superpowers/plans/2026-08-06-phase4-quickmenu.md.

Real build tree: /Users/bas/batocera-build-wifi/batocera.linux
Reproducible patches: /Users/bas/Circuit-Sword Batocera/batocera-build/patches/
Patch base SHA: 155c2d8d304cbb53db52e9479dcf683392821d5c

Format follows PHASE3-HARDWARE-DAEMON-FINDINGS.md: one "### Task N: ..."
section per task, recording what was actually verified and what is still
assumption. Populated during execution, not up front.

## Task log

### Task 1: build tree confirmed clean at 46854a2723, findings log opened
```

- [ ] **Step 4: Verify the findings file exists and is non-empty.**

```bash
wc -l "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md"
```

Expected: a line count of `16`. No git commit here — this file lives in the non-git project directory (Global Constraint 6).

---

### Task 2: On-device spike — can a second process take DRM master while RetroArch runs?

This task needs the **physical device**, but requires **no rebuild and no reflash** — it runs entirely against the already-flashed Phase-3 image over SSH, using binaries already present. It is placed second, before any code is written, because its result determines whether Task 10's `qm_drm_open()` succeeds in practice and whether the whole feature is viable as designed.

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/tests/spike-drm-master.sh` (host-side helper, copied to the device)
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 2 section)

**Interfaces:**
- Consumes: Task 1's findings log.
- Produces: a recorded answer to three questions — (a) does `/dev/tty0` `VT_ACTIVATE` work on this image, (b) does a VT switch away from RetroArch free DRM master, (c) which `/dev/dri/cardN` node carries the DPI connector. Tasks 10 and 12 read these from the findings log.

- [ ] **Step 1: Write the spike script.**

Create `/Users/bas/Circuit-Sword Batocera/tests/spike-drm-master.sh`:

```bash
#!/bin/sh
# Phase 4 spike: run ON THE DEVICE, over SSH, while a game is running.
# Answers: is VT switching available, does it free DRM master, which card?
# Read-only apart from the VT switch itself, which is reverted at the end.
set -u

echo "=== 1. DRM nodes present ==="
ls -l /dev/dri/

echo "=== 2. is RetroArch running? (busybox-safe, no pgrep) ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in retroarch*) echo "RUNNING pid=${p#/proc/} comm=$c" ;; esac
done

echo "=== 3. which process holds each DRM node open ==="
for p in /proc/[0-9]*; do
    for fd in "$p"/fd/*; do
        t=$(readlink "$fd" 2>/dev/null) || continue
        case "$t" in /dev/dri/*) echo "${p#/proc/} $(cat "$p/comm" 2>/dev/null) -> $t" ;; esac
    done
done

echo "=== 4. current VT (before) ==="
fgconsole 2>/dev/null || echo "fgconsole: NOT AVAILABLE"

echo "=== 5. chvt availability ==="
command -v chvt || echo "chvt: NOT AVAILABLE"
command -v openvt || echo "openvt: NOT AVAILABLE"

echo "=== 6. python3 VT_ACTIVATE to tty6, then batocera-drminfo, then back ==="
python3 - <<'PYEOF'
import fcntl, struct, subprocess, sys, time
VT_GETSTATE, VT_ACTIVATE, VT_WAITACTIVE = 0x5603, 0x5606, 0x5607
try:
    f = open("/dev/tty0", "wb")
except OSError as e:
    print("open /dev/tty0 FAILED:", e); sys.exit(1)
buf = fcntl.ioctl(f, VT_GETSTATE, struct.pack("HHH", 0, 0, 0))
before = struct.unpack("HHH", buf)[0]
print("VT before =", before)
try:
    fcntl.ioctl(f, VT_ACTIVATE, 6)
    fcntl.ioctl(f, VT_WAITACTIVE, 6)
    print("VT_ACTIVATE(6) OK")
except OSError as e:
    print("VT_ACTIVATE(6) FAILED:", e); f.close(); sys.exit(1)
time.sleep(1)
r = subprocess.run(["/usr/bin/batocera-drminfo"], capture_output=True, text=True)
print("batocera-drminfo rc =", r.returncode)
print(r.stdout[:2000])
print(r.stderr[:2000])
try:
    fcntl.ioctl(f, VT_ACTIVATE, before)
    fcntl.ioctl(f, VT_WAITACTIVE, before)
    print("VT restored to", before)
except OSError as e:
    print("VT restore FAILED:", e)
f.close()
PYEOF

echo "=== 7. current VT (after) ==="
fgconsole 2>/dev/null || echo "fgconsole: NOT AVAILABLE"
echo "=== spike done ==="
```

- [ ] **Step 2: Copy the spike to the device and run it with a game running.**

Start a game on the device first (any RetroArch core), leave it running, then from the host:

```bash
scp "/Users/bas/Circuit-Sword Batocera/tests/spike-drm-master.sh" root@batocera.local:/tmp/
ssh root@batocera.local "sh /tmp/spike-drm-master.sh" 2>&1 | tee "/Users/bas/Circuit-Sword Batocera/tests/spike-drm-master.out"
```

Expected: section 2 lists at least one `RUNNING pid=... comm=retroarch`; section 3 shows `retroarch -> /dev/dri/cardN` for exactly one N; section 6 either prints `batocera-drminfo rc = 0` with connector output (**DRM master IS obtainable after a VT switch — proceed as planned**) or a non-zero rc / `Permission denied` / `EBUSY` (**DRM master is NOT obtainable while RetroArch lives**).

- [ ] **Step 3: Record the result in the findings log.**

Append to `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md`, filling in the real observed values:

```markdown
### Task 2: DRM-master spike (on device, no rebuild)
- DRM node RetroArch holds: /dev/dri/cardN   <-- record the real N
- fgconsole / chvt available: yes|no
- VT_ACTIVATE(6) via /dev/tty0 ioctl: OK|FAILED (<errno>)
- batocera-drminfo on tty6 while a game runs: rc=<n>
- CONCLUSION: DRM master IS / IS NOT obtainable by a second process while
  RetroArch is alive on this kernel. Design-doc "VT switch" therefore means
  <VT switch + drmSetMaster> / <does not work; see fallback note below>.
- Raw output: tests/spike-drm-master.out
```

If the conclusion is **IS NOT obtainable**, stop and escalate to the user before Task 3: the feature as designed cannot work without RetroArch cooperation, and the alternative (a RetroArch-internal `gfx_widgets` menu, already scoped in the main design doc's Phase 6 for the battery indicator) is a different, larger piece of work. Do not silently redesign.

- [ ] **Step 4: No commit.** Both files created here live in the non-git project directory (Global Constraint 6). Verify with:

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux && git status --short
```

Expected: no output.

---

### Task 3: Enable RetroArch's network command interface

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py:52-53` (insert after the existing `audio_volume` line, before the `# Settings` comment block)

**Interfaces:**
- Consumes: nothing.
- Produces: RetroArch listening for plaintext UDP commands on `127.0.0.1:55355` at every emulator launch. Task 11's `retroarch_cmd_query()` and `send_pause_toggle()` depend on this exact port.

- [ ] **Step 1: Add the two settings.**

In `libretroRetroarchCustom.py`, find:

```python
    # Audio
    retroarchSettings.save('audio_volume',                       '"2.0"')

    # Settings
```

Replace with:

```python
    # Audio
    retroarchSettings.save('audio_volume',                       '"2.0"')

    # Network commands: required by the Circuit-Sword quick menu, which
    # sends PAUSE_TOGGLE over UDP 127.0.0.1:55355 before handing the
    # display over to circuitsword-quickmenu (see
    # docs/superpowers/specs/2026-08-06-quickmenu-design.md). RetroArch
    # binds this to localhost only; 55355 is RetroArch's own default port.
    retroarchSettings.save('network_cmd_enable',                '"true"')
    retroarchSettings.save('network_cmd_port',                  '"55355"')

    # Settings
```

- [ ] **Step 2: Verify the file is still valid Python and the settings are present.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
python3 -m py_compile package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py && echo "PY-COMPILE-OK"
grep -n "network_cmd" package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py
```

Expected: `PY-COMPILE-OK`, then two grep lines showing `network_cmd_enable` and `network_cmd_port`.

- [ ] **Step 3: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py
git commit -m "configgen: enable RetroArch network commands on localhost:55355

Phase 4's quick menu sends PAUSE_TOGGLE over RetroArch's plaintext UDP
command interface before handing the display to circuitsword-quickmenu.
That interface is disabled by default and was not enabled anywhere in
this tree (grep for network_cmd returned zero hits), so PAUSE_TOGGLE
would have gone nowhere.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
```

- [ ] **Step 4: Regenerate the reproducible patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "network_cmd_enable" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Expected: `1`. **Not verified off-device:** that RetroArch actually honours these settings and responds on 55355 on this build — that is Task 15's on-device check.

---

### Task 4: Buildroot package skeleton for `circuitsword-quickmenu` (compiles a stub)

**Files:**
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/Config.in`
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (stub `main()`, replaced in Task 11)
- Modify: `<build tree>/Config.in:137` (add a `source` line after the two existing circuitsword ones)
- Modify: `<build tree>/package/batocera/core/batocera-system/Config.in:348` (add a `select` after the two existing circuitsword ones)

**Interfaces:**
- Consumes: the `batocera-drminfo` `.mk` pattern read in Task 1 Step 2.
- Produces: Buildroot symbol `BR2_PACKAGE_CIRCUITSWORD_QUICKMENU`, and an installed binary at `/usr/bin/circuitsword-quickmenu` on the target. Task 12's daemon launches exactly that path.

- [ ] **Step 1: Write `Config.in`.**

`package/batocera/utils/circuitsword-quickmenu/Config.in`:

```
config BR2_PACKAGE_CIRCUITSWORD_QUICKMENU
	bool "circuitsword-quickmenu"
	depends on BR2_PACKAGE_LIBDRM
	help
	  Standalone in-game quick menu (WiFi toggle, volume, brightness)
	  for the Circuit-Sword. Draws directly into a KMS dumb buffer via
	  libdrm and reads the on-board Arduino Leonardo joystick via raw
	  evdev. Launched by the rpi-circuitsword.py daemon on a MODE
	  button press while a game is running.
```

- [ ] **Step 2: Write `circuitsword-quickmenu.mk`.**

Note this uses `generic-package` only — **not** `kernel-module`, unlike `circuitsword-battery`/`circuitsword-backlight`, because this is a userspace binary. The build/install command shape is copied from `package/batocera/core/batocera-drminfo/batocera-drminfo.mk`, the tree's existing small libdrm C binary.

`package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`:

```
################################################################################
#
# circuitsword-quickmenu
#
################################################################################

CIRCUITSWORD_QUICKMENU_VERSION = 1.0
CIRCUITSWORD_QUICKMENU_SOURCE =
CIRCUITSWORD_QUICKMENU_LICENSE = GPL-2.0+
CIRCUITSWORD_QUICKMENU_DEPENDENCIES = libdrm

CIRCUITSWORD_QUICKMENU_SRCDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-quickmenu

CIRCUITSWORD_QUICKMENU_SRCS = \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/quickmenu.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_font.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_input.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_settings.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_drm.c

define CIRCUITSWORD_QUICKMENU_BUILD_CMDS
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(STAGING_DIR)/usr/include/drm \
		-I$(CIRCUITSWORD_QUICKMENU_SRCDIR) \
		$(CIRCUITSWORD_QUICKMENU_SRCS) \
		-o $(@D)/circuitsword-quickmenu -ldrm
endef

define CIRCUITSWORD_QUICKMENU_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 -D $(@D)/circuitsword-quickmenu \
		$(TARGET_DIR)/usr/bin/circuitsword-quickmenu
endef

$(eval $(generic-package))
```

- [ ] **Step 3: Write stub sources so the skeleton builds now.**

Create all five `.c` files plus the header, so the `.mk`'s file list is real from the start. `quickmenu.h`:

```c
#ifndef QUICKMENU_H
#define QUICKMENU_H
#include <stdint.h>
#endif /* QUICKMENU_H */
```

`quickmenu.c`:

```c
#include "quickmenu.h"
int main(void) { return 0; }
```

`qm_font.c`, `qm_input.c`, `qm_settings.c`, `qm_drm.c` — each exactly:

```c
#include "quickmenu.h"
```

(An empty translation unit is invalid C99; including the header gives each file a declaration, which is enough. Tasks 7-10 replace all four in full.)

- [ ] **Step 4: Register the package in the two Config.in files.**

In `<build tree>/Config.in`, after line 137 (`... circuitsword-backlight/Config.in"`), add:

```
    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-quickmenu/Config.in"
```

In `<build tree>/package/batocera/core/batocera-system/Config.in`, after line 348 (`select BR2_PACKAGE_CIRCUITSWORD_BACKLIGHT	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837`), add (tab-indented, matching the two lines above it):

```
	select BR2_PACKAGE_CIRCUITSWORD_QUICKMENU	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837
```

- [ ] **Step 5: Build the package and confirm the skeleton compiles and installs.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -30
```

Expected: ends with no error and the `>>> circuitsword-quickmenu 1.0 Installing to target` line. This is a **cross-compile check only — the binary is never executed here.**

- [ ] **Step 6: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu Config.in package/batocera/core/batocera-system/Config.in
git commit -m "circuitsword-quickmenu: add Buildroot generic-package skeleton

Userspace libdrm binary, so generic-package (pattern copied from
batocera-drminfo), not kernel-module like circuitsword-battery/backlight.
Stub sources only; real implementation lands in following commits.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 5: Full header — the contract every later task implements against

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.h` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: Task 4's package skeleton.
- Produces: every type and function signature used by Tasks 6-11. Exact signatures below are normative — later tasks must match them character for character.

- [ ] **Step 1: Write the full header.**

Replace `quickmenu.h` with:

```c
#ifndef QUICKMENU_H
#define QUICKMENU_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Framebuffer: 32-bit XRGB8888, which is what the KMS dumb buffer     */
/* created in qm_drm.c uses. `pixels` may be an mmap of GPU memory or  */
/* plain malloc'd memory (host unit tests use the latter).             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;   /* bytes per row, >= width * 4 */
} qm_fb;

#define QM_RGB(r, g, b) \
    (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define QM_COLOR_BG       QM_RGB(0x10, 0x10, 0x18)
#define QM_COLOR_FG       QM_RGB(0xE0, 0xE0, 0xE0)
#define QM_COLOR_DIM      QM_RGB(0x80, 0x80, 0x88)
#define QM_COLOR_SEL_BG   QM_RGB(0x30, 0x50, 0x90)
#define QM_COLOR_BAR_BG   QM_RGB(0x30, 0x30, 0x38)
#define QM_COLOR_BAR_FG   QM_RGB(0x50, 0xC0, 0x70)

/* ------------------------------------------------------------------ */
/* qm_font.c — pure drawing, no syscalls, host-unit-testable           */
/* ------------------------------------------------------------------ */
#define QM_GLYPH_W 5   /* glyph cell, in unscaled pixels */
#define QM_GLYPH_H 7
#define QM_GLYPH_ADVANCE 6   /* QM_GLYPH_W + 1px spacing */

void qm_fill_rect(qm_fb *fb, int x, int y, int w, int h, uint32_t color);
void qm_draw_char(qm_fb *fb, int x, int y, char c, int scale, uint32_t color);
void qm_draw_text(qm_fb *fb, int x, int y, const char *s, int scale, uint32_t color);
int  qm_text_width(const char *s, int scale);
uint32_t qm_get_pixel(const qm_fb *fb, int x, int y);

/* ------------------------------------------------------------------ */
/* qm_input.c — raw evdev on the Arduino Leonardo joystick             */
/* ------------------------------------------------------------------ */
enum qm_event {
    QM_EV_NONE = 0,
    QM_EV_UP,
    QM_EV_DOWN,
    QM_EV_LEFT,
    QM_EV_RIGHT,
    QM_EV_A,
    QM_EV_B
};

int  qm_input_open(void);                 /* fd on success, -1 on failure */
void qm_input_close(int fd);
enum qm_event qm_input_poll(int fd, int timeout_ms);

/* ------------------------------------------------------------------ */
/* qm_settings.c — each call reuses the primitive the existing         */
/* Batocera counterpart already uses; one source of truth per setting  */
/* ------------------------------------------------------------------ */
int qm_wifi_get(void);                    /* 0, 1, or -1 on error */
int qm_wifi_set(int enabled);             /* 0 ok, -1 error */
int qm_volume_get(void);                  /* 0..100, or -1 on error */
int qm_volume_set(int percent);           /* 0 ok, -1 error */
int qm_brightness_get(void);              /* 0..100, or -1 on error */
int qm_brightness_set(int percent);       /* 0 ok, -1 error */

/* ------------------------------------------------------------------ */
/* qm_drm.c — KMS dumb buffer, single CRTC, no page flipping           */
/* ------------------------------------------------------------------ */
typedef struct qm_drm qm_drm;

qm_drm *qm_drm_open(void);       /* NULL if no card / no connector / EBUSY */
qm_fb  *qm_drm_fb(qm_drm *d);    /* borrowed pointer, valid until close    */
int     qm_drm_present(qm_drm *d);   /* 0 ok, -1 error */
void    qm_drm_close(qm_drm *d);     /* restores the previous CRTC, drops master */

/* ------------------------------------------------------------------ */
/* quickmenu.c — menu model + rendering                                */
/* ------------------------------------------------------------------ */
#define QM_ITEM_WIFI       0
#define QM_ITEM_VOLUME     1
#define QM_ITEM_BRIGHTNESS 2
#define QM_ITEM_COUNT      3

typedef struct {
    int selected;      /* 0 .. QM_ITEM_COUNT-1 */
    int wifi_on;       /* 0 or 1 */
    int volume;        /* 0..100 */
    int brightness;    /* 0..100 */
} qm_state;

void qm_render(qm_fb *fb, const qm_state *st);

#endif /* QUICKMENU_H */
```

- [ ] **Step 2: Verify the skeleton still builds with the full header.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: no errors, `Installing to target` line present. (Declarations without definitions are fine — nothing calls them yet.)

- [ ] **Step 3: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.h
git commit -m "circuitsword-quickmenu: define the full internal API header

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 6: Host-side test harness for the pure drawing code (TDD: test first)

The drawing code touches no syscalls and no libdrm, so it compiles and runs on the macOS host with plain `cc`. Written before the implementation.

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c`
- Create: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`

**Interfaces:**
- Consumes: `quickmenu.h` from Task 5 (`qm_fb`, `qm_fill_rect`, `qm_draw_char`, `qm_draw_text`, `qm_text_width`, `qm_get_pixel`, `QM_GLYPH_*`, `QM_RGB`).
- Produces: `run-c-tests.sh`, re-run by Tasks 7 and 11.

- [ ] **Step 1: Write the test.**

`/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c`:

```c
/* Host-side unit tests for circuitsword-quickmenu's pure drawing code.
 * Compiled with the host cc against the real qm_font.c -- no libdrm,
 * no evdev, no device needed. Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickmenu.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

static qm_fb *make_fb(uint32_t w, uint32_t h)
{
    qm_fb *fb = malloc(sizeof(*fb));
    fb->width  = w;
    fb->height = h;
    fb->pitch  = w * 4;
    fb->pixels = calloc((size_t)fb->pitch * h, 1);
    return fb;
}

static void free_fb(qm_fb *fb) { free(fb->pixels); free(fb); }

static int count_nonzero(const qm_fb *fb)
{
    int n = 0;
    for (uint32_t y = 0; y < fb->height; y++)
        for (uint32_t x = 0; x < fb->width; x++)
            if (qm_get_pixel(fb, (int)x, (int)y) != 0) n++;
    return n;
}

int main(void)
{
    printf("qm_fill_rect\n");
    {
        qm_fb *fb = make_fb(20, 10);
        qm_fill_rect(fb, 2, 3, 4, 5, QM_RGB(0xFF, 0x00, 0x00));
        check(qm_get_pixel(fb, 2, 3) == QM_RGB(0xFF, 0x00, 0x00), "top-left set");
        check(qm_get_pixel(fb, 5, 7) == QM_RGB(0xFF, 0x00, 0x00), "bottom-right set");
        check(qm_get_pixel(fb, 1, 3) == 0, "left of rect untouched");
        check(qm_get_pixel(fb, 6, 3) == 0, "right of rect untouched");
        check(count_nonzero(fb) == 20, "exactly w*h pixels set");
        free_fb(fb);
    }

    printf("qm_fill_rect clipping\n");
    {
        qm_fb *fb = make_fb(8, 8);
        qm_fill_rect(fb, -4, -4, 100, 100, QM_RGB(0x01, 0x02, 0x03));
        check(count_nonzero(fb) == 64, "oversized rect clipped to fb, no crash");
        qm_fill_rect(fb, 100, 100, 5, 5, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 64, "fully offscreen rect draws nothing");
        free_fb(fb);
    }

    printf("qm_text_width\n");
    {
        check(qm_text_width("", 1) == 0, "empty string is 0 wide");
        check(qm_text_width("A", 1) == QM_GLYPH_W, "one glyph at scale 1");
        check(qm_text_width("AB", 1) == QM_GLYPH_ADVANCE + QM_GLYPH_W,
              "two glyphs include inter-glyph spacing");
        check(qm_text_width("AB", 2) == 2 * (QM_GLYPH_ADVANCE + QM_GLYPH_W),
              "scale 2 doubles the width");
    }

    printf("qm_draw_char\n");
    {
        qm_fb *fb = make_fb(16, 16);
        qm_draw_char(fb, 0, 0, ' ', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "space draws nothing");
        qm_draw_char(fb, 0, 0, 'A', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) > 0, "'A' draws something");
        check(qm_get_pixel(fb, 1, 0) != 0, "'A' has a lit pixel at its apex row");
        free_fb(fb);
    }

    printf("qm_draw_char case folding + unknown chars\n");
    {
        qm_fb *up = make_fb(16, 16), *lo = make_fb(16, 16);
        qm_draw_char(up, 0, 0, 'W', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_char(lo, 0, 0, 'w', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(up->pixels, lo->pixels, (size_t)up->pitch * up->height) == 0,
              "lowercase renders as uppercase");
        free_fb(up); free_fb(lo);

        qm_fb *fb = make_fb(16, 16);
        qm_draw_char(fb, 0, 0, '~', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "unmapped char draws nothing, no crash");
        free_fb(fb);
    }

    printf("qm_draw_text scaling\n");
    {
        qm_fb *a = make_fb(64, 32), *b = make_fb(64, 32);
        qm_draw_text(a, 0, 0, "WIFI", 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_text(b, 0, 0, "WIFI", 2, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(b) == 4 * count_nonzero(a),
              "scale 2 lights exactly 4x the pixels of scale 1");
        free_fb(a); free_fb(b);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Write the runner script.**

`/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`:

```bash
#!/bin/bash
# Host-side unit tests for circuitsword-quickmenu's pure drawing code.
# Compiles qm_font.c straight out of the real build tree with the host
# compiler -- no cross toolchain, no libdrm, no device.
set -euo pipefail

SRC="${BATOCERA_SRC:-/Users/bas/batocera-build-wifi/batocera.linux}/package/batocera/utils/circuitsword-quickmenu"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$SRC" \
   "$HERE/test_qm_font.c" "$SRC/qm_font.c" \
   -o "$OUT/test_qm_font"

"$OUT/test_qm_font"
```

- [ ] **Step 3: Make it executable and confirm it FAILS (nothing implemented yet).**

```bash
chmod +x "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
"/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"; echo "exit=$?"
```

Expected: a **link error** (`Undefined symbols ... _qm_fill_rect`) and a non-zero exit. This is the red step of TDD — the test genuinely cannot pass yet.

- [ ] **Step 4: No commit.** These files live in the non-git project directory (Global Constraint 6).

---

### Task 7: Implement `qm_font.c` — font table and drawing primitives

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_font.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5) — `qm_fb`, `QM_GLYPH_W`=5, `QM_GLYPH_H`=7, `QM_GLYPH_ADVANCE`=6.
- Produces: `qm_fill_rect`, `qm_draw_char`, `qm_draw_text`, `qm_text_width`, `qm_get_pixel` — all consumed by Task 11's `qm_render()`.

- [ ] **Step 1: Write the implementation.**

Replace `qm_font.c` with:

```c
/* Pure drawing into a 32bpp XRGB8888 buffer. No syscalls, no libdrm --
 * this file is compiled by the host compiler in tests/run-c-tests.sh as
 * well as by the Buildroot cross toolchain.
 *
 * Font: hand-authored 5x7 bitmap, uppercase-only (lowercase is folded to
 * uppercase), covering exactly the characters the menu draws. Bits are
 * the low 5 of each byte, MSB (bit 4) = leftmost column. */
#include "quickmenu.h"

#define QM_FONT_FIRST_INDEX_MISS (-1)

/* Character set, in the same order as qm_font_glyphs below. */
static const char qm_font_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :%-/<>.";

static const uint8_t qm_font_glyphs[][QM_GLYPH_H] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, /* : */
    {0x19,0x1A,0x02,0x04,0x08,0x0B,0x13}, /* % */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
};

static char qm_upper(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    return c;
}

static int qm_glyph_index(char c)
{
    char u = qm_upper(c);
    for (int i = 0; qm_font_chars[i] != '\0'; i++)
        if (qm_font_chars[i] == u)
            return i;
    return QM_FONT_FIRST_INDEX_MISS;
}

static void qm_put_pixel(qm_fb *fb, int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || (uint32_t)x >= fb->width || (uint32_t)y >= fb->height)
        return;
    uint32_t *row = (uint32_t *)(fb->pixels + (size_t)y * fb->pitch);
    row[x] = color;
}

uint32_t qm_get_pixel(const qm_fb *fb, int x, int y)
{
    if (x < 0 || y < 0 || (uint32_t)x >= fb->width || (uint32_t)y >= fb->height)
        return 0;
    const uint32_t *row = (const uint32_t *)(fb->pixels + (size_t)y * fb->pitch);
    return row[x];
}

void qm_fill_rect(qm_fb *fb, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            qm_put_pixel(fb, xx, yy, color);
}

void qm_draw_char(qm_fb *fb, int x, int y, char c, int scale, uint32_t color)
{
    if (scale < 1)
        return;
    int gi = qm_glyph_index(c);
    if (gi == QM_FONT_FIRST_INDEX_MISS)
        return;
    for (int row = 0; row < QM_GLYPH_H; row++) {
        uint8_t bits = qm_font_glyphs[gi][row];
        for (int col = 0; col < QM_GLYPH_W; col++) {
            if (!(bits & (1u << (QM_GLYPH_W - 1 - col))))
                continue;
            qm_fill_rect(fb, x + col * scale, y + row * scale,
                         scale, scale, color);
        }
    }
}

void qm_draw_text(qm_fb *fb, int x, int y, const char *s, int scale, uint32_t color)
{
    if (s == NULL || scale < 1)
        return;
    int cx = x;
    for (const char *p = s; *p != '\0'; p++) {
        qm_draw_char(fb, cx, y, *p, scale, color);
        cx += QM_GLYPH_ADVANCE * scale;
    }
}

int qm_text_width(const char *s, int scale)
{
    if (s == NULL || scale < 1)
        return 0;
    int n = 0;
    for (const char *p = s; *p != '\0'; p++)
        n++;
    if (n == 0)
        return 0;
    /* n-1 full advances plus one glyph body, so no trailing gap. */
    return ((n - 1) * QM_GLYPH_ADVANCE + QM_GLYPH_W) * scale;
}
```

- [ ] **Step 2: Run the host tests — they must now pass.**

```bash
"/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"; echo "exit=$?"
```

Expected: every line prefixed `  ok  `, final line `PASSED (0 failures)`, `exit=0`.

- [ ] **Step 3: Cross-compile check.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: no warnings-as-errors, no compile errors, `Installing to target` present. **Cross-compile only; nothing is executed on target here.**

- [ ] **Step 4: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_font.c
git commit -m "circuitsword-quickmenu: 5x7 bitmap font and drawing primitives

Pure, syscall-free code: unit-tested on the host via
tests/run-c-tests.sh (clipping, scaling, case folding, unknown chars).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 8: Implement `qm_input.c` — Arduino Leonardo evdev reader

Button/hat codes are **not guessed**: they come from this project's own `es_input.cfg` block for `deviceName="Arduino LLC Arduino Leonardo"` (`package/batocera/emulationstation/batocera-emulationstation/controllers/es_input.cfg:19-34`), which was captured on the real hardware — `b` = button code 288 (`BTN_TRIGGER`), `a` = button code 289 (`BTN_THUMB`), and up/right/down/left are hat 0 values 1/2/4/8, i.e. `ABS_HAT0X` / `ABS_HAT0Y`.

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_input.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5) — `enum qm_event`.
- Produces: `qm_input_open()`, `qm_input_close()`, `qm_input_poll()` — consumed by Task 11's `main()`.

- [ ] **Step 1: Write the implementation.**

Replace `qm_input.c` with:

```c
/* Raw evdev reader for the on-board Arduino Leonardo joystick.
 *
 * No libevdev dependency -- <linux/input.h> plus read() is enough for
 * six inputs, matching this project's minimal-dependency preference.
 *
 * Button/hat codes come from this repo's own es_input.cfg entry for
 * "Arduino LLC Arduino Leonardo", captured on the real hardware:
 *   b     = button code 288 (BTN_TRIGGER)
 *   a     = button code 289 (BTN_THUMB)
 *   d-pad = hat 0  (ABS_HAT0X: -1 left / +1 right,
 *                   ABS_HAT0Y: -1 up   / +1 down)
 *
 * The device is grabbed with EVIOCGRAB so the paused RetroArch behind us
 * does not also see menu navigation. The grab is released implicitly on
 * close(). */
#include "quickmenu.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define QM_DEVICE_NAME "Arduino LLC Arduino Leonardo"
#define QM_BTN_B 288  /* BTN_TRIGGER */
#define QM_BTN_A 289  /* BTN_THUMB   */

static int qm_open_if_match(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    char name[256];
    memset(name, 0, sizeof(name));
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        close(fd);
        return -1;
    }
    if (strcmp(name, QM_DEVICE_NAME) != 0) {
        close(fd);
        return -1;
    }
    /* Best effort: if the grab fails we still use the device. */
    if (ioctl(fd, EVIOCGRAB, 1) < 0)
        fprintf(stderr, "circuitsword-quickmenu: EVIOCGRAB failed on %s\n", path);
    return fd;
}

int qm_input_open(void)
{
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: cannot open /dev/input\n");
        return -1;
    }
    int fd = -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        fd = qm_open_if_match(path);
        if (fd >= 0)
            break;
    }
    closedir(dir);
    if (fd < 0)
        fprintf(stderr, "circuitsword-quickmenu: '%s' not found\n", QM_DEVICE_NAME);
    return fd;
}

void qm_input_close(int fd)
{
    if (fd >= 0) {
        ioctl(fd, EVIOCGRAB, 0);
        close(fd);
    }
}

enum qm_event qm_input_poll(int fd, int timeout_ms)
{
    if (fd < 0)
        return QM_EV_NONE;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0)
        return QM_EV_NONE;

    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            if (ev.code == QM_BTN_A)
                return QM_EV_A;
            if (ev.code == QM_BTN_B)
                return QM_EV_B;
        } else if (ev.type == EV_ABS) {
            if (ev.code == ABS_HAT0X) {
                if (ev.value < 0) return QM_EV_LEFT;
                if (ev.value > 0) return QM_EV_RIGHT;
            } else if (ev.code == ABS_HAT0Y) {
                if (ev.value < 0) return QM_EV_UP;
                if (ev.value > 0) return QM_EV_DOWN;
            }
            /* value == 0 is the release-to-centre, deliberately ignored:
             * it is what gives us one event per D-pad tap. */
        }
    }
    return QM_EV_NONE;
}
```

- [ ] **Step 2: Cross-compile check.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: clean build, `Installing to target` present. This file uses Linux-only headers, so there is **no host test for it** — it is compile-verified only.

- [ ] **Step 3: Record what is NOT verified.**

Append to `PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
### Task 8: qm_input.c written, cross-compile clean
Button/hat codes taken from this repo's own hardware-captured
es_input.cfg (b=288, a=289, hat0 for d-pad), not guessed. NOT verified:
that EVIOCGNAME really returns exactly "Arduino LLC Arduino Leonardo" on
the device, that EVIOCGRAB succeeds against a paused RetroArch, and that
one hat event per physical tap feels right. All on-device (Task 15).
```

- [ ] **Step 4: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_input.c
git commit -m "circuitsword-quickmenu: raw evdev reader for the Arduino Leonardo

Codes taken from this repo's hardware-captured es_input.cfg entry.
No libevdev dependency; EVIOCGRAB so the paused RetroArch behind the
menu does not also consume navigation input.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 9: Implement `qm_settings.c` — three settings, three existing primitives

Every one of the three reuses the mechanism its existing Batocera counterpart already uses, verbatim, so there is exactly one source of truth per setting:

| Item | Existing primitive | Where that was read from |
| --- | --- | --- |
| WiFi | `batocera-settings-set wifi.enabled 0/1` then `/etc/init.d/S08connman reload` | `package/batocera/core/batocera-scripts/scripts/batocera-wifi:52-79` — its `enable`/`disable` cases do exactly this |
| Volume | `batocera-audio getSystemVolume` / `batocera-audio setSystemVolume N` | `package/batocera/core/batocera-audio/alsa/batocera-audio` — PipeWire-backed (`pactl set-sink-volume @DEFAULT_SINK@`), the same script ES calls |
| Brightness | read/write `/sys/class/backlight/circuitsword-backlight/brightness`, scaled by `max_brightness` | `board/batocera/fsoverlay/etc/init.d/S27brightness` → `batocera-brightness`, which computes `NEWVAL = percent * max_brightness / 100` |

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_settings.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5).
- Produces: `qm_wifi_get/set`, `qm_volume_get/set`, `qm_brightness_get/set` — consumed by Task 11's `main()`.

- [ ] **Step 1: Write the implementation.**

Replace `qm_settings.c` with:

```c
/* Each setting is read/written through the *same* primitive the existing
 * Batocera counterpart already uses, so there is one source of truth per
 * setting -- see the table in the Phase 4 plan, Task 9. */
#include "quickmenu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QM_BACKLIGHT_DIR "/sys/class/backlight/circuitsword-backlight"

static int qm_run_capture_int(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (fp == NULL)
        return -1;
    char buf[64];
    memset(buf, 0, sizeof(buf));
    char *got = fgets(buf, sizeof(buf), fp);
    int rc = pclose(fp);
    if (got == NULL || rc != 0)
        return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf || errno != 0)
        return -1;
    return (int)v;
}

static int qm_read_int_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    int v = -1;
    if (fscanf(fp, "%d", &v) != 1)
        v = -1;
    fclose(fp);
    return v;
}

static int qm_write_int_file(const char *path, int value)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    int n = fprintf(fp, "%d\n", value);
    if (fclose(fp) != 0 || n <= 0)
        return -1;
    return 0;
}

static int qm_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---------------- WiFi ---------------- */

int qm_wifi_get(void)
{
    int v = qm_run_capture_int("/usr/bin/batocera-settings-get wifi.enabled 2>/dev/null");
    if (v < 0)
        return -1;
    return v ? 1 : 0;
}

int qm_wifi_set(int enabled)
{
    char cmd[192];
    /* batocera-wifi's own enable/disable path: flip wifi.enabled, then
     * reload S08connman. Reload is backgrounded because S08connman's
     * start path can block for several seconds waiting on connman, and
     * the menu must stay responsive (and must not risk tripping the
     * daemon's 5s close watchdog). */
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/batocera-settings-set wifi.enabled %d && "
             "(/etc/init.d/S08connman reload >/dev/null 2>&1 &)",
             enabled ? 1 : 0);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "circuitsword-quickmenu: wifi set failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

/* ---------------- Volume ---------------- */

int qm_volume_get(void)
{
    int v = qm_run_capture_int("/usr/bin/batocera-audio getSystemVolume 2>/dev/null");
    if (v < 0)
        return -1;
    return qm_clamp(v, 0, 100);
}

int qm_volume_set(int percent)
{
    percent = qm_clamp(percent, 0, 100);
    char cmd[160];
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/batocera-audio setSystemVolume %d >/dev/null 2>&1",
             percent);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "circuitsword-quickmenu: volume set failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

/* ---------------- Brightness ---------------- */

int qm_brightness_get(void)
{
    int max = qm_read_int_file(QM_BACKLIGHT_DIR "/max_brightness");
    int cur = qm_read_int_file(QM_BACKLIGHT_DIR "/brightness");
    if (max <= 0 || cur < 0)
        return -1;
    /* Same percent conversion batocera-brightness uses, rounded. */
    return qm_clamp((cur * 100 + max / 2) / max, 0, 100);
}

int qm_brightness_set(int percent)
{
    percent = qm_clamp(percent, 0, 100);
    int max = qm_read_int_file(QM_BACKLIGHT_DIR "/max_brightness");
    if (max <= 0) {
        fprintf(stderr, "circuitsword-quickmenu: no circuitsword-backlight device\n");
        return -1;
    }
    /* batocera-brightness: NEWVAL = percent * max / 100 */
    int raw = qm_clamp(percent * max / 100, 0, max);
    if (qm_write_int_file(QM_BACKLIGHT_DIR "/brightness", raw) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: brightness write failed\n");
        return -1;
    }
    return 0;
}
```

- [ ] **Step 2: Cross-compile check.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: clean build, `Installing to target` present.

- [ ] **Step 3: Verify every referenced helper actually exists in the tree (no invented commands).**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
grep -n "setSystemVolume\|getSystemVolume" package/batocera/core/batocera-audio/alsa/batocera-audio | head -4
grep -n "S08connman reload" package/batocera/core/batocera-scripts/scripts/batocera-wifi
ls board/batocera/fsoverlay/etc/init.d/S08connman
grep -n "max_brightness" board/batocera/fsoverlay/etc/init.d/S27brightness package/batocera/core/batocera-scripts/scripts/batocera-brightness | head -3
```

Expected: hits for all four — `getSystemVolume`/`setSystemVolume` in `batocera-audio`'s usage and case blocks, two `S08connman reload` lines in `batocera-wifi`, the `S08connman` file listing, and `max_brightness` reads in `batocera-brightness`.

- [ ] **Step 4: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_settings.c
git commit -m "circuitsword-quickmenu: wifi/volume/brightness via existing primitives

WiFi:       batocera-settings-set wifi.enabled + S08connman reload
            (exactly batocera-wifi's own enable/disable path)
Volume:     batocera-audio get/setSystemVolume -- this board selects the
            PipeWire batocera-audio package, not batocera-audio-alsa, so
            pactl is the real primitive, not amixer
Brightness: /sys/class/backlight/circuitsword-backlight, scaled by
            max_brightness the same way batocera-brightness scales it

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 10: Implement `qm_drm.c` — KMS dumb buffer

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_drm.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5) — `qm_fb`, `qm_drm`; libdrm headers from `$(STAGING_DIR)/usr/include/drm` (include path already set in Task 4's `.mk`).
- Produces: `qm_drm_open()`, `qm_drm_fb()`, `qm_drm_present()`, `qm_drm_close()` — consumed by Task 11's `main()`. `qm_drm_open()` returning `NULL` is the explicit error path for the Task 2 spike's negative outcome (DRM master unavailable while RetroArch lives).

- [ ] **Step 1: Write the implementation.**

Replace `qm_drm.c` with:

```c
/* Minimal KMS output: one dumb buffer, one connector, one CRTC, no page
 * flipping (the menu redraws at most a few times a second on input).
 *
 * Structure follows package/batocera/core/batocera-drminfo, this tree's
 * existing small libdrm C binary -- same include path (-I$(STAGING_DIR)/
 * usr/include/drm) and same -ldrm link.
 *
 * IMPORTANT: RetroArch stays alive (paused) while this runs, so it still
 * holds its own DRM fd. drmSetMaster() may therefore fail with EBUSY.
 * That is reported and returned as NULL, NOT worked around -- the daemon
 * treats a non-zero exit as "hand back and resume the game". See the
 * plan's architecture note and the Task 2 spike. */
#include "quickmenu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

struct qm_drm {
    int fd;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeCrtc *saved_crtc;
    drmModeModeInfo mode;
    size_t map_size;
    qm_fb fb;
};

static drmModeConnector *qm_find_connected(int fd, drmModeRes *res)
{
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
        if (conn == NULL)
            continue;
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            return conn;
        drmModeFreeConnector(conn);
    }
    return NULL;
}

static uint32_t qm_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn)
{
    if (conn->encoder_id != 0) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc != NULL) {
            uint32_t id = enc->crtc_id;
            drmModeFreeEncoder(enc);
            if (id != 0)
                return id;
        }
    }
    for (int i = 0; i < conn->count_encoders; i++) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
        if (enc == NULL)
            continue;
        for (int j = 0; j < res->count_crtcs; j++) {
            if (enc->possible_crtcs & (1u << j)) {
                uint32_t id = res->crtcs[j];
                drmModeFreeEncoder(enc);
                return id;
            }
        }
        drmModeFreeEncoder(enc);
    }
    return 0;
}

qm_drm *qm_drm_open(void)
{
    int fd = -1;
    char path[32];
    drmModeRes *res = NULL;

    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        res = drmModeGetResources(fd);
        if (res != NULL && res->count_connectors > 0)
            break;
        if (res != NULL) {
            drmModeFreeResources(res);
            res = NULL;
        }
        close(fd);
        fd = -1;
    }
    if (fd < 0 || res == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: no usable /dev/dri/cardN\n");
        if (fd >= 0) close(fd);
        return NULL;
    }

    if (drmSetMaster(fd) != 0) {
        fprintf(stderr,
                "circuitsword-quickmenu: drmSetMaster failed (%s) -- another "
                "process still owns the display; refusing to continue\n",
                strerror(errno));
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }

    drmModeConnector *conn = qm_find_connected(fd, res);
    if (conn == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: no connected connector\n");
        drmDropMaster(fd);
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }

    uint32_t crtc_id = qm_find_crtc(fd, res, conn);
    if (crtc_id == 0) {
        fprintf(stderr, "circuitsword-quickmenu: no CRTC for connector\n");
        drmModeFreeConnector(conn);
        drmDropMaster(fd);
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }

    qm_drm *d = calloc(1, sizeof(*d));
    if (d == NULL) {
        drmModeFreeConnector(conn);
        drmDropMaster(fd);
        drmModeFreeResources(res);
        close(fd);
        return NULL;
    }
    d->fd = fd;
    d->connector_id = conn->connector_id;
    d->crtc_id = crtc_id;
    d->mode = conn->modes[0];
    d->saved_crtc = drmModeGetCrtc(fd, crtc_id);

    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));
    creq.width = d->mode.hdisplay;
    creq.height = d->mode.vdisplay;
    creq.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        fprintf(stderr, "circuitsword-quickmenu: CREATE_DUMB failed (%s)\n",
                strerror(errno));
        goto fail;
    }
    d->handle = creq.handle;
    d->map_size = creq.size;

    if (drmModeAddFB(fd, creq.width, creq.height, 24, 32, creq.pitch,
                     creq.handle, &d->fb_id) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: drmModeAddFB failed (%s)\n",
                strerror(errno));
        goto fail;
    }

    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = creq.handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        fprintf(stderr, "circuitsword-quickmenu: MAP_DUMB failed (%s)\n",
                strerror(errno));
        goto fail;
    }

    void *map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, (off_t)mreq.offset);
    if (map == MAP_FAILED) {
        fprintf(stderr, "circuitsword-quickmenu: mmap failed (%s)\n",
                strerror(errno));
        goto fail;
    }
    memset(map, 0, creq.size);

    d->fb.pixels = (uint8_t *)map;
    d->fb.width = creq.width;
    d->fb.height = creq.height;
    d->fb.pitch = creq.pitch;

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return d;

fail:
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    if (d->saved_crtc != NULL)
        drmModeFreeCrtc(d->saved_crtc);
    if (d->fb_id != 0)
        drmModeRmFB(fd, d->fb_id);
    if (d->handle != 0) {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = d->handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
    drmDropMaster(fd);
    close(fd);
    free(d);
    return NULL;
}

qm_fb *qm_drm_fb(qm_drm *d)
{
    return (d == NULL) ? NULL : &d->fb;
}

int qm_drm_present(qm_drm *d)
{
    if (d == NULL)
        return -1;
    if (drmModeSetCrtc(d->fd, d->crtc_id, d->fb_id, 0, 0,
                       &d->connector_id, 1, &d->mode) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: drmModeSetCrtc failed (%s)\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

void qm_drm_close(qm_drm *d)
{
    if (d == NULL)
        return;
    if (d->saved_crtc != NULL) {
        drmModeSetCrtc(d->fd, d->saved_crtc->crtc_id, d->saved_crtc->buffer_id,
                       d->saved_crtc->x, d->saved_crtc->y,
                       &d->connector_id, 1, &d->saved_crtc->mode);
        drmModeFreeCrtc(d->saved_crtc);
        d->saved_crtc = NULL;
    }
    if (d->fb.pixels != NULL) {
        munmap(d->fb.pixels, d->map_size);
        d->fb.pixels = NULL;
    }
    if (d->fb_id != 0) {
        drmModeRmFB(d->fd, d->fb_id);
        d->fb_id = 0;
    }
    if (d->handle != 0) {
        struct drm_mode_destroy_dumb dreq;
        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = d->handle;
        drmIoctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        d->handle = 0;
    }
    drmDropMaster(d->fd);
    close(d->fd);
    free(d);
}
```

- [ ] **Step 2: Cross-compile check against Buildroot's libdrm headers.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -15
```

Expected: clean build (this is the design doc's "compiles cleanly against Buildroot's libdrm" criterion), `Installing to target` present. **Nothing is executed — modesetting behaviour, flicker and timing are on-device only (Task 15).**

- [ ] **Step 3: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_drm.c
git commit -m "circuitsword-quickmenu: KMS dumb-buffer output via libdrm

Single connector/CRTC, no page flip. drmSetMaster failure (EBUSY while a
live RetroArch still holds its DRM fd) is a hard error returning NULL,
not something worked around -- the daemon then hands back and resumes.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 11: Implement `quickmenu.c` — menu model, rendering, main loop

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (replace the Task 4 stub entirely)
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` (append a `qm_render` smoke test)
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh` (add `quickmenu.c` to the host compile)

**Interfaces:**
- Consumes: everything from Tasks 7-10 — `qm_fill_rect`, `qm_draw_text`, `qm_text_width`, `qm_input_open/close/poll`, `enum qm_event`, `qm_wifi_get/set`, `qm_volume_get/set`, `qm_brightness_get/set`, `qm_drm_open/fb/present/close`, `qm_state`.
- Produces: the `/usr/bin/circuitsword-quickmenu` binary contract Task 12's daemon relies on — **exit code 0 = normal close, non-zero = could not run**; **SIGTERM = close request, must restore the CRTC and exit within 5 seconds.**

- [ ] **Step 1: Add a host test for `qm_render` first (TDD).**

In `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c`, insert this block immediately before the final `printf("\n%s (%d failure%s)\n", ...)` line:

```c
    printf("qm_render\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .selected = QM_ITEM_WIFI, .wifi_on = 1,
                        .volume = 50, .brightness = 70 };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");

        int lit_wifi_sel = count_nonzero(fb);
        st.selected = QM_ITEM_BRIGHTNESS;
        qm_render(fb, &st);
        check(count_nonzero(fb) != lit_wifi_sel || 1,
              "re-render with a different selection does not crash");

        st.volume = 0; st.brightness = 0; st.wifi_on = 0;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "zeroed state still paints bg");

        st.volume = 100; st.brightness = 100; st.wifi_on = 1;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "maxed state still paints bg");
        free_fb(fb);
    }
```

In `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`, change the `cc` invocation to:

```bash
cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -DQM_NO_MAIN \
   -I"$SRC" \
   "$HERE/test_qm_font.c" "$SRC/qm_font.c" "$SRC/quickmenu.c" \
   -o "$OUT/test_qm_font"
```

`-DQM_NO_MAIN` compiles `quickmenu.c` without its `main()` and without the Linux-only parts, so the pure `qm_render()` is testable on macOS.

- [ ] **Step 2: Run the tests and watch them fail.**

```bash
"/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"; echo "exit=$?"
```

Expected: link error `Undefined symbols ... _qm_render`, non-zero exit.

- [ ] **Step 3: Write the implementation.**

Replace `quickmenu.c` with:

```c
/* circuitsword-quickmenu: 3-item in-game menu for the Circuit-Sword.
 *
 * Launched by rpi-circuitsword.py after it has paused RetroArch and
 * switched VT. Owns the display for as long as it runs.
 *
 * Exit contract (relied on by the daemon):
 *   0        normal close (B pressed, or SIGTERM handled)
 *   non-zero could not run at all (no input device, no DRM master) --
 *            the daemon hands the display back and resumes immediately.
 *   SIGTERM  = "close now"; the CRTC is restored and we exit within the
 *            daemon's 5s watchdog.
 *
 * Compiling with -DQM_NO_MAIN builds only the pure qm_render() half, for
 * the host-side unit tests in tests/run-c-tests.sh. */
#include "quickmenu.h"

#include <stdio.h>

#define QM_STEP 5   /* % per left/right press, volume and brightness alike */

static void qm_format_percent(char *out, size_t n, const char *label, int value)
{
    snprintf(out, n, "%s: %d%%", label, value);
}

static void qm_draw_bar(qm_fb *fb, int x, int y, int w, int h, int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    qm_fill_rect(fb, x, y, w, h, QM_COLOR_BAR_BG);
    qm_fill_rect(fb, x, y, w * percent / 100, h, QM_COLOR_BAR_FG);
}

void qm_render(qm_fb *fb, const qm_state *st)
{
    const int scale = (fb->width >= 640) ? 4 : 2;
    const int line_h = (QM_GLYPH_H + 4) * scale;
    const int margin = 8 * scale;
    const int bar_w = (int)fb->width - 2 * margin;
    const int bar_h = 3 * scale;

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

    const char *title = "CIRCUIT-SWORD";
    qm_draw_text(fb, ((int)fb->width - qm_text_width(title, scale)) / 2,
                 margin, title, scale, QM_COLOR_DIM);

    int y = margin + line_h + line_h / 2;
    char buf[48];

    for (int item = 0; item < QM_ITEM_COUNT; item++) {
        int row_h = line_h + bar_h + 2 * scale;
        if (item == st->selected)
            qm_fill_rect(fb, margin / 2, y - scale,
                         (int)fb->width - margin, row_h, QM_COLOR_SEL_BG);

        uint32_t fg = (item == st->selected) ? QM_COLOR_FG : QM_COLOR_DIM;

        if (item == QM_ITEM_WIFI) {
            snprintf(buf, sizeof(buf), "WIFI: %s", st->wifi_on ? "ON" : "OFF");
            qm_draw_text(fb, margin, y, buf, scale, fg);
        } else if (item == QM_ITEM_VOLUME) {
            qm_format_percent(buf, sizeof(buf), "VOLUME", st->volume);
            qm_draw_text(fb, margin, y, buf, scale, fg);
            qm_draw_bar(fb, margin, y + line_h - 2 * scale, bar_w, bar_h,
                        st->volume);
        } else {
            qm_format_percent(buf, sizeof(buf), "BRIGHTNESS", st->brightness);
            qm_draw_text(fb, margin, y, buf, scale, fg);
            qm_draw_bar(fb, margin, y + line_h - 2 * scale, bar_w, bar_h,
                        st->brightness);
        }
        y += row_h + line_h / 2;
    }

    const char *hint = "A: SELECT   B: BACK   LEFT/RIGHT: ADJUST";
    qm_draw_text(fb, ((int)fb->width - qm_text_width(hint, 1)) / 2,
                 (int)fb->height - margin - QM_GLYPH_H, hint, 1, QM_COLOR_DIM);
}

#ifndef QM_NO_MAIN

#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t qm_quit = 0;

static void qm_on_signal(int signum)
{
    (void)signum;
    qm_quit = 1;
}

static int qm_clamp_pct(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

int main(void)
{
    struct sigaction sa;
    sa.sa_handler = qm_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int input_fd = qm_input_open();
    if (input_fd < 0) {
        fprintf(stderr, "circuitsword-quickmenu: no input device, aborting\n");
        return 2;
    }

    qm_drm *drm = qm_drm_open();
    if (drm == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: no display, aborting\n");
        qm_input_close(input_fd);
        return 3;
    }

    qm_state st;
    st.selected = QM_ITEM_WIFI;
    st.wifi_on = qm_wifi_get();
    if (st.wifi_on < 0) st.wifi_on = 0;
    st.volume = qm_volume_get();
    if (st.volume < 0) st.volume = 0;
    st.brightness = qm_brightness_get();
    if (st.brightness < 0) st.brightness = 0;

    qm_fb *fb = qm_drm_fb(drm);
    qm_render(fb, &st);
    if (qm_drm_present(drm) != 0) {
        qm_drm_close(drm);
        qm_input_close(input_fd);
        return 4;
    }

    while (!qm_quit) {
        enum qm_event ev = qm_input_poll(input_fd, 100);
        if (ev == QM_EV_NONE)
            continue;

        int dirty = 1;
        switch (ev) {
        case QM_EV_B:
            qm_quit = 1;
            dirty = 0;
            break;
        case QM_EV_UP:
            st.selected = (st.selected + QM_ITEM_COUNT - 1) % QM_ITEM_COUNT;
            break;
        case QM_EV_DOWN:
            st.selected = (st.selected + 1) % QM_ITEM_COUNT;
            break;
        case QM_EV_A:
            /* A toggles WiFi; on the sliders it is a no-op, since
             * left/right already adjust them live. */
            if (st.selected == QM_ITEM_WIFI) {
                int want = st.wifi_on ? 0 : 1;
                if (qm_wifi_set(want) == 0)
                    st.wifi_on = want;
            } else {
                dirty = 0;
            }
            break;
        case QM_EV_LEFT:
        case QM_EV_RIGHT: {
            int delta = (ev == QM_EV_RIGHT) ? QM_STEP : -QM_STEP;
            if (st.selected == QM_ITEM_WIFI) {
                int want = st.wifi_on ? 0 : 1;
                if (qm_wifi_set(want) == 0)
                    st.wifi_on = want;
            } else if (st.selected == QM_ITEM_VOLUME) {
                int want = qm_clamp_pct(st.volume + delta);
                if (qm_volume_set(want) == 0)
                    st.volume = want;
            } else {
                int want = qm_clamp_pct(st.brightness + delta);
                if (qm_brightness_set(want) == 0)
                    st.brightness = want;
            }
            break;
        }
        default:
            dirty = 0;
            break;
        }

        if (dirty && !qm_quit) {
            qm_render(fb, &st);
            qm_drm_present(drm);
        }
    }

    qm_drm_close(drm);
    qm_input_close(input_fd);
    return 0;
}

#endif /* QM_NO_MAIN */
```

- [ ] **Step 4: Run the host tests — all must pass.**

```bash
"/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"; echo "exit=$?"
```

Expected: `PASSED (0 failures)`, `exit=0`.

- [ ] **Step 5: Cross-compile check.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: clean build, `Installing to target` present.

- [ ] **Step 6: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "circuitsword-quickmenu: menu model, rendering and main loop

Exit contract for the daemon: 0 = normal close, non-zero = could not run,
SIGTERM = close now (restores the CRTC before exiting).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 12: Daemon unit tests first — debounce, busy flag, RetroArch detection

Pure-Python logic, fully testable on the host with mocked serial/subprocess/socket. Written before the daemon code.

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`

**Interfaces:**
- Consumes: the daemon module at `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`, loaded by path (the filename has a hyphen, so a normal `import` will not work — `importlib.util.spec_from_file_location` is used).
- Produces: the test suite Task 13 must make pass. Names it asserts on: `ModeButton` (with `.update(raw, now) -> bool` and `.reset()`), `retroarch_running() -> bool`, `MODE_DEBOUNCE_S`, `QUICKMENU_WATCHDOG_S`, `RETROARCH_CMD_PORT`, `QUICKMENU_BIN`.

- [ ] **Step 1: Write the tests.**

`/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`:

```python
#!/usr/bin/env python3
"""Host-side unit tests for the Phase 4 quick-menu logic inside
rpi-circuitsword.py. No hardware, no serial port, no device: `serial`
and `gpiod` are stubbed before the module is loaded, and every syscall
the tests touch is monkeypatched.

Run:  python3 tests/test_quickmenu_logic.py
"""
import importlib.util
import os
import sys
import types
import unittest

BUILD_TREE = os.environ.get(
    "BATOCERA_SRC", "/Users/bas/batocera-build-wifi/batocera.linux")
DAEMON_PATH = os.path.join(
    BUILD_TREE, "package/batocera/utils/rpigpioswitch/rpi-circuitsword.py")


def load_daemon():
    """Import rpi-circuitsword.py by path, with `serial` stubbed out."""
    if "serial" not in sys.modules:
        stub = types.ModuleType("serial")

        class SerialException(Exception):
            pass

        class Serial:
            def __init__(self, *a, **kw):
                self.is_open = True

            def reset_input_buffer(self):
                pass

            def write(self, data):
                return len(data)

            def read(self, n):
                return b"\x00" * n

        stub.SerialException = SerialException
        stub.Serial = Serial
        sys.modules["serial"] = stub

    spec = importlib.util.spec_from_file_location("rpi_circuitsword", DAEMON_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


cs = load_daemon()


class TestConstants(unittest.TestCase):
    def test_debounce_is_in_the_designed_range(self):
        # Design doc: ~50-100ms for the plain MODE pushbutton, distinct
        # from the 800ms mechanical power switch.
        self.assertGreaterEqual(cs.MODE_DEBOUNCE_S, 0.050)
        self.assertLessEqual(cs.MODE_DEBOUNCE_S, 0.100)

    def test_watchdog_matches_the_phase3_join_timeout_convention(self):
        self.assertEqual(cs.QUICKMENU_WATCHDOG_S, 5)

    def test_retroarch_command_port(self):
        self.assertEqual(cs.RETROARCH_CMD_PORT, 55355)

    def test_quickmenu_binary_path(self):
        self.assertEqual(cs.QUICKMENU_BIN, "/usr/bin/circuitsword-quickmenu")


class TestModeButton(unittest.TestCase):
    def setUp(self):
        self.btn = cs.ModeButton(debounce_s=0.08)

    def test_idle_never_fires(self):
        for i in range(20):
            self.assertFalse(self.btn.update(False, i * 0.01))

    def test_press_fires_once_after_debounce(self):
        self.assertFalse(self.btn.update(True, 1.00))     # edge seen
        self.assertFalse(self.btn.update(True, 1.05))     # not stable yet
        self.assertTrue(self.btn.update(True, 1.10))      # stable -> fire
        self.assertFalse(self.btn.update(True, 1.20))     # held: no repeat
        self.assertFalse(self.btn.update(True, 5.00))     # still held

    def test_release_does_not_fire(self):
        self.btn.update(True, 1.00)
        self.btn.update(True, 1.10)
        self.assertFalse(self.btn.update(False, 2.00))
        self.assertFalse(self.btn.update(False, 2.10))

    def test_bounce_shorter_than_debounce_is_rejected(self):
        self.assertFalse(self.btn.update(True, 1.00))
        self.assertFalse(self.btn.update(False, 1.02))
        self.assertFalse(self.btn.update(True, 1.04))
        self.assertFalse(self.btn.update(False, 1.06))
        # settled low for longer than debounce: still no press
        self.assertFalse(self.btn.update(False, 1.30))

    def test_two_separate_presses_fire_twice(self):
        self.btn.update(True, 1.00)
        self.assertTrue(self.btn.update(True, 1.10))
        self.btn.update(False, 2.00)
        self.btn.update(False, 2.10)
        self.btn.update(True, 3.00)
        self.assertTrue(self.btn.update(True, 3.10))

    def test_reset_swallows_a_still_held_button(self):
        self.btn.update(True, 1.00)
        self.assertTrue(self.btn.update(True, 1.10))
        self.btn.reset()
        # Button still physically held after reset: must not re-fire.
        self.assertFalse(self.btn.update(True, 1.20))
        self.assertFalse(self.btn.update(True, 1.90))


class TestRetroarchRunning(unittest.TestCase):
    def setUp(self):
        self.real_listdir = os.listdir
        self.real_open = cs.open if hasattr(cs, "open") else open

    def _patch_proc(self, procs):
        """procs: {pid_str: comm_str}"""
        import builtins
        real_open = builtins.open

        def fake_listdir(path):
            if path == "/proc":
                return list(procs.keys()) + ["self", "cpuinfo"]
            return self.real_listdir(path)

        def fake_open(path, *a, **kw):
            if isinstance(path, str) and path.startswith("/proc/") \
                    and path.endswith("/comm"):
                pid = path.split("/")[2]
                if pid not in procs:
                    raise OSError("no such process")
                import io
                return io.StringIO(procs[pid] + "\n")
            return real_open(path, *a, **kw)

        os.listdir = fake_listdir
        builtins.open = fake_open
        self.addCleanup(setattr, os, "listdir", self.real_listdir)
        self.addCleanup(setattr, builtins, "open", real_open)

    def test_true_when_retroarch_present(self):
        self._patch_proc({"101": "sh", "202": "retroarch"})
        self.assertTrue(cs.retroarch_running())

    def test_true_for_truncated_comm(self):
        # /proc/<pid>/comm is capped at 15 chars.
        self._patch_proc({"303": "retroarch-core"})
        self.assertTrue(cs.retroarch_running())

    def test_false_when_only_es_running(self):
        self._patch_proc({"101": "emulationstatio", "102": "connmand"})
        self.assertFalse(cs.retroarch_running())

    def test_false_on_empty_proc(self):
        self._patch_proc({})
        self.assertFalse(cs.retroarch_running())


class TestSessionAbortsWhenUnreachable(unittest.TestCase):
    def test_no_pause_and_no_launch_when_retroarch_does_not_answer(self):
        calls = []
        cs.retroarch_cmd_query = lambda cmd, timeout_s=0.5: None
        cs.send_pause_toggle = lambda: calls.append("pause") or True
        cs.vt_activate = lambda vt: calls.append(("vt", vt)) or True
        cs.vt_current = lambda: 1

        class Boom:
            def __init__(self, *a, **kw):
                raise AssertionError("quickmenu must not be launched")

        cs.subprocess.Popen = Boom
        cs.run_quickmenu_session()
        self.assertEqual(calls, [],
                         "nothing may happen before the game is confirmed paused")


if __name__ == "__main__":
    unittest.main(verbosity=2)
```

- [ ] **Step 2: Run the tests and watch them fail.**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py 2>&1 | tail -20; echo "exit=${PIPESTATUS[0]}"
```

Expected: an `AttributeError: module 'rpi_circuitsword' has no attribute 'MODE_DEBOUNCE_S'` (or similar) and a non-zero exit — nothing is implemented yet.

- [ ] **Step 3: No commit.** These files live in the non-git project directory (Global Constraint 6).

---

### Task 13: Extend `rpi-circuitsword.py` with the quick-menu thread

**Files:**
- Modify: `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` — insert a new section between the power-switch section (ends line 365, `request.release()`) and the `# Entry point` comment block (line 368), and add one entry to the `threads` list in `main()` (line 388-393).

**Interfaces:**
- Consumes: existing daemon helpers `read_mode_button() -> bool` (line 293) and the `stop_event: threading.Event` convention used by `fan_thread`/`battery_bridge`/`backlight_bridge`/`switch_monitor`; `/usr/bin/circuitsword-quickmenu` from Task 11; RetroArch UDP 55355 from Task 3.
- Produces: `ModeButton`, `retroarch_running()`, `retroarch_cmd_query()`, `send_pause_toggle()`, `vt_current()`, `vt_activate()`, `run_quickmenu_session()`, `quickmenu_thread(stop_event)`, and the constants `MODE_DEBOUNCE_S`, `QUICKMENU_WATCHDOG_S`, `RETROARCH_CMD_PORT`, `QUICKMENU_BIN`, `VT_QUICKMENU`.

- [ ] **Step 1: Add the imports the new section needs.**

At the top of the file, change:

```python
import os
import sys
import time
import threading
import serial  # python3-serial, already a Batocera Buildroot package
```

to:

```python
import fcntl
import os
import struct
import socket
import subprocess
import sys
import time
import threading
import serial  # python3-serial, already a Batocera Buildroot package
```

- [ ] **Step 2: Insert the quick-menu section.**

Insert immediately after `switch_monitor()`'s closing `request.release()` line and before the `# ============================================================` / `# Entry point.` comment block:

```python
# ============================================================
# In-game quick menu (Phase 4).
#
# The MODE button is owned end-to-end here, because this daemon already
# polls CMD_GET_STATUS over serial regardless of which VT is active.
#
# Open : confirm RetroArch is alive -> confirm its network command port
#        answers -> PAUSE_TOGGLE -> VT switch -> launch
#        circuitsword-quickmenu and wait.
# Close: quickmenu exits (B, SIGTERM from a second MODE press, crash, or
#        the 5s watchdog kill) -> VT switch back -> PAUSE_TOGGLE.
#
# Error handling is deliberately fail-safe in one direction: we never
# blank the screen without having actually paused first, and we always
# hand the display back and resume, even if the menu misbehaves.
# See docs/superpowers/specs/2026-08-06-quickmenu-design.md.
# ============================================================
QUICKMENU_BIN = "/usr/bin/circuitsword-quickmenu"
MODE_DEBOUNCE_S = 0.08          # ~80ms: plain pushbutton, NOT the 800ms
                                # mechanical power switch. Starting
                                # estimate, needs on-device tuning.
QUICKMENU_POLL_INTERVAL_S = 0.05
QUICKMENU_WATCHDOG_S = 5        # same 5s convention as main()'s t.join(timeout=5)

RETROARCH_CMD_HOST = "127.0.0.1"
RETROARCH_CMD_PORT = 55355      # RetroArch default; enabled for this image
                                # in configgen's libretroRetroarchCustom.py
                                # (network_cmd_enable/network_cmd_port).

CONSOLE_DEV = "/dev/tty0"
VT_GETSTATE = 0x5603
VT_ACTIVATE = 0x5606
VT_WAITACTIVE = 0x5607
VT_QUICKMENU = 6                # /etc/inittab runs gettys on tty4 and tty5
                                # only, so tty6 is free.


class ModeButton:
    """Edge-detecting debouncer for the Arduino MODE button.

    Feed it raw CMD_GET_STATUS samples via update(); it returns True
    exactly once per physical press, after the level has been stable for
    debounce_s. Pure logic, no I/O -- unit-tested in
    tests/test_quickmenu_logic.py.
    """

    def __init__(self, debounce_s: float = MODE_DEBOUNCE_S):
        self.debounce_s = debounce_s
        self._stable = False
        self._candidate = False
        self._since = None

    def reset(self):
        """Forget the current press. Called after a menu session so a
        still-held MODE button cannot immediately re-open the menu."""
        self._stable = True
        self._candidate = True
        self._since = None

    def update(self, raw: bool, now: float) -> bool:
        raw = bool(raw)
        if raw != self._candidate:
            self._candidate = raw
            self._since = now
            return False
        if self._since is None:
            return False
        if (now - self._since) < self.debounce_s:
            return False
        if self._candidate != self._stable:
            self._stable = self._candidate
            self._since = None
            return self._stable   # rising edge only
        return False


def retroarch_running() -> bool:
    """True if a retroarch process exists. Scans /proc directly: busybox
    is the only ps/grep on this image and pgrep is not installed, so
    nothing external is depended on here."""
    try:
        entries = os.listdir("/proc")
    except OSError:
        return False
    for entry in entries:
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/comm", "r") as f:
                if f.read().strip().startswith("retroarch"):
                    return True
        except OSError:
            continue
    return False


def retroarch_cmd_query(cmd: bytes, timeout_s: float = 0.5):
    """Send a RetroArch network command and wait for its reply.
    Returns the reply bytes, or None if the port did not answer.
    Used with b"GET_STATUS" as a reachability probe *before* pausing --
    plain UDP sends cannot themselves report an unreachable port."""
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout_s)
        sock.connect((RETROARCH_CMD_HOST, RETROARCH_CMD_PORT))
        sock.send(cmd)
        return sock.recv(1024)
    except (OSError, socket.timeout) as e:
        print(f"[rpi-circuitsword] retroarch cmd {cmd!r} no reply: {e}", file=sys.stderr)
        return None
    finally:
        if sock is not None:
            sock.close()


def send_pause_toggle() -> bool:
    """Fire-and-forget PAUSE_TOGGLE. Reachability must already have been
    established with retroarch_cmd_query(b"GET_STATUS")."""
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.5)
        sock.connect((RETROARCH_CMD_HOST, RETROARCH_CMD_PORT))
        sock.send(b"PAUSE_TOGGLE")
        return True
    except OSError as e:
        print(f"[rpi-circuitsword] PAUSE_TOGGLE failed: {e}", file=sys.stderr)
        return False
    finally:
        if sock is not None:
            sock.close()


def vt_current() -> int:
    """Active virtual terminal number, or -1 if unavailable."""
    try:
        with open(CONSOLE_DEV, "wb") as f:
            buf = fcntl.ioctl(f, VT_GETSTATE, struct.pack("HHH", 0, 0, 0))
        return struct.unpack("HHH", buf)[0]
    except (OSError, ValueError) as e:
        print(f"[rpi-circuitsword] VT_GETSTATE failed: {e}", file=sys.stderr)
        return -1


def vt_activate(vt: int) -> bool:
    try:
        with open(CONSOLE_DEV, "wb") as f:
            fcntl.ioctl(f, VT_ACTIVATE, vt)
            fcntl.ioctl(f, VT_WAITACTIVE, vt)
        return True
    except (OSError, ValueError) as e:
        print(f"[rpi-circuitsword] VT_ACTIVATE({vt}) failed: {e}", file=sys.stderr)
        return False


def run_quickmenu_session():
    """One full open/close cycle. Blocks until the menu is closed and the
    game has been resumed. Never returns with the game left paused or the
    display left handed away."""
    # 1. Reachability BEFORE anything visible happens. If RetroArch's
    #    command port does not answer, abort: never blank the screen
    #    without having actually paused the game first.
    if retroarch_cmd_query(b"GET_STATUS") is None:
        print("[rpi-circuitsword] quickmenu: RetroArch command port silent, aborting",
              file=sys.stderr)
        return

    if not send_pause_toggle():
        print("[rpi-circuitsword] quickmenu: PAUSE_TOGGLE failed, aborting",
              file=sys.stderr)
        return

    previous_vt = vt_current()
    switched = vt_activate(VT_QUICKMENU)
    if not switched:
        # Could not hand the display over -- resume immediately.
        send_pause_toggle()
        return

    proc = None
    try:
        proc = subprocess.Popen([QUICKMENU_BIN])
    except OSError as e:
        print(f"[rpi-circuitsword] quickmenu: launch failed: {e}", file=sys.stderr)

    if proc is not None:
        button = ModeButton()
        closing_since = None
        while True:
            rc = proc.poll()
            if rc is not None:
                if rc != 0:
                    print(f"[rpi-circuitsword] quickmenu exited rc={rc}", file=sys.stderr)
                break
            # A second MODE press is the close signal: SIGTERM the menu.
            if button.update(read_mode_button(), time.monotonic()) and closing_since is None:
                closing_since = time.monotonic()
                print("[rpi-circuitsword] quickmenu: MODE again -> closing", file=sys.stderr)
                proc.terminate()
            # Watchdog: 5s after a close signal, kill it regardless. The
            # device must never get stuck on a blank screen.
            if closing_since is not None and \
                    (time.monotonic() - closing_since) >= QUICKMENU_WATCHDOG_S:
                print("[rpi-circuitsword] quickmenu: watchdog expired, killing",
                      file=sys.stderr)
                proc.kill()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
                break
            time.sleep(QUICKMENU_POLL_INTERVAL_S)

    # 2. Hand the display back and resume -- unconditionally, whatever
    #    happened above.
    if previous_vt > 0:
        vt_activate(previous_vt)
    send_pause_toggle()


def quickmenu_thread(stop_event: threading.Event):
    button = ModeButton()
    busy_lock = threading.Lock()
    busy = False

    while not stop_event.is_set():
        pressed = button.update(read_mode_button(), time.monotonic())
        if pressed:
            with busy_lock:
                already = busy
                if not already:
                    busy = True
            if already:
                # MODE arrived mid-transition: ignore, so an open can't
                # race a close.
                print("[rpi-circuitsword] quickmenu: MODE ignored (busy)",
                      file=sys.stderr)
            else:
                try:
                    if retroarch_running():
                        run_quickmenu_session()
                    else:
                        # ES already exposes wifi/volume/brightness in its
                        # own settings -- nothing to do here.
                        print("[rpi-circuitsword] quickmenu: MODE with no game running, ignored",
                              file=sys.stderr)
                finally:
                    # Swallow a still-held MODE so the menu can't
                    # immediately re-open on the same physical press.
                    button.reset()
                    with busy_lock:
                        busy = False
        stop_event.wait(QUICKMENU_POLL_INTERVAL_S)
```

- [ ] **Step 3: Register the thread in `main()`.**

Change the `threads` list to:

```python
    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
    ]
```

The existing `for t in threads: t.join(timeout=5)` at the end of `main()` is unchanged — that is the 5-second convention `QUICKMENU_WATCHDOG_S` matches, per the design doc.

- [ ] **Step 4: Run the Python unit tests — all must pass.**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py 2>&1 | tail -25; echo "exit=${PIPESTATUS[0]}"
```

Expected: `OK` with all tests passing, `exit=0`.

- [ ] **Step 5: Syntax-check the daemon.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
python3 -m py_compile package/batocera/utils/rpigpioswitch/rpi-circuitsword.py && echo "PY-COMPILE-OK"
python3 - <<'EOF'
import re
src = open("package/batocera/utils/rpigpioswitch/rpi-circuitsword.py").read()
for name in ["class ModeButton", "def retroarch_running", "def retroarch_cmd_query",
             "def send_pause_toggle", "def vt_current", "def vt_activate",
             "def run_quickmenu_session", "def quickmenu_thread",
             'name="quickmenu"']:
    assert name in src, f"MISSING: {name}"
print("ALL-SYMBOLS-PRESENT")
EOF
```

Expected: `PY-COMPILE-OK` then `ALL-SYMBOLS-PRESENT`.

- [ ] **Step 6: Commit.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: add the Phase 4 quick-menu thread

MODE press -> debounce (80ms) -> RetroArch-running check (/proc scan, no
pgrep on this image) -> GET_STATUS reachability probe -> PAUSE_TOGGLE ->
VT switch -> circuitsword-quickmenu -> VT switch back -> PAUSE_TOGGLE.

Fail-safe: the display is never handed away unless the game is confirmed
paused first, and it is always handed back and resumed afterwards. A
second MODE press SIGTERMs the menu; 5s later it is SIGKILLed, matching
main()'s existing 5s thread-join convention.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 14: Full image build and flash

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 14)

**Interfaces:**
- Consumes: everything from Tasks 3-13, all committed and captured in `batocera-linux.patch`.
- Produces: a flashed device carrying `/usr/bin/circuitsword-quickmenu`, the extended daemon, and the network-command-enabled configgen.

- [ ] **Step 1: Verify the patch is complete before building.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git status --short
P="/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
for s in network_cmd_enable circuitsword-quickmenu.mk qm_font.c qm_drm.c qm_input.c qm_settings.c quickmenu.h "def quickmenu_thread" "class ModeButton"; do
    printf "%-28s %s\n" "$s" "$(grep -c -- "$s" "$P")"
done
```

Expected: `git status --short` prints nothing (clean tree — everything committed), and every listed string has a count `>= 1`. A `0` anywhere means a commit was missed; go back and fix before spending hours on a build.

- [ ] **Step 2: Kick off the full image build.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
./build-image.sh
```

Expected: `Started, PID <n>` and the log path. This is a multi-hour Docker Buildroot build (warm ccache: shorter). It runs at the project's existing `BR2_JLEVEL=4` **on the host** — Global Constraint 2's `-j2` cap applies to on-device builds, of which there are none in this phase.

- [ ] **Step 3: Wait for the build and confirm it succeeded.**

```bash
tail -40 "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
grep -c "circuitsword-quickmenu" "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
```

Expected: the log ends with genimage/image creation and no `*** Error`, and the grep count is `>= 2` (extract + install lines for the new package).

- [ ] **Step 4: Extract and flash.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
./extract-artifacts.sh
ls -la "/Users/bas/Circuit-Sword Batocera/output/"*.img.gz
```

Expected: a `batocera-bcm2837-*.img.gz` on the host. Flash it to the SD card with the same procedure used in Phases 2/3, then boot the device.

- [ ] **Step 5: Confirm the pieces landed on the device.**

```bash
ssh root@batocera.local "ls -l /usr/bin/circuitsword-quickmenu; grep -c network_cmd /userdata/system/configs/retroarch/retroarchcustom.cfg; grep -c quickmenu_thread /usr/bin/rpi-circuitsword"
```

Expected: the binary exists and is `-rwxr-xr-x`; the `retroarchcustom.cfg` grep is `2` **after a game has been launched at least once** (configgen rewrites that file at launch, so launch and exit a game first if it is `0`); the daemon grep is `1`.

- [ ] **Step 6: Record in the findings log.** Append to `PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
### Task 14: full image built and flashed
Build duration: <fill in>. circuitsword-quickmenu present at
/usr/bin/circuitsword-quickmenu. network_cmd_enable/port present in the
generated retroarchcustom.cfg after one game launch: yes|no.
```

- [ ] **Step 7: No commit.** Nothing in the build tree changed in this task; verify with `cd /Users/bas/batocera-build-wifi/batocera.linux && git status --short` (expect no output).

---

### Task 15: On-device validation

**Nothing in this task is verifiable off-device.** Every check below requires the physical Circuit-Sword. Do not mark any of them done from log inspection or reasoning alone — each needs an observed result on the real screen.

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 15 with the real observations)

**Interfaces:**
- Consumes: the flashed device from Task 14.
- Produces: the recorded pass/fail matrix; any fix needed loops back to the relevant earlier task.

- [ ] **Step 1: `PAUSE_TOGGLE` reliability.** Launch a game. From SSH:

```bash
ssh root@batocera.local "python3 -c \"
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(1)
s.connect(('127.0.0.1',55355)); s.send(b'GET_STATUS'); print(s.recv(1024))
s.send(b'PAUSE_TOGGLE'); print('sent')\""
```

Expected: a `GET_STATUS PLAYING ...` reply, and the game visibly freezes. Send `PAUSE_TOGGLE` again; it must visibly resume. Record: does it reliably toggle, and is it ever missed?

- [ ] **Step 2: MODE opens the menu.** With a game running, press MODE on the device. Expected: game pauses, screen switches, the 3-item menu appears with WIFI/VOLUME/BRIGHTNESS showing the current real values. Record how long the transition takes and whether any flicker, garbage frame, or black flash is visible — the design doc explicitly lists "VT-switch timing and any visible flicker" as unverified until now.

- [ ] **Step 3: Navigation and each item.** In the menu: D-pad up/down moves the highlight; left/right adjusts volume and brightness in 5% steps with the bar tracking; A on WIFI toggles it. Verify independently that the change stuck:

```bash
ssh root@batocera.local "batocera-settings-get wifi.enabled; batocera-audio getSystemVolume; cat /sys/class/backlight/circuitsword-backlight/brightness"
```

Expected: values match what the menu displayed. Record whether button-navigation feel is right (one step per tap, not runaway repeats — that is `qm_input_poll`'s hat-release handling, Task 8).

- [ ] **Step 4: Both close paths.** (a) Press B — expect the menu to close, the screen to return to the game, and the game to resume. (b) Reopen, then press MODE again — same result. Record which path is faster/cleaner.

- [ ] **Step 5: Watchdog fallback (simulated crash).** With the menu open, from SSH:

```bash
ssh root@batocera.local "kill -STOP \$(pidof circuitsword-quickmenu)"
```

Then press MODE on the device to request a close. Expected: the daemon SIGTERMs (ignored, process is stopped), waits `QUICKMENU_WATCHDOG_S` = 5 seconds, SIGKILLs, switches the VT back, and sends `PAUSE_TOGGLE` — **the device ends up on the running game, never on a blank screen.** Also test the hard-crash variant:

```bash
ssh root@batocera.local "kill -SEGV \$(pidof circuitsword-quickmenu)"
```

Expected: `proc.poll()` returns immediately, the daemon hands back and resumes with no watchdog wait.

- [ ] **Step 6: MODE from EmulationStation is a no-op.** Exit to ES and press MODE. Expected: nothing happens on screen; `ssh root@batocera.local "logread | grep quickmenu | tail -5"` shows `MODE with no game running, ignored`.

- [ ] **Step 7: MODE mid-transition is ignored.** With a game running, press MODE twice in rapid succession (well under a second). Expected: the menu opens once; the log shows either a `busy` line or no second open. Confirm the device is not left in a half-transitioned state.

- [ ] **Step 8: Debounce tuning.** If Step 7 or Step 2 shows double-fires or missed presses, adjust `MODE_DEBOUNCE_S` in `rpi-circuitsword.py` (design-doc range: 0.050-0.100), edit the file **on the device** at `/usr/bin/rpi-circuitsword` for the measurement loop only, then port the final value back into the build tree, re-run Task 12's tests, commit, and regenerate the patch:

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: tune MODE_DEBOUNCE_S to the measured value

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

- [ ] **Step 9: Record the full matrix.** Append to `PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
### Task 15: on-device validation
| Check | Result | Notes |
| --- | --- | --- |
| PAUSE_TOGGLE reliability | pass/fail | |
| MODE opens menu, transition time | pass/fail | <ms>, flicker: yes/no |
| WiFi toggle takes effect | pass/fail | |
| Volume adjust takes effect | pass/fail | |
| Brightness adjust takes effect | pass/fail | |
| Close via B | pass/fail | |
| Close via second MODE | pass/fail | |
| Watchdog: SIGSTOP'd menu | pass/fail | recovers to running game? |
| Watchdog: SIGSEGV'd menu | pass/fail | |
| MODE in ES is a no-op | pass/fail | |
| MODE mid-transition ignored | pass/fail | |
| Final MODE_DEBOUNCE_S | <value> | measured, not estimated |
```

- [ ] **Step 10: Close out the phase.** Update `docs/superpowers/specs/2026-08-06-quickmenu-design.md` and `docs/superpowers/specs/2026-07-28-batocera-port-design.md` per the project's "update the open gaps section when a gap is resolved" convention: record (a) that "VT switch" in this design means VT switch + `drmSetMaster()`, with the Task 2 spike result, (b) that RetroArch network commands had to be enabled in configgen, (c) that volume goes through PipeWire/`batocera-audio`, not `amixer`, on this board. Mark Phase 4 done in the main design doc's phasing section. **No git commit** — both files are in the non-git project directory (Global Constraint 6).

---

## Self-review

**Spec coverage** — every requirement in `2026-08-06-quickmenu-design.md` maps to a task:

| Design-doc requirement | Task |
| --- | --- |
| MODE detected only while RetroArch runs | 13 (`retroarch_running()`), tested in 12, on-device 15.6 |
| Debounce ~50-100ms | 13 (`MODE_DEBOUNCE_S = 0.08`), tested in 12, tuned in 15.8 |
| Busy/in-transition flag | 13 (`busy_lock`/`busy` in `quickmenu_thread`), on-device 15.7 |
| Open: PAUSE_TOGGLE → VT switch → launch → wait | 13 (`run_quickmenu_session`), on-device 15.2 |
| Close: VT switch back → PAUSE_TOGGLE | 13, on-device 15.4 |
| 5s watchdog matching main()'s join timeout | 13 (`QUICKMENU_WATCHDOG_S = 5`), asserted in 12, on-device 15.5 |
| C program, libdrm not SDL2 | 4 (`.mk` links `-ldrm` only), 10 |
| 3 items + bottom hint line | 11 (`qm_render`, hint `"A: SELECT   B: BACK   LEFT/RIGHT: ADJUST"`) |
| D-pad/A/B via evdev, no MODE in the program | 8 (`qm_input.c` handles only hat0 + codes 288/289) |
| WiFi via `wifi.enabled` + connman reload as `S08connman` does | 9 (`qm_wifi_set`) |
| Volume via the same control the system already uses | 9 (`qm_volume_get/set`; PipeWire finding documented) |
| Brightness via `/sys/class/backlight/circuitsword-backlight/brightness` | 9 (`qm_brightness_get/set`, `max_brightness`-scaled like `batocera-brightness`) |
| Exits on B or relayed MODE | 11 (`QM_EV_B` and SIGTERM), 13 (`proc.terminate()`) |
| Error: RetroArch not running → no-op | 13, on-device 15.6 |
| Error: PAUSE_TOGGLE fails → abort before handoff | 13 (`retroarch_cmd_query` probe first), tested in 12 (`TestSessionAbortsWhenUnreachable`) |
| Error: crash/hang → forced hand-back | 13, on-device 15.5 |
| Error: MODE mid-transition ignored | 13, on-device 15.7 |
| Off-device: compiles against Buildroot libdrm | 10.2 |
| Off-device: Python syntax | 13.5 (`py_compile`) |
| Off-device: debounce/busy unit tests | 12 |
| On-device only, flagged: VT timing/flicker, PAUSE_TOGGLE reliability, feel, watchdog, debounce | 15.2/15.1/15.3/15.5/15.8 |
| Out of scope: WiFi scanning/password, ES/RetroArch existing paths, calibration, low battery | no task touches any of these |

**Placeholder scan:** no "TBD", no "add appropriate error handling", no "similar to Task N". Every code block is complete and self-contained; every command is literal. The only intentional fill-in-later blanks are inside findings-log *templates* (`<fill in>`, `pass/fail`), which the plan explicitly states are populated during execution, not now.

**Type/signature consistency:** `quickmenu.h` (Task 5) is the single source of truth. Verified across tasks: `qm_fb` fields `pixels/width/height/pitch` used identically in `qm_font.c`, `qm_drm.c`, `quickmenu.c`, and the host tests; `qm_state` is passed as `const qm_state *` by both `qm_render`'s declaration and its definition and the test's call site; `qm_drm_open()` returns `qm_drm *` (NULL on failure) and is checked for NULL at its only call site; `enum qm_event` values produced by `qm_input_poll` are exactly the ones `main()` switches on; `QM_GLYPH_W/H/ADVANCE` are used consistently by `qm_draw_char`, `qm_text_width` and the tests. On the Python side, `ModeButton.update(raw, now) -> bool` and `.reset()`, `retroarch_running() -> bool`, `retroarch_cmd_query(cmd, timeout_s) -> bytes|None`, `send_pause_toggle() -> bool`, `vt_current() -> int`, `vt_activate(vt) -> bool` are used with matching signatures in Task 12's tests and Task 13's implementation. The `/usr/bin/circuitsword-quickmenu` path is identical in Task 4's `.mk` install rule, Task 13's `QUICKMENU_BIN`, and Task 12's constant test. Port `55355` is identical in Task 3's configgen setting and Task 13's `RETROARCH_CMD_PORT`.
