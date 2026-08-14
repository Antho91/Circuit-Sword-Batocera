# Circuit-Sword In-Game Quick Menu (Phase 4, v2 — Wayland overlay) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-game quick menu (WiFi toggle / Volume / Brightness) to the Circuit-Sword Batocera image, opened by the Arduino MODE button while a game is running, drawn as a true Wayland overlay-layer surface on top of the running RetroArch client.

**Architecture:** This Batocera 43.1 tree runs `labwc` (wlroots-based Wayland compositor) continuously, and RetroArch is a Wayland *client* of it — labwc never stops being DRM master. So `circuitsword-quickmenu` is a small standalone `libwayland-client` program: it connects to the same compositor, requests a `zwlr_layer_surface_v1` on the **overlay** layer anchored to all four edges, draws a full-screen opaque menu into a `wl_shm` XRGB8888 buffer, and reads the Arduino joystick directly via evdev (`EVIOCGRAB`). The Phase-3 daemon `rpi-circuitsword.py` owns the MODE button: on a debounced press with RetroArch running it probes RetroArch's UDP command port, sends `PAUSE_TOGGLE`, launches the menu, waits, then sends `PAUSE_TOGGLE` again. **There is no VT switch and no libdrm anywhere in this design.**

**Tech Stack:** C99 + `libwayland-client` + `wlr-layer-shell-unstable-v1` (protocol XML vendored from labwc's source, C stubs generated at build time by `$(HOST_DIR)/bin/wayland-scanner`) + raw Linux evdev (`<linux/input.h>`, no libevdev); Python 3 + pyserial + stdlib `socket`/`subprocess` for the daemon; Buildroot `generic-package`; Docker-based Buildroot cross-build.

## Why this is a v2 plan

A first implementation attempt (`docs/superpowers/plans/2026-08-06-phase4-quickmenu.md`, kept on disk, **do not delete**) assumed this hardware has no compositor and planned a VT-switch + raw-libdrm display hand-off. An on-device spike (recorded in `PHASE4-QUICKMENU-FINDINGS.md`, "Task 2") proved that assumption **false**: `labwc` 0.9.3 runs for the whole EmulationStation session (`batocera-resolution.mk:33`, `BATOCERA_SCRIPT_TYPE=wayland-labwc`, unconditional), RetroArch's `/proc/<pid>/environ` carries `WAYLAND_DISPLAY=wayland-0`, and RetroArch holds `/dev/dri/renderD128` but never `/dev/dri/card0`. CLAUDE.md hard rule #6 was corrected accordingly. The design was rewritten around a real Wayland overlay; **this plan implements that rewrite**. Read `docs/superpowers/specs/2026-08-06-quickmenu-design.md` before starting — it is normative and this plan does not contradict it.

## Global Constraints

Copied verbatim from `CLAUDE.md` and the design docs — non-negotiable for every task below:

1. **Never PWM the fan.** It's a 2-wire blower — on/off only, temperature-based. (No task here touches the fan.)
2. **`-j2` max for on-device builds.** 1 GB RAM; `-j3+` OOMs even with zram. **There are no on-device builds in this phase** — host cross-builds in Docker use the project's existing `BR2_JLEVEL=4` from `batocera-build/scripts/env.sh`; do not change it.
3. **Updates stay manual/user-triggered.** Never add auto-update behavior.
4. **WiFi (RTL8723BS) stability fix must carry over**: `rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0` plus WiFi power-save disabled at the network-manager level. This phase must not touch those overlay files.
5. **CLAUDE.md hard rule #6 as corrected 2026-08-06:** a real overlay **is** possible on this Batocera tree via `wlr-layer-shell-unstable-v1`. The old "no overlay exists" wording applied to the historical RetroPie build only. Do not reintroduce VT switching or `drmSetMaster()` anywhere in this phase.
6. **The main project directory `/Users/bas/Circuit-Sword Batocera` is deliberately NOT a git repo.** Never run `git init` or `git commit` there. All git operations happen in `/Users/bas/batocera-build-wifi/batocera.linux`, followed by a patch-regeneration step that writes a plain file into the project directory.
7. **No hardware in CI.** Every task states explicitly what is verified off-device vs. what still needs the physical device. Never claim hardware behavior is confirmed when it isn't.

**Fixed paths used throughout:**

| Thing | Path |
| --- | --- |
| Real build tree (git repo, HEAD detached) | `/Users/bas/batocera-build-wifi/batocera.linux` |
| Patch capture target | `/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch` |
| Patch base SHA (pinned commit, verified ancestor of HEAD) | `155c2d8d304cbb53db52e9479dcf683392821d5c` |
| Expected HEAD at start of this plan | `46854a2723 es_input.cfg: swap Arduino Leonardo A/B button mapping` |
| Phase-3 daemon to extend | `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` |
| New Buildroot package | `<build tree>/package/batocera/utils/circuitsword-quickmenu/` |
| Host-side unit tests (non-git project dir) | `/Users/bas/Circuit-Sword Batocera/tests/` |
| Findings log (APPEND, do not recreate) | `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` |
| Buildroot output root (host-visible) | `/Volumes/BatoceraBuild/output/bcm2837` |
| Vendored protocol source (labwc's own tree) | `/Volumes/BatoceraBuild/output/bcm2837/build/labwc-0.9.3/protocols/wlr-layer-shell-unstable-v1.xml` |
| `xdg-shell.xml` as installed by `wayland-protocols` | `$(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml` |
| `wayland-scanner` host tool | `$(HOST_DIR)/bin/wayland-scanner` |
| Compositor environment used by everything on the device | `WAYLAND_DISPLAY=wayland-0`, `XDG_RUNTIME_DIR=/var/run` |

**Patch regeneration command** (run verbatim, from the build tree, after every commit that touches the tree):

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

**Package build (cross-compile check) command**, from the build tree, with the project's env:

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

---

## Pre-established findings carried into this plan (already verified, do not re-derive)

1. **`labnag` is already installed on the target image** at `/usr/bin/labnag` (confirmed: `/Volumes/BatoceraBuild/output/bcm2837/target/usr/bin/labnag` exists). It is labwc's own reference layer-shell client and accepts `-y overlay`. **This makes the load-bearing overlay spike possible with NO rebuild and NO reflash** — see Task 2.
2. **RetroArch network commands are NOT enabled in this fork.** `grep -rn "network_cmd" --include='*.py' .` over the build tree (excluding `buildroot/`) returns zero hits, so the v1 attempt never committed this. Task 3 adds it.
3. **This board's volume is PipeWire, not ALSA `amixer`.** `batocera-audio getSystemVolume` / `setSystemVolume N` is the one source of truth (the same script ES calls). Writing `amixer sset Master` directly would be a second, divergent source of truth and is explicitly rejected.
4. **`wlr-layer-shell-unstable-v1.xml` is not shipped by `wayland-protocols`.** It lives only inside labwc's fetched source at `protocols/wlr-layer-shell-unstable-v1.xml` and must be vendored into our package (Task 4 includes its full literal contents).
5. **Buildroot symbols** (read from `buildroot/package/wayland/Config.in` and `buildroot/package/wayland-protocols/Config.in`): `BR2_PACKAGE_WAYLAND` and `BR2_PACKAGE_WAYLAND_PROTOCOLS`. `wayland`'s own `.mk` declares `WAYLAND_DEPENDENCIES = host-pkgconf host-wayland ...`, so `host-wayland` (which provides `wayland-scanner`) is guaranteed present whenever `wayland` is.
6. **The daemon must pass the compositor environment explicitly.** `rpi-circuitsword.py` runs from init with no `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR`. `package/batocera/emulationstation/batocera-emulationstation/wayland/labwc/04-labwc.sh` sets `WAYLAND_DISPLAY=wayland-0` and `XDG_RUNTIME_DIR=/var/run`; the daemon reproduces exactly those two variables when launching the menu, and the C program falls back to the same two values if they are unset.

---

### Task 1: Setup — confirm build-tree state and open the v2 findings section

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append; the file already exists with the v1 attempt's Task 1/Task 2 entries — do **not** recreate or truncate it)

**Interfaces:**
- Consumes: nothing.
- Produces: a verified-clean build tree at HEAD `46854a272337983c7b63a486ae3038f2946ff328`, base SHA `155c2d8d304cbb53db52e9479dcf683392821d5c` confirmed as an ancestor; a `### Task 1 (v2)` findings-log section that every later task appends after.

- [ ] **Step 1: Confirm the build tree is clean and at the expected HEAD.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git status --short
git log --oneline -1
git merge-base --is-ancestor 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD && echo "BASE-SHA-OK"
```

Expected: `git status --short` prints only these three pre-existing untracked/submodule lines and nothing else —

```
 m buildroot
?? board/batocera/broadcom/bcm2837/fsoverlay/etc/NetworkManager/
?? board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/
?? package/batocera/utils/xxd/0001-remove-broken-K-R-forward-declarations.patch
```

— then `46854a2723 es_input.cfg: swap Arduino Leonardo A/B button mapping`, then `BASE-SHA-OK`. Those untracked entries are intentional artifacts from earlier phases; no *tracked* file may be modified. If a tracked file is dirty, stop and resolve before continuing.

- [ ] **Step 2: Confirm the Wayland pieces this plan depends on really exist.**

```bash
ls -l /Volumes/BatoceraBuild/output/bcm2837/host/bin/wayland-scanner
ls -l /Volumes/BatoceraBuild/output/bcm2837/build/labwc-0.9.3/protocols/wlr-layer-shell-unstable-v1.xml
ls -l /Volumes/BatoceraBuild/output/bcm2837/staging/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
ls -l /Volumes/BatoceraBuild/output/bcm2837/target/usr/bin/labnag
cd /Users/bas/batocera-build-wifi/batocera.linux
grep -n "^config BR2_PACKAGE_WAYLAND$" buildroot/package/wayland/Config.in
grep -n "^config BR2_PACKAGE_WAYLAND_PROTOCOLS$" buildroot/package/wayland-protocols/Config.in
```

Expected: all four `ls` calls succeed (the `wayland-scanner` host binary, the vendorable protocol XML, the installed `xdg-shell.xml`, and the already-built `labnag` used by Task 2's spike), and both greps print exactly one line each. If `/Volumes/BatoceraBuild` is not mounted, mount it (`batocera-build/scripts/setup-disk-image.sh`'s image) before continuing — Tasks 2 and 11 need host-visible build output.

- [ ] **Step 3: Confirm the tests directory exists.**

```bash
mkdir -p "/Users/bas/Circuit-Sword Batocera/tests"
ls "/Users/bas/Circuit-Sword Batocera/tests"
```

Expected: the directory exists (it may already contain `spike-drm-master.sh`/`spike-drm-master.out` from the abandoned v1 attempt — leave them, they are the record of how the labwc finding was made).

- [ ] **Step 4: Append the v2 findings-log section.**

Append (do not overwrite) to `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md`:

```markdown

## v2 task log (Wayland layer-shell redesign)

The sections above ("### Task 1:", "### Task 2:") belong to the ABANDONED
v1 libdrm/VT-switch attempt and are kept as the record of how the labwc
finding was made. Everything below is the v2 plan
(docs/superpowers/plans/2026-08-06-phase4-quickmenu-v2.md), whose task
numbering restarts at 1 and is marked "(v2)".

### Task 1 (v2): build tree re-confirmed clean at 46854a2723
No tracked file modified. Untracked NetworkManager/modprobe.d/xxd-patch
entries and the `m buildroot` submodule pointer are pre-existing
artifacts from earlier phases, deliberately untouched. Base SHA
155c2d8d304cbb53db52e9479dcf683392821d5c re-verified as an ancestor of
HEAD. Verified present: host wayland-scanner, labwc's
protocols/wlr-layer-shell-unstable-v1.xml, staging
usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml, and
target/usr/bin/labnag (used by the Task 2 spike).
```

- [ ] **Step 5: Verify the append landed and no commit is needed.**

```bash
grep -c "Task 1 (v2)" "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md"
cd /Users/bas/batocera-build-wifi/batocera.linux && git status --short
```

Expected: `1`, then the same three-line pre-existing status from Step 1. **No git commit** — the findings log lives in the non-git project directory (Global Constraint 6).

---

### Task 2: LOAD-BEARING on-device spike — does labwc composite an overlay-layer surface above a running fullscreen RetroArch?

This is the single assumption the whole redesign rests on, and it is spiked **before any code is written**, exactly as the v1 plan spiked (and disproved) its own DRM-master assumption.

**This spike needs NO rebuild and NO reflash.** `labnag` — labwc's own reference layer-shell client — is already installed on the flashed image at `/usr/bin/labnag`, and it takes `-y overlay`. It creates a real `zwlr_layer_surface_v1` on the overlay layer, which is precisely what `circuitsword-quickmenu` will do. (`wlr-randr`, the other Wayland client on the image, is a read-only output-info tool that cannot create a surface at all — it is useless for this question.)

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/tests/spike-layer-shell.sh` (host-side helper, copied to the device)
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 2 (v2) section)

**Interfaces:**
- Consumes: Task 1's verified findings log and flashed Phase-3 device.
- Produces: a recorded yes/no answer to "does an overlay-layer surface draw above a running fullscreen RetroArch on this compositor", plus the observed `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` that a client must use. Tasks 10, 11 and 14 depend on this answer.

- [ ] **Step 1: Write the spike script.**

Create `/Users/bas/Circuit-Sword Batocera/tests/spike-layer-shell.sh`:

```bash
#!/bin/sh
# Phase 4 v2 spike: run ON THE DEVICE, over SSH, WHILE A GAME IS RUNNING.
#
# Question: does labwc composite a wlr-layer-shell OVERLAY surface above a
# running fullscreen RetroArch client? If yes, circuitsword-quickmenu can
# be a plain Wayland client and no display hand-off is needed at all.
#
# Uses /usr/bin/labnag, labwc's own reference layer-shell client, which is
# already on this image -- nothing is built or installed here.
set -u

echo "=== 1. is RetroArch running? (busybox-safe, no pgrep) ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in retroarch*) echo "RUNNING pid=${p#/proc/} comm=$c" ;; esac
done

echo "=== 2. RetroArch's Wayland environment ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in
        retroarch*)
            tr '\0' '\n' < "$p/environ" 2>/dev/null | grep -E '^(WAYLAND_DISPLAY|XDG_RUNTIME_DIR|XDG_SESSION_TYPE)=' ;;
    esac
done

echo "=== 3. which process holds each DRM node ==="
for p in /proc/[0-9]*; do
    for fd in "$p"/fd/*; do
        t=$(readlink "$fd" 2>/dev/null) || continue
        case "$t" in /dev/dri/*) echo "${p#/proc/} $(cat "$p/comm" 2>/dev/null) -> $t" ;; esac
    done
done

echo "=== 4. labnag present? ==="
ls -l /usr/bin/labnag || echo "labnag: NOT PRESENT -- spike cannot run"

echo "=== 5. overlay-layer surface for 15 seconds (WATCH THE SCREEN) ==="
echo "Expect a bar reading 'QUICKMENU OVERLAY SPIKE' on top of the game."
XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 \
    /usr/bin/labnag -y overlay -k none -e top -t 15 \
        -m "QUICKMENU OVERLAY SPIKE" 2>&1
echo "labnag exit rc=$?"

echo "=== 6. game still running afterwards? ==="
for p in /proc/[0-9]*; do
    [ -r "$p/comm" ] || continue
    c=$(cat "$p/comm" 2>/dev/null)
    case "$c" in retroarch*) echo "STILL RUNNING pid=${p#/proc/}" ;; esac
done
echo "=== spike done ==="
```

- [ ] **Step 2: Start a game on the device, then run the spike.**

Launch any RetroArch game on the physical Circuit-Sword and **leave it running and visible**. Then, from the host:

```bash
chmod +x "/Users/bas/Circuit-Sword Batocera/tests/spike-layer-shell.sh"
scp "/Users/bas/Circuit-Sword Batocera/tests/spike-layer-shell.sh" root@batocera.local:/tmp/
ssh root@batocera.local "sh /tmp/spike-layer-shell.sh" 2>&1 | tee "/Users/bas/Circuit-Sword Batocera/tests/spike-layer-shell.out"
```

Expected on the terminal: section 1 lists at least one `RUNNING pid=... comm=retroarch`; section 2 shows `WAYLAND_DISPLAY=wayland-0` and `XDG_RUNTIME_DIR=/var/run`; section 3 shows `labwc -> /dev/dri/card0` and `retroarch -> /dev/dri/renderD128` (and **not** retroarch on card0); section 4 lists the binary; section 5 runs for 15 seconds then exits.

**Expected on the physical screen (this is the actual result being measured):** a bar with the text `QUICKMENU OVERLAY SPIKE` appears **on top of the running game**, the game keeps rendering underneath, and the bar disappears after 15 seconds leaving the game undisturbed.

- [ ] **Step 3: Record the result in the findings log.**

Append to `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md`, with the real observed values:

```markdown
### Task 2 (v2): layer-shell overlay spike (on device, NO rebuild)
Method: /usr/bin/labnag (labwc's own reference layer-shell client, already
on the image) run with `-y overlay -k none` for 15s while a RetroArch game
was running fullscreen. No build, no flash, nothing installed.
- RetroArch running during the spike: yes|no  (pid <n>)
- RetroArch env: WAYLAND_DISPLAY=<value>  XDG_RUNTIME_DIR=<value>
- DRM nodes: labwc -> <node>, retroarch -> <node>
- labnag exit rc: <n>
- OBSERVED ON SCREEN: overlay bar appeared above the running game: YES|NO
- Game kept rendering underneath: yes|no
- Screen returned to the game cleanly after labnag exited: yes|no
- CONCLUSION: labwc DOES / DOES NOT composite an overlay-layer surface
  above a running fullscreen RetroArch client on this hardware.
- Raw output: tests/spike-layer-shell.out
```

If the observed result is **NO** (nothing appeared over the game, or the game was disturbed), **stop and escalate to the human partner before Task 3.** The redesign's core premise would be wrong for a second time and the remaining options (a RetroArch-internal `gfx_widgets` menu, already scoped for Phase 6) are a different, larger piece of work. Do not silently redesign.

- [ ] **Step 4: No commit.** Both files created here live in the non-git project directory (Global Constraint 6). Verify:

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux && git status --short
```

Expected: only the three pre-existing untracked/submodule lines from Task 1 Step 1.

---

### Task 3: Enable RetroArch's network command interface

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py` (insert after the existing `audio_volume` line, before the `# Settings` comment block)

**Interfaces:**
- Consumes: nothing.
- Produces: RetroArch listening for plaintext UDP commands on `127.0.0.1:55355` at every emulator launch. Task 14's `retroarch_cmd_query()` and `send_pause_toggle()` depend on this exact port.

- [ ] **Step 1: Confirm this was never committed during the v1 attempt.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
grep -rn "network_cmd" package/batocera/ | head
```

Expected: **no output.** (The v1 SDD ledger claims Task 3 was never reached; this verifies it directly.) If it *does* print hits showing `network_cmd_enable` already in `libretroRetroarchCustom.py`, skip Steps 2-3 and go straight to Step 4's verification.

- [ ] **Step 2: Add the two settings.**

In `package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py`, find:

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
    # sends PAUSE_TOGGLE over UDP 127.0.0.1:55355 before showing its
    # Wayland overlay (see
    # docs/superpowers/specs/2026-08-06-quickmenu-design.md). RetroArch
    # binds this to localhost only; 55355 is RetroArch's own default port.
    retroarchSettings.save('network_cmd_enable',                '"true"')
    retroarchSettings.save('network_cmd_port',                  '"55355"')

    # Settings
```

- [ ] **Step 3: Verify the file is still valid Python and the settings are present.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
python3 -m py_compile package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py && echo "PY-COMPILE-OK"
grep -n "network_cmd" package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py
```

Expected: `PY-COMPILE-OK`, then two grep lines showing `network_cmd_enable` and `network_cmd_port`.

- [ ] **Step 4: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/core/batocera-configgen/configgen/configgen/generators/libretro/libretroRetroarchCustom.py
git commit -m "configgen: enable RetroArch network commands on localhost:55355

Phase 4's quick menu sends PAUSE_TOGGLE over RetroArch's plaintext UDP
command interface before showing its Wayland overlay. That interface is
disabled by default and was not enabled anywhere in this tree (grep for
network_cmd returned zero hits), so PAUSE_TOGGLE would have gone nowhere.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "network_cmd_enable" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Expected: `1`. **Not verified off-device:** that RetroArch actually honours these settings and answers on 55355 on this build — that is Task 16's on-device check.

---

### Task 4: Buildroot package skeleton for `circuitsword-quickmenu` (vendored protocol XML + wayland-scanner, compiles a stub)

**Files:**
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/Config.in`
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/protocols/wlr-layer-shell-unstable-v1.xml` (vendored, full contents below)
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.h` (stub, replaced in Task 5)
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (stub `main()`, replaced in Task 12)
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_font.c`, `qm_input.c`, `qm_settings.c`, `qm_wl.c` (stubs, replaced in Tasks 7-10)
- Modify: `<build tree>/Config.in:137` (add a `source` line after the two existing circuitsword ones)
- Modify: `<build tree>/package/batocera/core/batocera-system/Config.in:348` (add a `select` after the two existing circuitsword ones)

**Interfaces:**
- Consumes: the `batocera-drminfo` `.mk` shape (this tree's existing small hand-compiled C binary — `generic-package`, empty `_SOURCE`, `$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) ... -o $(@D)/<bin>` in `BUILD_CMDS`).
- Produces: Buildroot symbol `BR2_PACKAGE_CIRCUITSWORD_QUICKMENU`; an installed binary at `/usr/bin/circuitsword-quickmenu`; generated headers `wlr-layer-shell-unstable-v1-client-protocol.h` and `xdg-shell-client-protocol.h` in `$(@D)`, which Task 10's `qm_wl.c` includes by those exact names.

- [ ] **Step 1: Vendor the protocol XML.**

Create the directory and copy the file straight out of labwc's fetched source, then verify it byte-for-byte:

```bash
mkdir -p /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/protocols
cp /Volumes/BatoceraBuild/output/bcm2837/build/labwc-0.9.3/protocols/wlr-layer-shell-unstable-v1.xml \
   /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/protocols/wlr-layer-shell-unstable-v1.xml
diff /Volumes/BatoceraBuild/output/bcm2837/build/labwc-0.9.3/protocols/wlr-layer-shell-unstable-v1.xml \
     /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/protocols/wlr-layer-shell-unstable-v1.xml && echo "XML-IDENTICAL"
```

Expected: `XML-IDENTICAL`.

**Why vendored:** `wlr-layer-shell-unstable-v1` is *not* part of the standard `wayland-protocols` package — it originates from wlroots and is shipped inside labwc's own source tree at `protocols/wlr-layer-shell-unstable-v1.xml` (labwc's `clients/meson.build` references it as `'../protocols/wlr-layer-shell-unstable-v1.xml'`, alongside `wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml'` which *does* come from `wayland-protocols`). Every Wayland client in the wider ecosystem that uses layer-shell vendors this file the same way. Its licence header is an MIT/X11-style permission notice (Copyright © 2017 Drew DeVault) and is preserved verbatim by the copy above — do not strip it.

If `/Volumes/BatoceraBuild` is unavailable, create the file by hand with **exactly** these contents:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<protocol name="wlr_layer_shell_unstable_v1">
  <copyright>
    Copyright © 2017 Drew DeVault

    Permission to use, copy, modify, distribute, and sell this
    software and its documentation for any purpose is hereby granted
    without fee, provided that the above copyright notice appear in
    all copies and that both that copyright notice and this permission
    notice appear in supporting documentation, and that the name of
    the copyright holders not be used in advertising or publicity
    pertaining to distribution of the software without specific,
    written prior permission.  The copyright holders make no
    representations about the suitability of this software for any
    purpose.  It is provided "as is" without express or implied
    warranty.

    THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
    SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
    FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
    SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
    AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
    ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
    THIS SOFTWARE.
  </copyright>

  <interface name="zwlr_layer_shell_v1" version="4">
    <description summary="create surfaces that are layers of the desktop">
      Clients can use this interface to assign the surface_layer role to
      wl_surfaces. Such surfaces are assigned to a "layer" of the output and
      rendered with a defined z-depth respective to each other. They may also be
      anchored to the edges and corners of a screen and specify input handling
      semantics. This interface should be suitable for the implementation of
      many desktop shell components, and a broad number of other applications
      that interact with the desktop.
    </description>

    <request name="get_layer_surface">
      <description summary="create a layer_surface from a surface">
        Create a layer surface for an existing surface. This assigns the role of
        layer_surface, or raises a protocol error if another role is already
        assigned.

        Creating a layer surface from a wl_surface which has a buffer attached
        or committed is a client error, and any attempts by a client to attach
        or manipulate a buffer prior to the first layer_surface.configure call
        must also be treated as errors.

        After creating a layer_surface object and setting it up, the client
        must perform an initial commit without any buffer attached.
        The compositor will reply with a layer_surface.configure event.
        The client must acknowledge it and is then allowed to attach a buffer
        to map the surface.

        You may pass NULL for output to allow the compositor to decide which
        output to use. Generally this will be the one that the user most
        recently interacted with.

        Clients can specify a namespace that defines the purpose of the layer
        surface.
      </description>
      <arg name="id" type="new_id" interface="zwlr_layer_surface_v1"/>
      <arg name="surface" type="object" interface="wl_surface"/>
      <arg name="output" type="object" interface="wl_output" allow-null="true"/>
      <arg name="layer" type="uint" enum="layer" summary="layer to add this surface to"/>
      <arg name="namespace" type="string" summary="namespace for the layer surface"/>
    </request>

    <enum name="error">
      <entry name="role" value="0" summary="wl_surface has another role"/>
      <entry name="invalid_layer" value="1" summary="layer value is invalid"/>
      <entry name="already_constructed" value="2" summary="wl_surface has a buffer attached or committed"/>
    </enum>

    <enum name="layer">
      <description summary="available layers for surfaces">
        These values indicate which layers a surface can be rendered in. They
        are ordered by z depth, bottom-most first. Traditional shell surfaces
        will typically be rendered between the bottom and top layers.
        Fullscreen shell surfaces are typically rendered at the top layer.
        Multiple surfaces can share a single layer, and ordering within a
        single layer is undefined.
      </description>

      <entry name="background" value="0"/>
      <entry name="bottom" value="1"/>
      <entry name="top" value="2"/>
      <entry name="overlay" value="3"/>
    </enum>

    <!-- Version 3 additions -->

    <request name="destroy" type="destructor" since="3">
      <description summary="destroy the layer_shell object">
        This request indicates that the client will not use the layer_shell
        object any more. Objects that have been created through this instance
        are not affected.
      </description>
    </request>
  </interface>

  <interface name="zwlr_layer_surface_v1" version="4">
    <description summary="layer metadata interface">
      An interface that may be implemented by a wl_surface, for surfaces that
      are designed to be rendered as a layer of a stacked desktop-like
      environment.

      Layer surface state (layer, size, anchor, exclusive zone,
      margin, interactivity) is double-buffered, and will be applied at the
      time wl_surface.commit of the corresponding wl_surface is called.

      Attaching a null buffer to a layer surface unmaps it.

      Unmapping a layer_surface means that the surface cannot be shown by the
      compositor until it is explicitly mapped again. The layer_surface
      returns to the state it had right after layer_shell.get_layer_surface.
      The client can re-map the surface by performing a commit without any
      buffer attached, waiting for a configure event and handling it as usual.
    </description>

    <request name="set_size">
      <description summary="sets the size of the surface">
        Sets the size of the surface in surface-local coordinates. The
        compositor will display the surface centered with respect to its
        anchors.

        If you pass 0 for either value, the compositor will assign it and
        inform you of the assignment in the configure event. You must set your
        anchor to opposite edges in the dimensions you omit; not doing so is a
        protocol error. Both values are 0 by default.

        Size is double-buffered, see wl_surface.commit.
      </description>
      <arg name="width" type="uint"/>
      <arg name="height" type="uint"/>
    </request>

    <request name="set_anchor">
      <description summary="configures the anchor point of the surface">
        Requests that the compositor anchor the surface to the specified edges
        and corners. If two orthogonal edges are specified (e.g. 'top' and
        'left'), then the anchor point will be the intersection of the edges
        (e.g. the top left corner of the output); otherwise the anchor point
        will be centered on that edge, or in the center if none is specified.

        Anchor is double-buffered, see wl_surface.commit.
      </description>
      <arg name="anchor" type="uint" enum="anchor"/>
    </request>

    <request name="set_exclusive_zone">
      <description summary="configures the exclusive geometry of this surface">
        Requests that the compositor avoids occluding an area with other
        surfaces. The compositor's use of this information is
        implementation-dependent - do not assume that this region will not
        actually be occluded.

        A positive value is only meaningful if the surface is anchored to one
        edge or an edge and both perpendicular edges. If the surface is not
        anchored, anchored to only two perpendicular edges (a corner), anchored
        to only two parallel edges or anchored to all edges, a positive value
        will be treated the same as zero.

        A positive zone is the distance from the edge in surface-local
        coordinates to consider exclusive.

        Surfaces that do not wish to have an exclusive zone may instead specify
        how they should interact with surfaces that do. If set to zero, the
        surface indicates that it would like to be moved to avoid occluding
        surfaces with a positive exclusive zone. If set to -1, the surface
        indicates that it would not like to be moved to accommodate for other
        surfaces, and the compositor should extend it all the way to the edges
        it is anchored to.

        For example, a panel might set its exclusive zone to 10, so that
        maximized shell surfaces are not shown on top of it. A notification
        might set its exclusive zone to 0, so that it is moved to avoid
        occluding the panel, but shell surfaces are shown underneath it. A
        wallpaper or lock screen might set their exclusive zone to -1, so that
        they stretch below or over the panel.

        The default value is 0.

        Exclusive zone is double-buffered, see wl_surface.commit.
      </description>
      <arg name="zone" type="int"/>
    </request>

    <request name="set_margin">
      <description summary="sets a margin from the anchor point">
        Requests that the surface be placed some distance away from the anchor
        point on the output, in surface-local coordinates. Setting this value
        for edges you are not anchored to has no effect.

        The exclusive zone includes the margin.

        Margin is double-buffered, see wl_surface.commit.
      </description>
      <arg name="top" type="int"/>
      <arg name="right" type="int"/>
      <arg name="bottom" type="int"/>
      <arg name="left" type="int"/>
    </request>

    <enum name="keyboard_interactivity">
      <description summary="types of keyboard interaction possible for a layer shell surface">
        Types of keyboard interaction possible for layer shell surfaces. The
        rationale for this is twofold: (1) some applications are not interested
        in keyboard events and not allowing them to be focused can improve the
        desktop experience; (2) some applications will want to take exclusive
        keyboard focus.
      </description>

      <entry name="none" value="0">
        <description summary="no keyboard focus is possible">
          This value indicates that this surface is not interested in keyboard
          events and the compositor should never assign it the keyboard focus.

          This is the default value, set for newly created layer shell surfaces.

          This is useful for e.g. desktop widgets that display information or
          only have interaction with non-keyboard input devices.
        </description>
      </entry>
      <entry name="exclusive" value="1">
        <description summary="request exclusive keyboard focus">
          Request exclusive keyboard focus if this surface is above the shell surface layer.

          For the top and overlay layers, the seat will always give
          exclusive keyboard focus to the top-most layer which has keyboard
          interactivity set to exclusive. If this layer contains multiple
          surfaces with keyboard interactivity set to exclusive, the compositor
          determines the one receiving keyboard events in an implementation-
          defined manner. In this case, no guarantee is made when this surface
          will receive keyboard focus (if ever).

          For the bottom and background layers, the compositor is allowed to use
          normal focus semantics.

          This setting is mainly intended for applications that need to ensure
          they receive all keyboard events, such as a lock screen or a password
          prompt.
        </description>
      </entry>
      <entry name="on_demand" value="2" since="4">
        <description summary="request regular keyboard focus semantics">
          This requests the compositor to allow this surface to be focused and
          unfocused by the user in an implementation-defined manner. The user
          should be able to unfocus this surface even regardless of the layer
          it is on.

          Typically, the compositor will want to use its normal mechanism to
          manage keyboard focus between layer shell surfaces with this setting
          and regular toplevels on the desktop layer (e.g. click to focus).
          Nevertheless, it is possible for a compositor to require a special
          interaction to focus or unfocus layer shell surfaces (e.g. requiring
          a click even if focus follows the mouse normally, or providing a
          keybinding to switch focus between layers).

          This setting is mainly intended for desktop shell components (e.g.
          panels) that allow keyboard interaction. Using this option can allow
          implementing a desktop shell that can be fully usable without the
          mouse.
        </description>
      </entry>
    </enum>

    <request name="set_keyboard_interactivity">
      <description summary="requests keyboard events">
        Set how keyboard events are delivered to this surface. By default,
        layer shell surfaces do not receive keyboard events; this request can
        be used to change this.

        This setting is inherited by child surfaces set by the get_popup
        request.

        Layer surfaces receive pointer, touch, and tablet events normally. If
        you do not want to receive them, set the input region on your surface
        to an empty region.

        Keyboard interactivity is double-buffered, see wl_surface.commit.
      </description>
      <arg name="keyboard_interactivity" type="uint" enum="keyboard_interactivity"/>
    </request>

    <request name="get_popup">
      <description summary="assign this layer_surface as an xdg_popup parent">
        This assigns an xdg_popup's parent to this layer_surface.  This popup
        should have been created via xdg_surface::get_popup with the parent set
        to NULL, and this request must be invoked before committing the popup's
        initial state.

        See the documentation of xdg_popup for more details about what an
        xdg_popup is and how it is used.
      </description>
      <arg name="popup" type="object" interface="xdg_popup"/>
    </request>

    <request name="ack_configure">
      <description summary="ack a configure event">
        When a configure event is received, if a client commits the
        surface in response to the configure event, then the client
        must make an ack_configure request sometime before the commit
        request, passing along the serial of the configure event.

        If the client receives multiple configure events before it
        can respond to one, it only has to ack the last configure event.

        A client is not required to commit immediately after sending
        an ack_configure request - it may even ack_configure several times
        before its next surface commit.

        A client may send multiple ack_configure requests before committing, but
        only the last request sent before a commit indicates which configure
        event the client really is responding to.
      </description>
      <arg name="serial" type="uint" summary="the serial from the configure event"/>
    </request>

    <request name="destroy" type="destructor">
      <description summary="destroy the layer_surface">
        This request destroys the layer surface.
      </description>
    </request>

    <event name="configure">
      <description summary="suggest a surface change">
        The configure event asks the client to resize its surface.

        Clients should arrange their surface for the new states, and then send
        an ack_configure request with the serial sent in this configure event at
        some point before committing the new surface.

        The client is free to dismiss all but the last configure event it
        received.

        The width and height arguments specify the size of the window in
        surface-local coordinates.

        The size is a hint, in the sense that the client is free to ignore it if
        it doesn't resize, pick a smaller size (to satisfy aspect ratio or
        resize in steps of NxM pixels). If the client picks a smaller size and
        is anchored to two opposite anchors (e.g. 'top' and 'bottom'), the
        surface will be centered on this axis.

        If the width or height arguments are zero, it means the client should
        decide its own window dimension.
      </description>
      <arg name="serial" type="uint"/>
      <arg name="width" type="uint"/>
      <arg name="height" type="uint"/>
    </event>

    <event name="closed">
      <description summary="surface should be closed">
        The closed event is sent by the compositor when the surface will no
        longer be shown. The output may have been destroyed or the user may
        have asked for it to be removed. Further changes to the surface will be
        ignored. The client should destroy the resource after receiving this
        event, and create a new surface if they so choose.
      </description>
    </event>

    <enum name="error">
      <entry name="invalid_surface_state" value="0" summary="provided surface state is invalid"/>
      <entry name="invalid_size" value="1" summary="size is invalid"/>
      <entry name="invalid_anchor" value="2" summary="anchor bitfield is invalid"/>
      <entry name="invalid_keyboard_interactivity" value="3" summary="keyboard interactivity is invalid"/>
    </enum>

    <enum name="anchor" bitfield="true">
      <entry name="top" value="1" summary="the top edge of the anchor rectangle"/>
      <entry name="bottom" value="2" summary="the bottom edge of the anchor rectangle"/>
      <entry name="left" value="4" summary="the left edge of the anchor rectangle"/>
      <entry name="right" value="8" summary="the right edge of the anchor rectangle"/>
    </enum>

    <!-- Version 2 additions -->

    <request name="set_layer" since="2">
      <description summary="change the layer of the surface">
        Change the layer that the surface is rendered on.

        Layer is double-buffered, see wl_surface.commit.
      </description>
      <arg name="layer" type="uint" enum="zwlr_layer_shell_v1.layer" summary="layer to move this surface to"/>
    </request>
  </interface>
</protocol>
```

(The `diff`-verified `cp` in the command block above is the authoritative route; this literal listing exists so the file can be reconstructed without the build volume. If you reconstruct by hand, re-run the `diff` as soon as `/Volumes/BatoceraBuild` is available again.)

- [ ] **Step 2: Write `Config.in`.**

`package/batocera/utils/circuitsword-quickmenu/Config.in`:

```
config BR2_PACKAGE_CIRCUITSWORD_QUICKMENU
	bool "circuitsword-quickmenu"
	depends on BR2_PACKAGE_WAYLAND
	select BR2_PACKAGE_WAYLAND_PROTOCOLS
	help
	  Standalone in-game quick menu (WiFi toggle, volume, brightness)
	  for the Circuit-Sword. Draws a full-screen overlay-layer Wayland
	  surface (wlr-layer-shell-unstable-v1) on top of the running
	  emulator via labwc, and reads the on-board Arduino Leonardo
	  joystick via raw evdev. Launched by the rpi-circuitsword.py
	  daemon on a MODE button press while a game is running.

comment "circuitsword-quickmenu needs wayland"
	depends on !BR2_PACKAGE_WAYLAND
```

- [ ] **Step 3: Write `circuitsword-quickmenu.mk`.**

This uses `generic-package` (a userspace binary — **not** `kernel-module` like `circuitsword-battery`/`circuitsword-backlight`). The build/install shape is copied from `package/batocera/core/batocera-drminfo/batocera-drminfo.mk` (empty `_SOURCE`, direct `$(TARGET_CONFIGURE_OPTS) $(TARGET_CC)` call). The `wayland-scanner` invocations are the literal shell equivalent of labwc's own `clients/meson.build` custom targets (`wayland-scanner private-code <xml> <out.c>` and `wayland-scanner client-header <xml> <out.h>`) — this project does not use meson for its own small C packages.

`package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`:

```
################################################################################
#
# circuitsword-quickmenu
#
################################################################################

CIRCUITSWORD_QUICKMENU_VERSION = 2.0
CIRCUITSWORD_QUICKMENU_SOURCE =
CIRCUITSWORD_QUICKMENU_LICENSE = GPL-2.0+
CIRCUITSWORD_QUICKMENU_DEPENDENCIES = host-wayland wayland wayland-protocols

CIRCUITSWORD_QUICKMENU_SRCDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-quickmenu

CIRCUITSWORD_QUICKMENU_SRCS = \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/quickmenu.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_font.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_input.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_settings.c \
	$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_wl.c

# wlr-layer-shell-unstable-v1 is NOT shipped by the wayland-protocols
# package -- it comes from wlroots/labwc. It is vendored here, copied
# verbatim (licence header included) from labwc's own source tree at
# protocols/wlr-layer-shell-unstable-v1.xml, exactly as labwc's
# clients/meson.build references it.
CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML = $(CIRCUITSWORD_QUICKMENU_SRCDIR)/protocols/wlr-layer-shell-unstable-v1.xml
# xdg-shell DOES come from wayland-protocols, installed into staging.
CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML = $(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

define CIRCUITSWORD_QUICKMENU_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML) \
		$(@D)/xdg-shell-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML) \
		$(@D)/xdg-shell-protocol.c
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_QUICKMENU_SRCDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		$(CIRCUITSWORD_QUICKMENU_SRCS) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/circuitsword-quickmenu \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt
endef

define CIRCUITSWORD_QUICKMENU_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 -D $(@D)/circuitsword-quickmenu \
		$(TARGET_DIR)/usr/bin/circuitsword-quickmenu
endef

$(eval $(generic-package))
```

Notes, so nothing here is mysterious later:
- `host-wayland` provides `$(HOST_DIR)/bin/wayland-scanner`; it is already a dependency of the `wayland` package itself (`WAYLAND_DEPENDENCIES = host-pkgconf host-wayland ...`), but it is listed explicitly so this package does not depend on that transitivity.
- `-lrt` is for `shm_open()`; harmless on glibc ≥ 2.34 where it is a stub library.
- **`wayland-cursor` is deliberately NOT a dependency.** The design doc specifies `keyboard_interactivity = none` and there is no pointer or cursor interaction at all — all input comes from evdev. labwc's `labnag` links `wayland-cursor` only because it draws mouse cursors for clickable buttons, which this menu does not have.

- [ ] **Step 4: Write stub sources so the skeleton builds now.**

`quickmenu.h`:

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

`qm_font.c`, `qm_input.c`, `qm_settings.c`, `qm_wl.c` — each exactly:

```c
#include "quickmenu.h"
```

(An empty translation unit is invalid C99; including the header gives each file a declaration, which is enough. Tasks 7-10 replace all four in full, and Task 12 replaces `quickmenu.c`.)

- [ ] **Step 5: Register the package in the two Config.in files.**

In `<build tree>/Config.in`, after line 137 (`    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-backlight/Config.in"`), add a line with the same 4-space indentation:

```
    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-quickmenu/Config.in"
```

In `<build tree>/package/batocera/core/batocera-system/Config.in`, after line 348 (`	select BR2_PACKAGE_CIRCUITSWORD_BACKLIGHT	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837`), add (tab-indented, matching the two lines above it):

```
	select BR2_PACKAGE_CIRCUITSWORD_QUICKMENU	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837
```

- [ ] **Step 6: Build the package and confirm the skeleton compiles and installs.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -30
```

Expected: no error, ending with a `>>> circuitsword-quickmenu 2.0 Installing to target` line. This is a **cross-compile check only — the binary is never executed here.**

- [ ] **Step 7: Confirm the generated protocol headers really appeared.**

```bash
ls -l /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/*-protocol*.h \
      /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/*-protocol.c
grep -c "zwlr_layer_shell_v1_interface" /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/wlr-layer-shell-unstable-v1-client-protocol.h
```

Expected: four files listed (`wlr-layer-shell-unstable-v1-client-protocol.h`, `xdg-shell-client-protocol.h`, `wlr-layer-shell-unstable-v1-protocol.c`, `xdg-shell-protocol.c`), and the grep count is `>= 1`. Task 10's `qm_wl.c` includes `"wlr-layer-shell-unstable-v1-client-protocol.h"` by exactly that name.

- [ ] **Step 8: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu Config.in package/batocera/core/batocera-system/Config.in
git commit -m "circuitsword-quickmenu: Buildroot generic-package skeleton (Wayland)

Userspace Wayland client, so generic-package (shape copied from
batocera-drminfo), not kernel-module like circuitsword-battery/backlight.

wlr-layer-shell-unstable-v1.xml is vendored from labwc's own source tree
because it is not part of the wayland-protocols package; xdg-shell.xml is
taken from staging. Both are turned into C stubs at build time with the
host wayland-scanner, matching labwc's own clients/meson.build.

Stub sources only; real implementation lands in following commits.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "wlr-layer-shell-unstable-v1.xml" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Expected: the grep count is `>= 1` (the vendored XML is a new file and must be inside the patch, or a fresh `setup-build-tree.sh` run would produce a package that cannot build).

---

### Task 5: Full header — the contract every later task implements against

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.h` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: Task 4's package skeleton.
- Produces: every type and function signature used by Tasks 6-12. The signatures below are **normative** — later tasks must match them character for character.

- [ ] **Step 1: Write the full header.**

Replace `quickmenu.h` with:

```c
#ifndef QUICKMENU_H
#define QUICKMENU_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Framebuffer: 32-bit XRGB8888, which is what the wl_shm buffer       */
/* created in qm_wl.c uses (WL_SHM_FORMAT_XRGB8888). `pixels` may be   */
/* an mmap of shared memory or plain malloc'd memory (host unit tests  */
/* use the latter) -- nothing in the drawing code knows the difference.*/
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
/*                                                                     */
/* Joysticks never route through Wayland -- evdev is the only path     */
/* regardless of compositor -- so this layer is identical to what a    */
/* non-Wayland build would use.                                        */
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
/* qm_wl.c — Wayland overlay-layer surface backed by wl_shm            */
/*                                                                     */
/* Replaces the abandoned libdrm/KMS backend entirely. labwc stays DRM */
/* master the whole time; we are just another client of it, on the     */
/* topmost (overlay) layer, above the running fullscreen RetroArch.    */
/* ------------------------------------------------------------------ */
typedef struct qm_wl qm_wl;

/* Connect, bind wl_compositor/wl_shm/zwlr_layer_shell_v1, create a
 * full-screen overlay-layer surface, and block until the compositor's
 * first configure has been handled and the shm buffer exists.
 * Returns NULL on any failure (no compositor, no layer-shell global,
 * no shm, configure never arrived). */
qm_wl *qm_wl_open(void);

/* Borrowed pointer to the shm-backed framebuffer, valid until
 * qm_wl_close(). Never NULL for a non-NULL qm_wl. */
qm_fb *qm_wl_fb(qm_wl *w);

/* Attach + damage + commit + flush. 0 ok, -1 error. */
int qm_wl_present(qm_wl *w);

/* One iteration of the merged event loop: prepare/flush the Wayland
 * connection, poll BOTH the Wayland fd and `input_fd` for up to
 * timeout_ms, read+dispatch any Wayland events, and report whether
 * `input_fd` has data waiting.
 * Returns  1  input_fd is readable (caller should call qm_input_poll)
 *          0  timeout or Wayland-only activity
 *         -1  connection error, or the compositor closed our surface */
int qm_wl_pump(qm_wl *w, int input_fd, int timeout_ms);

/* Destroy the layer surface and disconnect. Safe on NULL. */
void qm_wl_close(qm_wl *w);

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

Everything above except the `qm_wl.c` block is byte-identical to the abandoned v1 header — the drawing, input and settings layers never depended on the display mechanism. The four `qm_drm_open/fb/present/close` declarations are gone, replaced by the five `qm_wl_*` ones.

- [ ] **Step 2: Verify the skeleton still builds with the full header.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: no errors, `Installing to target` present. (Declarations without definitions are fine — nothing calls them yet.)

- [ ] **Step 3: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.h
git commit -m "circuitsword-quickmenu: define the full internal API header

Drawing/input/settings layers are unchanged from the abandoned libdrm
design -- none of them depended on the display mechanism. The DRM
backend's four declarations are replaced by the qm_wl_* Wayland
overlay-surface API.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 6: Host-side test harness for the pure drawing code (TDD: test first)

The drawing code touches no syscalls and no Wayland, so it compiles and runs on the macOS host with plain `cc`. Written before the implementation. Carried over unchanged from the abandoned v1 plan — it tests code with zero display-mechanism dependency.

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c`
- Create: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`

**Interfaces:**
- Consumes: `quickmenu.h` from Task 5 (`qm_fb`, `qm_fill_rect`, `qm_draw_char`, `qm_draw_text`, `qm_text_width`, `qm_get_pixel`, `QM_GLYPH_*`, `QM_RGB`).
- Produces: `run-c-tests.sh`, re-run by Tasks 7 and 12.

- [ ] **Step 1: Write the test.**

`/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c`:

```c
/* Host-side unit tests for circuitsword-quickmenu's pure drawing code.
 * Compiled with the host cc against the real qm_font.c -- no Wayland,
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
# compiler -- no cross toolchain, no Wayland, no device.
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
- Produces: `qm_fill_rect`, `qm_draw_char`, `qm_draw_text`, `qm_text_width`, `qm_get_pixel` — all consumed by Task 12's `qm_render()`.

- [ ] **Step 1: Write the implementation.**

Replace `qm_font.c` with:

```c
/* Pure drawing into a 32bpp XRGB8888 buffer. No syscalls, no Wayland --
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

Expected: no compile errors, `Installing to target` present. **Cross-compile only; nothing is executed on target here.**

- [ ] **Step 4: Commit and regenerate the patch.**

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

Joysticks never route through Wayland — evdev is the only path regardless of compositor — so this file is **100% unchanged** from the abandoned v1 plan.

Button/hat codes are **not guessed**: they come from this project's own `es_input.cfg` block for `deviceName="Arduino LLC Arduino Leonardo"` (`package/batocera/emulationstation/batocera-emulationstation/controllers/es_input.cfg`), captured on the real hardware — `b` = button code 288 (`BTN_TRIGGER`), `a` = button code 289 (`BTN_THUMB`), and up/right/down/left are hat 0 values 1/2/4/8, i.e. `ABS_HAT0X` / `ABS_HAT0Y`.

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_input.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5) — `enum qm_event`.
- Produces: `qm_input_open()`, `qm_input_close()`, `qm_input_poll()` — consumed by Task 12's `main()`. Note `qm_input_poll(fd, 0)` is a **non-blocking drain**, which is how Task 12's merged Wayland+evdev loop uses it.

- [ ] **Step 1: Write the implementation.**

Replace `qm_input.c` with:

```c
/* Raw evdev reader for the on-board Arduino Leonardo joystick.
 *
 * No libevdev dependency -- <linux/input.h> plus read() is enough for
 * six inputs, matching this project's minimal-dependency preference.
 *
 * Joysticks do NOT route through Wayland: even though the menu draws as
 * a Wayland layer-shell surface with keyboard_interactivity=none, input
 * still comes straight off the evdev node, exactly as it would with any
 * other display mechanism.
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
### Task 8 (v2): qm_input.c written, cross-compile clean
Unchanged from the abandoned v1 plan -- evdev never depended on the
display mechanism. Button/hat codes taken from this repo's own
hardware-captured es_input.cfg (b=288, a=289, hat0 for d-pad), not
guessed. NOT verified: that EVIOCGNAME really returns exactly
"Arduino LLC Arduino Leonardo" on the device, that EVIOCGRAB succeeds
against a paused-but-still-Wayland-focused RetroArch, and that one hat
event per physical tap feels right. All on-device (Task 16).
```

- [ ] **Step 4: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_input.c
git commit -m "circuitsword-quickmenu: raw evdev reader for the Arduino Leonardo

Codes taken from this repo's hardware-captured es_input.cfg entry.
No libevdev dependency; EVIOCGRAB so the paused RetroArch behind the
overlay does not also consume navigation input. Joysticks never route
through Wayland, so this layer is display-mechanism agnostic.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 9: Implement `qm_settings.c` — three settings, three existing primitives

Also **100% unchanged** from the abandoned v1 plan: none of these primitives depend on the display mechanism. Every one reuses the mechanism its existing Batocera counterpart already uses, verbatim, so there is exactly one source of truth per setting:

| Item | Existing primitive | Where that was read from |
| --- | --- | --- |
| WiFi | `batocera-settings-set wifi.enabled 0/1` then `/etc/init.d/S08connman reload` | `package/batocera/core/batocera-scripts/scripts/batocera-wifi` — its `enable`/`disable` cases do exactly this |
| Volume | `batocera-audio getSystemVolume` / `batocera-audio setSystemVolume N` | `package/batocera/core/batocera-audio/alsa/batocera-audio` — PipeWire-backed (`pactl set-sink-volume @DEFAULT_SINK@`), the same script ES calls |
| Brightness | read/write `/sys/class/backlight/circuitsword-backlight/brightness`, scaled by `max_brightness` | `board/batocera/fsoverlay/etc/init.d/S27brightness` → `batocera-brightness`, which computes `NEWVAL = percent * max_brightness / 100` |

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_settings.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5).
- Produces: `qm_wifi_get/set`, `qm_volume_get/set`, `qm_brightness_get/set` — consumed by Task 12's `main()`.

- [ ] **Step 1: Write the implementation.**

Replace `qm_settings.c` with:

```c
/* Each setting is read/written through the *same* primitive the existing
 * Batocera counterpart already uses, so there is one source of truth per
 * setting -- see the table in the Phase 4 v2 plan, Task 9. */
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

- [ ] **Step 2: Verify every referenced helper actually exists in the tree (no invented commands).**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
grep -n "setSystemVolume\|getSystemVolume" package/batocera/core/batocera-audio/alsa/batocera-audio | head -4
grep -n "S08connman reload" package/batocera/core/batocera-scripts/scripts/batocera-wifi
ls board/batocera/fsoverlay/etc/init.d/S08connman
grep -n "max_brightness" package/batocera/core/batocera-scripts/scripts/batocera-brightness | head -3
```

Expected: hits for all four — `getSystemVolume`/`setSystemVolume` in `batocera-audio`, `S08connman reload` lines in `batocera-wifi`, the `S08connman` file listing, and `max_brightness` reads in `batocera-brightness`.

- [ ] **Step 3: Cross-compile check.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -10
```

Expected: clean build, `Installing to target` present.

- [ ] **Step 4: Commit and regenerate the patch.**

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

### Task 10: Implement `qm_wl.c` — the Wayland overlay-layer backend

This is the file that replaces the abandoned design's `qm_drm.c`. Its structure (registry binding, layer-surface configure/ack, `wl_shm` pool/buffer creation, `poll()`-based event loop over `wl_display_get_fd()`) is taken from labwc's own reference layer-shell client `clients/labnag.c` + `clients/pool-buffer.c`, **minus** all of labnag's cairo/pango/glib/xkbcommon/wlroots usage — this program uses nothing but `libwayland-client` and the two generated protocol headers.

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_wl.c` (replace the Task 4 stub entirely)

**Interfaces:**
- Consumes: `quickmenu.h` (Task 5) — `qm_fb`, `qm_wl`; the generated `wlr-layer-shell-unstable-v1-client-protocol.h` from Task 4's `BUILD_CMDS` (found via the `.mk`'s `-I$(@D)`).
- Produces: `qm_wl_open()`, `qm_wl_fb()`, `qm_wl_present()`, `qm_wl_pump()`, `qm_wl_close()` — consumed by Task 11's self-test and Task 12's `main()`. `qm_wl_open()` returning `NULL` is the explicit "could not draw anything" path that the daemon turns into an immediate resume.

- [ ] **Step 1: Write the implementation.**

Replace `qm_wl.c` with:

```c
/* Wayland overlay-layer output for circuitsword-quickmenu.
 *
 * This Batocera build runs labwc (wlroots-based) as EmulationStation's
 * persistent compositor, and RetroArch is a Wayland client of it. labwc
 * keeps /dev/dri/card0 (DRM master) the entire time. So the menu is just
 * another client: a wl_surface promoted to a zwlr_layer_surface_v1 on
 * the OVERLAY layer -- the topmost layer, drawn above even fullscreen
 * clients -- anchored to all four edges and filled from a wl_shm buffer.
 *
 * There is no VT switch, no drmSetMaster(), no display hand-off, and
 * therefore no blank-screen failure mode: if anything here fails we just
 * exit and the user never sees a change.
 *
 * Boilerplate sequence follows labwc's own clients/labnag.c and
 * clients/pool-buffer.c, with all cairo/pango/glib/wlroots use removed:
 * this file links only libwayland-client. */
#include "quickmenu.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* Used only if the compositor sends a 0x0 configure, which it should not
 * do for a surface anchored to all four edges. Matches this board's DPI
 * panel (640x480, see batocera-drminfo output recorded in the findings
 * log). */
#define QM_FALLBACK_W 640
#define QM_FALLBACK_H 480

#define QM_LAYER_NAMESPACE "circuitsword-quickmenu"
#define QM_CONFIGURE_ROUNDTRIPS 20

struct qm_wl {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer *buffer;
    uint32_t width;
    uint32_t height;
    int configured;
    int closed;
    size_t map_size;
    qm_fb fb;
};

/* ---------------- registry ---------------- */

static void qm_registry_global(void *data, struct wl_registry *reg,
                               uint32_t name, const char *iface,
                               uint32_t version)
{
    struct qm_wl *w = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        uint32_t v = (version < 4) ? version : 4;
        w->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, v);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        w->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t v = (version < 4) ? version : 4;
        w->layer_shell = wl_registry_bind(reg, name,
                                          &zwlr_layer_shell_v1_interface, v);
    }
}

static void qm_registry_global_remove(void *data, struct wl_registry *reg,
                                      uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener qm_registry_listener = {
    .global = qm_registry_global,
    .global_remove = qm_registry_global_remove,
};

/* ---------------- layer surface ---------------- */

static void qm_ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t width, uint32_t height)
{
    struct qm_wl *w = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    /* Only the FIRST configure sizes us. The menu is opened, used and
     * closed in a couple of seconds on a fixed-resolution built-in
     * panel; a mid-session resize is not a case worth carrying code for,
     * and ignoring it is strictly safer than reallocating the buffer
     * under the renderer. */
    if (w->configured)
        return;
    w->width = (width == 0) ? QM_FALLBACK_W : width;
    w->height = (height == 0) ? QM_FALLBACK_H : height;
    w->configured = 1;
}

static void qm_ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    ((struct qm_wl *)data)->closed = 1;
}

static const struct zwlr_layer_surface_v1_listener qm_ls_listener = {
    .configure = qm_ls_configure,
    .closed = qm_ls_closed,
};

/* ---------------- shm buffer ---------------- */

static int qm_anon_shm(void)
{
    for (int tries = 0; tries < 100; tries++) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        char name[64];
        snprintf(name, sizeof(name), "/circuitsword-quickmenu-%x-%x",
                 (unsigned int)getpid(), (unsigned int)ts.tv_nsec);
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
        if (errno != EEXIST)
            return -1;
    }
    return -1;
}

static int qm_create_buffer(struct qm_wl *w)
{
    uint32_t stride = w->width * 4;
    size_t size = (size_t)stride * w->height;

    int fd = qm_anon_shm();
    if (fd < 0) {
        fprintf(stderr, "circuitsword-quickmenu: shm_open failed (%s)\n",
                strerror(errno));
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        fprintf(stderr, "circuitsword-quickmenu: ftruncate failed (%s)\n",
                strerror(errno));
        close(fd);
        return -1;
    }
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "circuitsword-quickmenu: mmap failed (%s)\n",
                strerror(errno));
        close(fd);
        return -1;
    }
    memset(map, 0, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(w->shm, fd, (int32_t)size);
    if (pool == NULL) {
        munmap(map, size);
        close(fd);
        return -1;
    }
    w->buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)w->width,
                                          (int32_t)w->height, (int32_t)stride,
                                          WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    if (w->buffer == NULL) {
        munmap(map, size);
        return -1;
    }

    w->map_size = size;
    w->fb.pixels = (uint8_t *)map;
    w->fb.width = w->width;
    w->fb.height = w->height;
    w->fb.pitch = stride;
    return 0;
}

/* ---------------- public API ---------------- */

qm_wl *qm_wl_open(void)
{
    struct qm_wl *w = calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;

    /* rpi-circuitsword.py passes these explicitly, but default to what
     * package/batocera/emulationstation/batocera-emulationstation/wayland/
     * labwc/04-labwc.sh exports, so the binary is also usable by hand
     * over SSH. */
    if (getenv("XDG_RUNTIME_DIR") == NULL)
        setenv("XDG_RUNTIME_DIR", "/var/run", 1);
    const char *disp = getenv("WAYLAND_DISPLAY");
    w->display = wl_display_connect((disp != NULL) ? disp : "wayland-0");
    if (w->display == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: cannot connect to the "
                        "compositor (WAYLAND_DISPLAY=%s)\n",
                (disp != NULL) ? disp : "wayland-0");
        free(w);
        return NULL;
    }

    w->registry = wl_display_get_registry(w->display);
    wl_registry_add_listener(w->registry, &qm_registry_listener, w);
    if (wl_display_roundtrip(w->display) < 0) {
        fprintf(stderr, "circuitsword-quickmenu: registry roundtrip failed\n");
        qm_wl_close(w);
        return NULL;
    }
    if (w->compositor == NULL || w->shm == NULL || w->layer_shell == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: compositor lacks %s%s%s\n",
                (w->compositor == NULL) ? "wl_compositor " : "",
                (w->shm == NULL) ? "wl_shm " : "",
                (w->layer_shell == NULL) ? "zwlr_layer_shell_v1 " : "");
        qm_wl_close(w);
        return NULL;
    }

    w->surface = wl_compositor_create_surface(w->compositor);
    if (w->surface == NULL) {
        qm_wl_close(w);
        return NULL;
    }

    /* Empty input region: the menu takes no pointer or touch input at
     * all (everything comes from evdev), so nothing should be routed to
     * us and stolen from whatever is underneath. */
    struct wl_region *empty = wl_compositor_create_region(w->compositor);
    if (empty != NULL) {
        wl_surface_set_input_region(w->surface, empty);
        wl_region_destroy(empty);
    }

    w->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        w->layer_shell, w->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, QM_LAYER_NAMESPACE);
    if (w->layer_surface == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: get_layer_surface failed\n");
        qm_wl_close(w);
        return NULL;
    }
    zwlr_layer_surface_v1_add_listener(w->layer_surface, &qm_ls_listener, w);
    /* Anchored to all four edges with size 0x0 => the compositor tells us
     * the full output size in the configure event. Exclusive zone -1 means
     * "do not move me to accommodate panels, stretch to the anchored
     * edges". Keyboard interactivity none: Wayland keyboard focus is
     * irrelevant here, all input arrives via evdev. */
    zwlr_layer_surface_v1_set_anchor(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(w->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(w->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    /* Protocol requires an initial commit with NO buffer attached; the
     * compositor answers with configure, and only then may we attach. */
    wl_surface_commit(w->surface);

    for (int i = 0; i < QM_CONFIGURE_ROUNDTRIPS && !w->configured && !w->closed; i++) {
        if (wl_display_roundtrip(w->display) < 0) {
            fprintf(stderr, "circuitsword-quickmenu: roundtrip failed while "
                            "waiting for configure\n");
            qm_wl_close(w);
            return NULL;
        }
    }
    if (!w->configured || w->closed) {
        fprintf(stderr, "circuitsword-quickmenu: no layer-surface configure "
                        "(configured=%d closed=%d)\n", w->configured, w->closed);
        qm_wl_close(w);
        return NULL;
    }

    if (qm_create_buffer(w) != 0) {
        qm_wl_close(w);
        return NULL;
    }

    /* The whole surface is opaque -- tell the compositor so it can skip
     * blending and, on some paths, skip repainting what is underneath. */
    struct wl_region *opaque = wl_compositor_create_region(w->compositor);
    if (opaque != NULL) {
        wl_region_add(opaque, 0, 0, (int32_t)w->width, (int32_t)w->height);
        wl_surface_set_opaque_region(w->surface, opaque);
        wl_region_destroy(opaque);
    }

    return w;
}

qm_fb *qm_wl_fb(qm_wl *w)
{
    return (w == NULL) ? NULL : &w->fb;
}

int qm_wl_present(qm_wl *w)
{
    if (w == NULL || w->buffer == NULL)
        return -1;
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    /* wl_surface_damage (not damage_buffer) so this works at any bound
     * wl_surface version; INT32_MAX x INT32_MAX is the idiomatic
     * "everything" rectangle. */
    wl_surface_damage(w->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(w->surface);
    if (wl_display_flush(w->display) < 0 && errno != EAGAIN) {
        fprintf(stderr, "circuitsword-quickmenu: display flush failed (%s)\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

int qm_wl_pump(qm_wl *w, int input_fd, int timeout_ms)
{
    if (w == NULL || w->closed)
        return -1;

    while (wl_display_prepare_read(w->display) != 0) {
        if (wl_display_dispatch_pending(w->display) < 0)
            return -1;
    }
    errno = 0;
    if (wl_display_flush(w->display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(w->display);
        return -1;
    }

    struct pollfd pfds[2];
    pfds[0].fd = wl_display_get_fd(w->display);
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = input_fd;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;
    int nfds = (input_fd >= 0) ? 2 : 1;

    int pr = poll(pfds, (nfds_t)nfds, timeout_ms);

    if (pr > 0 && (pfds[0].revents & POLLIN)) {
        if (wl_display_read_events(w->display) < 0)
            return -1;
    } else {
        wl_display_cancel_read(w->display);
    }
    if (wl_display_dispatch_pending(w->display) < 0)
        return -1;
    if (w->closed)
        return -1;
    if (pr < 0 && errno != EINTR)
        return -1;
    if (nfds == 2 && (pfds[1].revents & POLLIN))
        return 1;
    return 0;
}

void qm_wl_close(qm_wl *w)
{
    if (w == NULL)
        return;
    if (w->buffer != NULL) {
        wl_buffer_destroy(w->buffer);
        w->buffer = NULL;
    }
    if (w->fb.pixels != NULL) {
        munmap(w->fb.pixels, w->map_size);
        w->fb.pixels = NULL;
    }
    if (w->layer_surface != NULL) {
        zwlr_layer_surface_v1_destroy(w->layer_surface);
        w->layer_surface = NULL;
    }
    if (w->surface != NULL) {
        wl_surface_destroy(w->surface);
        w->surface = NULL;
    }
    if (w->layer_shell != NULL) {
        zwlr_layer_shell_v1_destroy(w->layer_shell);
        w->layer_shell = NULL;
    }
    if (w->shm != NULL) {
        wl_shm_destroy(w->shm);
        w->shm = NULL;
    }
    if (w->compositor != NULL) {
        wl_compositor_destroy(w->compositor);
        w->compositor = NULL;
    }
    if (w->registry != NULL) {
        wl_registry_destroy(w->registry);
        w->registry = NULL;
    }
    if (w->display != NULL) {
        wl_display_flush(w->display);
        wl_display_disconnect(w->display);
        w->display = NULL;
    }
    free(w);
}
```

- [ ] **Step 2: Cross-compile check against Buildroot's `libwayland-client` and the generated headers.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -20
```

Expected: clean build, `Installing to target` present. This is the design doc's "compiles cleanly against Buildroot's `libwayland-client` and the generated protocol headers" criterion. **Nothing is executed — whether labwc actually composites this surface above RetroArch is Task 11's on-device check.**

- [ ] **Step 3: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_wl.c
git commit -m "circuitsword-quickmenu: wlr-layer-shell overlay surface via wl_shm

Replaces the abandoned libdrm/KMS backend. labwc keeps DRM master the
whole time; we are just another Wayland client on the OVERLAY layer,
anchored to all four edges, keyboard_interactivity=none, empty input
region, XRGB8888 wl_shm buffer. Boilerplate follows labwc's own
clients/labnag.c reference client, minus its cairo/pango/glib/wlroots
dependencies -- this links libwayland-client only.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 11: Smallest-possible on-device validation of OUR client (no image rebuild, no reflash)

Task 2 proved **labwc** composites an overlay-layer surface above a running game, using labwc's own client. This task proves **our** `qm_wl.c` does the same thing, using the smallest program that can possibly demonstrate it: create the overlay surface, fill it with a solid colour, hold it for N seconds, exit.

It deliberately does **not** need a full image build or a reflash: the Buildroot package build already produces a cross-compiled aarch64 binary in the host-visible build directory, which is `scp`-able straight to the device. That keeps the load-bearing assumption validated by the smallest thing built, hours before the first full build.

**Files:**
- Create: `<build tree>/package/batocera/utils/circuitsword-quickmenu/qm_wl_selftest.c`
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk` (add a second `$(TARGET_CC)` invocation to `BUILD_CMDS`; **do not** add an install rule — this binary stays out of the image)
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 11 (v2))

**Interfaces:**
- Consumes: `qm_wl_open()`, `qm_wl_fb()`, `qm_wl_present()`, `qm_wl_pump()`, `qm_wl_close()` (Task 10); `qm_fill_rect()` (Task 7).
- Produces: an observed answer to "does our own layer-shell client draw above the running game", plus the `$(@D)/qm-wl-selftest` binary path used by the `scp` below.

- [ ] **Step 1: Write the self-test program.**

Create `package/batocera/utils/circuitsword-quickmenu/qm_wl_selftest.c`:

```c
/* Smallest possible proof that qm_wl.c works on the real device:
 * create the overlay-layer surface, fill it with a solid colour, hold it
 * for a few seconds, exit cleanly.
 *
 * NOT installed into the image -- built alongside the real binary and
 * scp'd to the device by hand (Phase 4 v2 plan, Task 11).
 *
 * Usage: qm-wl-selftest [seconds]     (default 5)
 * Exit:  0 = surface was created and held; non-zero = could not create. */
#include "quickmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
    int seconds = 5;
    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0 && v <= 60)
            seconds = v;
    }

    qm_wl *w = qm_wl_open();
    if (w == NULL) {
        fprintf(stderr, "qm-wl-selftest: qm_wl_open() failed\n");
        return 1;
    }

    qm_fb *fb = qm_wl_fb(w);
    printf("qm-wl-selftest: surface %ux%u pitch=%u, holding %ds\n",
           fb->width, fb->height, fb->pitch, seconds);

    /* Solid magenta: unmistakable against any game frame. */
    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height,
                 QM_RGB(0xC0, 0x00, 0xC0));
    if (qm_wl_present(w) != 0) {
        fprintf(stderr, "qm-wl-selftest: qm_wl_present() failed\n");
        qm_wl_close(w);
        return 2;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) >= seconds)
            break;
        /* input_fd = -1: this test has no evdev device, it only pumps
         * the Wayland connection. */
        if (qm_wl_pump(w, -1, 100) < 0) {
            fprintf(stderr, "qm-wl-selftest: connection lost / surface closed\n");
            qm_wl_close(w);
            return 3;
        }
    }

    qm_wl_close(w);
    printf("qm-wl-selftest: done\n");
    return 0;
}
```

- [ ] **Step 2: Build it alongside the real binary.**

In `circuitsword-quickmenu.mk`, replace the whole `CIRCUITSWORD_QUICKMENU_BUILD_CMDS` block with this version (the four `wayland-scanner` lines and the first `$(TARGET_CC)` call are unchanged; a second `$(TARGET_CC)` call is appended):

```
define CIRCUITSWORD_QUICKMENU_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_QUICKMENU_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML) \
		$(@D)/xdg-shell-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_QUICKMENU_XDG_SHELL_XML) \
		$(@D)/xdg-shell-protocol.c
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_QUICKMENU_SRCDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		$(CIRCUITSWORD_QUICKMENU_SRCS) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/circuitsword-quickmenu \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_QUICKMENU_SRCDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_wl_selftest.c \
		$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_wl.c \
		$(CIRCUITSWORD_QUICKMENU_SRCDIR)/qm_font.c \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/qm-wl-selftest \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt
endef
```

`INSTALL_TARGET_CMDS` is **not** changed — `qm-wl-selftest` is never installed into the image.

- [ ] **Step 3: Build and confirm both binaries exist.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -15
ls -l /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/circuitsword-quickmenu \
      /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/qm-wl-selftest
file /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/qm-wl-selftest
```

Expected: both files exist and `file` reports `ELF 64-bit LSB ... ARM aarch64`. If the build directory is not host-visible (`/Volumes/BatoceraBuild` unmounted, or the project switched to Docker named volumes only), skip Steps 4-5 and record in the findings log that our own client could not be validated before the Task 15 full build — then do the Step 4/5 checks on the flashed image in Task 16 instead. **Do not claim this step passed if it was skipped.**

- [ ] **Step 4: Copy to the device and run it with a game running.**

Launch a game on the physical device and leave it running, then from the host:

```bash
scp /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/qm-wl-selftest root@batocera.local:/tmp/
ssh root@batocera.local "chmod +x /tmp/qm-wl-selftest && XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 /tmp/qm-wl-selftest 8; echo rc=\$?"
```

Expected terminal output: `qm-wl-selftest: surface 640x480 pitch=2560, holding 8s` (the real numbers may differ — record what you see), then `qm-wl-selftest: done` and `rc=0`.

**Expected on the physical screen:** the whole screen turns solid magenta on top of the running game for 8 seconds, then the game reappears exactly as it was, still running.

- [ ] **Step 5: Record the result.**

Append to `PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
### Task 11 (v2): our own layer-shell client validated on device (no reflash)
Cross-built qm-wl-selftest scp'd to /tmp and run with a game running.
- surface size reported by the compositor: <w>x<h>, pitch <n>
- exit rc: <n>
- OBSERVED: full-screen magenta drawn above the running game: YES|NO
- Game resumed/redrew cleanly after the surface was destroyed: yes|no
- Visible flicker on surface creation: yes|no  (describe)
- Visible flicker on surface destruction: yes|no  (describe)
This is the v2 equivalent of the v1 plan's DRM-master spike, but for our
own code rather than a reference client.
```

If the magenta screen does **not** appear even though Task 2's labnag spike passed, the fault is in `qm_wl.c`, not in the architecture — debug `qm_wl.c` (check `qm_wl_open()`'s stderr, which names the missing global) before moving on. Do not proceed to Task 12 with this unresolved.

- [ ] **Step 6: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_wl_selftest.c \
        package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk
git commit -m "circuitsword-quickmenu: add qm-wl-selftest (built, not installed)

Smallest program that proves qm_wl.c really gets an overlay-layer
surface composited above a running game: solid fill, hold N seconds,
exit. Cross-built next to the real binary and scp'd to the device by
hand, so the load-bearing assumption is validated before any full image
build. Deliberately not added to INSTALL_TARGET_CMDS.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 12: Implement `quickmenu.c` — menu model, rendering, merged event loop

`qm_render()` is carried over **verbatim** from the abandoned v1 plan (it only ever touched a `qm_fb`). `main()` is rewritten: instead of "present once per input event" against a KMS CRTC, it drives the merged Wayland+evdev loop through `qm_wl_pump()`. The **exit-code contract is unchanged** (0 = normal close, non-zero = could not run) and the input-handling switch statement is unchanged.

**Files:**
- Modify: `<build tree>/package/batocera/utils/circuitsword-quickmenu/quickmenu.c` (replace the Task 4 stub entirely)
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` (append a `qm_render` smoke test)
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh` (add `quickmenu.c` to the host compile)

**Interfaces:**
- Consumes: `qm_fill_rect`, `qm_draw_text`, `qm_text_width` (Task 7); `qm_input_open/close/poll`, `enum qm_event` (Task 8); `qm_wifi_get/set`, `qm_volume_get/set`, `qm_brightness_get/set` (Task 9); `qm_wl_open/fb/present/pump/close` (Task 10); `qm_state`, `QM_ITEM_*` (Task 5).
- Produces: the `/usr/bin/circuitsword-quickmenu` binary contract Task 14's daemon relies on — **exit code 0 = normal close (B pressed, SIGTERM handled, or the compositor closed our surface), non-zero = could not run at all**; **SIGTERM = close request, must tear the surface down and exit within 5 seconds.**

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

        st.selected = QM_ITEM_BRIGHTNESS;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG,
              "re-render with a different selection still paints bg");

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

`-DQM_NO_MAIN` compiles `quickmenu.c` without its `main()` and without the Linux/Wayland-only parts, so the pure `qm_render()` is testable on macOS.

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
 * Launched by rpi-circuitsword.py after it has paused RetroArch. Draws
 * a full-screen opaque Wayland overlay-layer surface ON TOP of the still
 * running (paused) game -- there is no display hand-off, labwc keeps
 * compositing RetroArch's surface underneath the whole time.
 *
 * Exit contract (relied on by the daemon):
 *   0        normal close: B pressed, SIGTERM handled, or the compositor
 *            closed our surface. The daemon resumes the game.
 *   non-zero could not run at all (no input device, no compositor, no
 *            layer-shell global) -- the daemon resumes immediately, and
 *            nothing was ever drawn, so there is no visible glitch.
 *   SIGTERM  = "close now"; the surface is destroyed and we exit well
 *            inside the daemon's 5s watchdog.
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

/* How long qm_wl_pump() waits per iteration. Short enough that a SIGTERM
 * is acted on well inside the daemon's 5s watchdog. */
#define QM_PUMP_TIMEOUT_MS 100

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
    sa.sa_flags = 0;   /* no SA_RESTART: poll() must return EINTR */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int input_fd = qm_input_open();
    if (input_fd < 0) {
        fprintf(stderr, "circuitsword-quickmenu: no input device, aborting\n");
        return 2;
    }

    qm_wl *wl = qm_wl_open();
    if (wl == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: no overlay surface, aborting\n");
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

    qm_fb *fb = qm_wl_fb(wl);
    qm_render(fb, &st);
    if (qm_wl_present(wl) != 0) {
        qm_wl_close(wl);
        qm_input_close(input_fd);
        return 4;
    }

    while (!qm_quit) {
        int pr = qm_wl_pump(wl, input_fd, QM_PUMP_TIMEOUT_MS);
        if (pr < 0)
            break;              /* compositor closed us or connection lost */
        if (pr == 0)
            continue;           /* timeout, or Wayland-only activity */

        /* Non-blocking drain: qm_wl_pump() already told us the evdev fd
         * is readable, so this never waits. */
        enum qm_event ev = qm_input_poll(input_fd, 0);
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
            qm_wl_present(wl);
        }
    }

    qm_wl_close(wl);
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

- [ ] **Step 6: Optional but cheap — re-run the real binary on the device the same way as Task 11.**

Only if `/Volumes/BatoceraBuild` is host-visible and a game is running:

```bash
scp /Volumes/BatoceraBuild/output/bcm2837/build/circuitsword-quickmenu-2.0/circuitsword-quickmenu root@batocera.local:/tmp/
ssh root@batocera.local "chmod +x /tmp/circuitsword-quickmenu && XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0 /tmp/circuitsword-quickmenu; echo rc=\$?"
```

Expected: the real 3-item menu appears over the game with live values; D-pad/A/B work; B exits with `rc=0`. **This is on-device behaviour — record it in the findings log, do not infer it from the compile succeeding.** (The game will not be paused in this manual run, since the daemon is not driving it; that is expected and fine.)

- [ ] **Step 7: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "circuitsword-quickmenu: menu model, rendering and merged event loop

qm_render() is unchanged from the abandoned libdrm design -- it only ever
touched a qm_fb. main() now drives a merged Wayland+evdev poll loop via
qm_wl_pump() instead of presenting to a KMS CRTC.

Exit contract for the daemon is unchanged: 0 = normal close, non-zero =
could not run, SIGTERM = close now.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 13: Daemon unit tests first — debounce, busy flag, RetroArch detection

Pure-Python logic, fully testable on the host with mocked serial/subprocess/socket. Written before the daemon code. The harness (stub `serial` module, `importlib.util.spec_from_file_location` loading by path because the daemon filename contains a hyphen) is carried over from the abandoned v1 plan; **every VT-related test is deleted** — there is no `vt_current`/`vt_activate` in this design.

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`

**Interfaces:**
- Consumes: the daemon module at `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`, loaded by path.
- Produces: the test suite Task 14 must make pass. Names it asserts on: `ModeButton` (with `.update(raw, now) -> bool` and `.reset()`), `retroarch_running() -> bool`, `MODE_DEBOUNCE_S`, `QUICKMENU_WATCHDOG_S`, `RETROARCH_CMD_PORT`, `QUICKMENU_BIN`, `run_quickmenu_session()`.

- [ ] **Step 1: Write the tests.**

`/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`:

```python
#!/usr/bin/env python3
"""Host-side unit tests for the Phase 4 quick-menu logic inside
rpi-circuitsword.py. No hardware, no serial port, no device: `serial`
is stubbed before the module is loaded, and every syscall the tests
touch is monkeypatched.

There are deliberately NO VT tests: the v2 (Wayland overlay) design has
no vt_current()/vt_activate() at all.

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
    """Import rpi-circuitsword.py by path, with `serial` stubbed out.
    The filename has a hyphen, so a plain `import` cannot reach it."""
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

    def test_no_vt_helpers_remain(self):
        # The Wayland overlay design has no VT switching at all. If these
        # ever come back, the design has silently regressed.
        self.assertFalse(hasattr(cs, "vt_activate"))
        self.assertFalse(hasattr(cs, "vt_current"))


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
    """If RetroArch's command port does not answer we must do nothing at
    all: no PAUSE_TOGGLE, no menu launch. With no VT switch in this
    design, those are the only two things that could go wrong."""

    def test_no_pause_and_no_launch_when_retroarch_does_not_answer(self):
        calls = []

        real_query = cs.retroarch_cmd_query
        real_pause = cs.send_pause_toggle
        real_popen = cs.subprocess.Popen
        self.addCleanup(setattr, cs, "retroarch_cmd_query", real_query)
        self.addCleanup(setattr, cs, "send_pause_toggle", real_pause)
        self.addCleanup(setattr, cs.subprocess, "Popen", real_popen)

        cs.retroarch_cmd_query = lambda cmd, timeout_s=0.5: None
        cs.send_pause_toggle = lambda: calls.append("pause") or True

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

- [ ] **Step 3: No commit.** This file lives in the non-git project directory (Global Constraint 6).

---

### Task 14: Extend `rpi-circuitsword.py` with the quick-menu thread

Structurally the same as the abandoned v1 plan's daemon extension, with **every VT piece deleted**: no `vt_current()`, no `vt_activate()`, no `VT_GETSTATE`/`VT_ACTIVATE`/`VT_WAITACTIVE`, no `CONSOLE_DEV`, no `VT_QUICKMENU`, and no `fcntl`/`struct` imports (those existed only for the VT ioctls — the rest of the file does not use either, verified by the grep in Step 5).

**Files:**
- Modify: `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` — add two imports at the top (currently lines 16-20), insert a new section between `switch_monitor()`'s closing `request.release()` (line 365) and the `# ============================================================` / `# Entry point.` comment block (line 368), and add one entry to the `threads` list in `main()` (line 388).

**Interfaces:**
- Consumes: existing daemon helper `read_mode_button() -> bool` (line 293) and the `stop_event: threading.Event` convention used by `fan_thread`/`battery_bridge`/`backlight_bridge`/`switch_monitor`; `/usr/bin/circuitsword-quickmenu` from Task 12; RetroArch UDP 55355 from Task 3.
- Produces: `ModeButton`, `retroarch_running()`, `retroarch_cmd_query()`, `send_pause_toggle()`, `run_quickmenu_session()`, `quickmenu_thread(stop_event)`, and the constants `MODE_DEBOUNCE_S`, `QUICKMENU_WATCHDOG_S`, `RETROARCH_CMD_PORT`, `QUICKMENU_BIN`, `QUICKMENU_ENV`.

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
import os
import socket
import subprocess
import sys
import time
import threading
import serial  # python3-serial, already a Batocera Buildroot package
```

(Only `socket` and `subprocess` are added. The v1 design also needed `fcntl` and `struct` for VT ioctls; this design has no VT ioctls, so they are not added.)

- [ ] **Step 2: Insert the quick-menu section.**

Insert immediately after `switch_monitor()`'s closing `request.release()` line and before the `# ============================================================` / `# Entry point.` comment block:

```python
# ============================================================
# In-game quick menu (Phase 4).
#
# The MODE button is owned end-to-end here, because this daemon already
# polls CMD_GET_STATUS over serial regardless of what is on screen.
#
# Open : confirm RetroArch is alive -> confirm its network command port
#        answers -> PAUSE_TOGGLE -> launch circuitsword-quickmenu and
#        wait. The menu draws itself as a Wayland overlay-layer surface
#        on top of the paused game; there is NO display hand-off and no
#        VT switch, so there is no blank-screen failure mode.
# Close: quickmenu exits (B, SIGTERM from a second MODE press, crash, or
#        the 5s watchdog kill) -> PAUSE_TOGGLE.
#
# Fail-safe in one direction: we never pause without first confirming we
# can talk to RetroArch, and we always resume afterwards.
# See docs/superpowers/specs/2026-08-06-quickmenu-design.md.
# ============================================================
QUICKMENU_BIN = "/usr/bin/circuitsword-quickmenu"
MODE_DEBOUNCE_S = 0.08          # ~80ms: plain pushbutton, NOT the 800ms
                                # mechanical power switch. Starting
                                # estimate, needs on-device tuning.
QUICKMENU_POLL_INTERVAL_S = 0.05
QUICKMENU_WATCHDOG_S = 5        # same 5s convention as main()'s t.join(timeout=5)

# This daemon is started from init with no session environment, but the
# menu is a Wayland client. These are exactly what
# package/batocera/emulationstation/batocera-emulationstation/wayland/
# labwc/04-labwc.sh exports for the ES session.
QUICKMENU_ENV = {
    "WAYLAND_DISPLAY": "wayland-0",
    "XDG_RUNTIME_DIR": "/var/run",
}

RETROARCH_CMD_HOST = "127.0.0.1"
RETROARCH_CMD_PORT = 55355      # RetroArch default; enabled for this image
                                # in configgen's libretroRetroarchCustom.py
                                # (network_cmd_enable/network_cmd_port).


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


def run_quickmenu_session():
    """One full open/close cycle. Blocks until the menu is closed and the
    game has been resumed. Never returns with the game left paused."""
    # 1. Reachability BEFORE anything visible happens. If RetroArch's
    #    command port does not answer, abort: never pause a game we
    #    cannot prove we can un-pause.
    if retroarch_cmd_query(b"GET_STATUS") is None:
        print("[rpi-circuitsword] quickmenu: RetroArch command port silent, aborting",
              file=sys.stderr)
        return

    if not send_pause_toggle():
        print("[rpi-circuitsword] quickmenu: PAUSE_TOGGLE failed, aborting",
              file=sys.stderr)
        return

    proc = None
    try:
        env = dict(os.environ)
        env.update(QUICKMENU_ENV)
        proc = subprocess.Popen([QUICKMENU_BIN], env=env)
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
            # Watchdog: 5s after a close signal, kill it regardless.
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

    # 2. Resume -- unconditionally, whatever happened above. There is no
    #    display to hand back: labwc composited RetroArch's surface the
    #    entire time, so the game is already visible again the moment the
    #    overlay surface is destroyed.
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

Expected: `OK` with all tests passing, `exit=0` — including `test_no_vt_helpers_remain`.

- [ ] **Step 5: Syntax-check the daemon and confirm no VT leftovers.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
python3 -m py_compile package/batocera/utils/rpigpioswitch/rpi-circuitsword.py && echo "PY-COMPILE-OK"
python3 - <<'EOF'
src = open("package/batocera/utils/rpigpioswitch/rpi-circuitsword.py").read()
for name in ["class ModeButton", "def retroarch_running", "def retroarch_cmd_query",
             "def send_pause_toggle", "def run_quickmenu_session",
             "def quickmenu_thread", 'name="quickmenu"', "QUICKMENU_ENV"]:
    assert name in src, f"MISSING: {name}"
for banned in ["vt_activate", "vt_current", "VT_ACTIVATE", "VT_GETSTATE",
               "VT_WAITACTIVE", "CONSOLE_DEV", "import fcntl", "import struct",
               "drmSetMaster"]:
    assert banned not in src, f"VT/DRM LEFTOVER: {banned}"
print("ALL-SYMBOLS-PRESENT-NO-VT-LEFTOVERS")
EOF
```

Expected: `PY-COMPILE-OK` then `ALL-SYMBOLS-PRESENT-NO-VT-LEFTOVERS`.

- [ ] **Step 6: Commit and regenerate the patch.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: add the Phase 4 quick-menu thread

MODE press -> debounce (80ms) -> RetroArch-running check (/proc scan, no
pgrep on this image) -> GET_STATUS reachability probe -> PAUSE_TOGGLE ->
circuitsword-quickmenu (Wayland overlay, launched with WAYLAND_DISPLAY
and XDG_RUNTIME_DIR since this daemon has no session env) -> PAUSE_TOGGLE.

No VT switching anywhere: labwc composites RetroArch's surface the whole
time, so there is nothing to hand over and nothing to restore. A second
MODE press SIGTERMs the menu; 5s later it is SIGKILLed, matching main()'s
existing 5s thread-join convention.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 15: Full image build and flash

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 15 (v2))

**Interfaces:**
- Consumes: everything from Tasks 3-14, all committed and captured in `batocera-linux.patch`.
- Produces: a flashed device carrying `/usr/bin/circuitsword-quickmenu`, the extended daemon, and the network-command-enabled configgen.

- [ ] **Step 1: Verify the patch is complete before spending hours building.**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git status --short
P="/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
for s in network_cmd_enable circuitsword-quickmenu.mk wlr-layer-shell-unstable-v1.xml \
         qm_font.c qm_wl.c qm_input.c qm_settings.c quickmenu.h qm_wl_selftest.c \
         "def quickmenu_thread" "class ModeButton" QUICKMENU_ENV; do
    printf "%-36s %s\n" "$s" "$(grep -c -- "$s" "$P")"
done
grep -c "vt_activate\|VT_ACTIVATE\|drmSetMaster" "$P"
```

Expected: `git status --short` prints only the three pre-existing untracked/submodule lines (everything of ours is committed); every listed string has a count `>= 1`; and the final grep prints `0` — no VT/DRM leftovers anywhere in the patch. A `0` in the list means a commit was missed; go back and fix it before building.

- [ ] **Step 2: Kick off the full image build.**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
./build-image.sh
```

Expected: `Started, PID <n>` and the log path. This is a multi-hour Docker Buildroot build (shorter with a warm ccache). It runs at the project's existing `BR2_JLEVEL=4` **on the host** — Global Constraint 2's `-j2` cap applies to on-device builds, of which there are none in this phase.

- [ ] **Step 3: Wait for the build and confirm it succeeded.**

```bash
tail -40 "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
grep -c "circuitsword-quickmenu" "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
```

Expected: the log ends with genimage/image creation and no `*** Error`; the grep count is `>= 2` (build + install lines for the new package).

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
ssh root@batocera.local "ls -l /usr/bin/circuitsword-quickmenu; ls /usr/bin/qm-wl-selftest 2>&1; grep -c network_cmd /userdata/system/configs/retroarch/retroarchcustom.cfg; grep -c quickmenu_thread /usr/bin/rpi-circuitsword"
```

Expected: `/usr/bin/circuitsword-quickmenu` exists and is `-rwxr-xr-x`; `qm-wl-selftest` reports `No such file or directory` (it must **not** be installed); the `retroarchcustom.cfg` grep is `2` **after a game has been launched at least once** (configgen rewrites that file at launch, so launch and exit a game first if it prints `0`); the daemon grep is `1`.

- [ ] **Step 6: Record in the findings log.** Append to `PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
### Task 15 (v2): full image built and flashed
Build duration: <fill in>. circuitsword-quickmenu present at
/usr/bin/circuitsword-quickmenu; qm-wl-selftest correctly absent from the
image. network_cmd_enable/port present in the generated
retroarchcustom.cfg after one game launch: yes|no.
```

- [ ] **Step 7: No commit.** Nothing in the build tree changed in this task; verify with `cd /Users/bas/batocera-build-wifi/batocera.linux && git status --short` (expect only the three pre-existing untracked/submodule lines).

---

### Task 16: On-device validation of the whole feature

**Nothing in this task is verifiable off-device.** Every check below requires the physical Circuit-Sword. Do not mark any of them done from log inspection or reasoning alone — each needs an observed result on the real screen.

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` (append Task 16 (v2) with the real observations)
- Modify (possibly): `<build tree>/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` (debounce tuning, Step 8)
- Modify: `docs/superpowers/specs/2026-08-06-quickmenu-design.md` and `docs/superpowers/specs/2026-07-28-batocera-port-design.md` (Step 10)

**Interfaces:**
- Consumes: the flashed device from Task 15.
- Produces: the recorded pass/fail matrix; any fix loops back to the relevant earlier task.

- [ ] **Step 1: `PAUSE_TOGGLE` reliability.** Launch a game. From SSH:

```bash
ssh root@batocera.local "python3 -c \"
import socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(1)
s.connect(('127.0.0.1',55355)); s.send(b'GET_STATUS'); print(s.recv(1024))
s.send(b'PAUSE_TOGGLE'); print('sent')\""
```

Expected: a `GET_STATUS PLAYING ...` reply, and the game visibly freezes. Send `PAUSE_TOGGLE` again; it must visibly resume. Record: does it reliably toggle, and is it ever missed?

- [ ] **Step 2: MODE opens the menu.** With a game running, press MODE on the device. Expected: the game pauses and the 3-item menu appears **on top of it** with WIFI/VOLUME/BRIGHTNESS showing the current real values. Record how long the transition takes and whether any flicker, torn frame, or black flash is visible on surface creation — the design doc explicitly lists "transition timing/any visible flicker on surface creation and destruction" as unverified until now.

- [ ] **Step 3: Navigation and each item.** In the menu: D-pad up/down moves the highlight; left/right adjusts volume and brightness in 5% steps with the bar tracking; A on WIFI toggles it. Verify independently that each change stuck:

```bash
ssh root@batocera.local "batocera-settings-get wifi.enabled; batocera-audio getSystemVolume; cat /sys/class/backlight/circuitsword-backlight/brightness"
```

Expected: values match what the menu displayed. Record whether navigation feel is right (one step per tap, not runaway repeats — that is `qm_input_poll`'s hat-release handling, Task 8).

- [ ] **Step 4: `EVIOCGRAB` against a still-focused RetroArch.** While the menu is open, press D-pad and A/B repeatedly, then close the menu with B and look at the game. Expected: the game did **not** also receive those inputs (no character moved, no menu opened inside the game). This is the design doc's explicit "EVIOCGRAB behavior against a paused RetroArch that's still nominally focused" unknown — RetroArch is still the Wayland-focused client while our overlay is up, so this is the check that matters.

- [ ] **Step 5: Both close paths.** (a) Press B — expect the overlay to disappear, the game to be visible again immediately, and to resume. (b) Reopen, then press MODE again — same result. Record which path is faster/cleaner and whether any flicker is visible on surface destruction.

- [ ] **Step 6: Watchdog fallback (simulated hang and crash).** With the menu open, from SSH:

```bash
ssh root@batocera.local "kill -STOP \$(pidof circuitsword-quickmenu)"
```

Then press MODE on the device to request a close. Expected: the daemon SIGTERMs (ignored, the process is stopped), waits `QUICKMENU_WATCHDOG_S` = 5 seconds, SIGKILLs, and sends `PAUSE_TOGGLE` — the device ends up on the running game. **Note the overlay disappears the instant the process dies, because the compositor drops its surface; unlike the abandoned VT design, there is no state to restore.** Also test the hard-crash variant:

```bash
ssh root@batocera.local "kill -SEGV \$(pidof circuitsword-quickmenu)"
```

Expected: `proc.poll()` returns immediately, the daemon resumes the game with no watchdog wait, and the screen shows the game with no residue of the overlay.

- [ ] **Step 7: MODE from EmulationStation is a no-op, and MODE mid-transition is ignored.** Exit to ES and press MODE — expected: nothing happens on screen, and `ssh root@batocera.local "logread | grep quickmenu | tail -5"` shows `MODE with no game running, ignored`. Then, back in a game, press MODE twice in rapid succession (well under a second) — expected: the menu opens once; the log shows a `busy` line or no second open; the device is not left half-transitioned.

- [ ] **Step 8: Debounce tuning.** If Step 2 or Step 7 shows double-fires or missed presses, adjust `MODE_DEBOUNCE_S` in `rpi-circuitsword.py` (design-doc range: 0.050-0.100). Edit the file **on the device** at `/usr/bin/rpi-circuitsword` for the measurement loop only, then port the final value back into the build tree, re-run Task 13's tests, and commit:

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py 2>&1 | tail -5
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: tune MODE_DEBOUNCE_S to the measured value

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

- [ ] **Step 9: Record the full matrix.** Append to `PHASE4-QUICKMENU-FINDINGS.md`:

```markdown
### Task 16 (v2): on-device validation
| Check | Result | Notes |
| --- | --- | --- |
| PAUSE_TOGGLE reliability | pass/fail | |
| MODE opens overlay above the game, transition time | pass/fail | <ms>, flicker: yes/no |
| Overlay is genuinely on top (game visible edges/none) | pass/fail | |
| WiFi toggle takes effect | pass/fail | |
| Volume adjust takes effect | pass/fail | |
| Brightness adjust takes effect | pass/fail | |
| EVIOCGRAB: game did NOT see menu input | pass/fail | |
| Close via B | pass/fail | flicker on destroy: yes/no |
| Close via second MODE | pass/fail | |
| Watchdog: SIGSTOP'd menu | pass/fail | recovers to running game? |
| Watchdog: SIGSEGV'd menu | pass/fail | |
| MODE in ES is a no-op | pass/fail | |
| MODE mid-transition ignored | pass/fail | |
| Final MODE_DEBOUNCE_S | <value> | measured, not estimated |
```

- [ ] **Step 10: Close out the phase.** Update `docs/superpowers/specs/2026-08-06-quickmenu-design.md` and `docs/superpowers/specs/2026-07-28-batocera-port-design.md` per the project's "update the open gaps section when a gap is resolved" convention. Record: (a) that the load-bearing overlay assumption is now **measured**, not assumed, with the Task 2 and Task 11 results; (b) that RetroArch network commands had to be enabled in configgen; (c) that volume goes through PipeWire/`batocera-audio`, not `amixer`; (d) that `wlr-layer-shell-unstable-v1.xml` had to be vendored from labwc's source because `wayland-protocols` does not ship it; (e) the measured `EVIOCGRAB` behaviour against a focused RetroArch. Mark Phase 4 done in the main design doc's phasing section. **No git commit** — both files are in the non-git project directory (Global Constraint 6).

---

## Self-review

**Spec coverage** — every requirement in `docs/superpowers/specs/2026-08-06-quickmenu-design.md` maps to a task:

| Design-doc requirement | Task |
| --- | --- |
| MODE detected only while RetroArch runs | 14 (`retroarch_running()`), tested in 13, on-device 16.7 |
| Debounce ~50-100ms | 14 (`MODE_DEBOUNCE_S = 0.08`), tested in 13, tuned in 16.8 |
| Busy/in-transition flag | 14 (`busy_lock`/`busy` in `quickmenu_thread`), on-device 16.7 |
| Open: reachability probe → PAUSE_TOGGLE → launch → wait, **no VT step** | 14 (`run_quickmenu_session`), asserted VT-free in 13 (`test_no_vt_helpers_remain`) and 14.5, on-device 16.2 |
| Close: PAUSE_TOGGLE again | 14, on-device 16.5 |
| 5s watchdog matching `main()`'s join timeout | 14 (`QUICKMENU_WATCHDOG_S = 5`), asserted in 13, on-device 16.6 |
| Standalone C program on raw `libwayland-client`, no toolkit | 4 (`.mk` links `-lwayland-client` only; no cairo/pango/glib), 10 |
| Binds `wl_compositor`, `wl_shm`, `zwlr_layer_shell_v1` via `wl_registry` | 10 (`qm_registry_global`) |
| Layer = overlay, anchored to all four edges, `keyboard_interactivity = none` | 10 (`ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY`, all four `..._ANCHOR_*`, `..._KEYBOARD_INTERACTIVITY_NONE`) |
| Full-screen opaque, 3 items + bottom hint line, XRGB8888 `wl_shm` buffer | 10 (`WL_SHM_FORMAT_XRGB8888`, opaque region), 12 (`qm_render`, hint `"A: SELECT   B: BACK   LEFT/RIGHT: ADJUST"`) |
| Rendering carried over unchanged from the abandoned plan's pure drawing layer | 7 (`qm_font.c` verbatim), 12 (`qm_render` verbatim) |
| D-pad/A/B via evdev with `EVIOCGRAB`, no MODE inside the program | 8 (`qm_input.c`, hat0 + codes 288/289, `EVIOCGRAB`) |
| WiFi via `wifi.enabled` + `S08connman reload` | 9 (`qm_wifi_set`) |
| Volume via `batocera-audio get/setSystemVolume` | 9 (`qm_volume_get/set`) |
| Brightness via `/sys/class/backlight/circuitsword-backlight`, `max_brightness`-scaled | 9 (`qm_brightness_get/set`) |
| Exits on B or SIGTERM | 12 (`QM_EV_B` and the `sigaction` handler), 14 (`proc.terminate()`) |
| Vendored `wlr-layer-shell-unstable-v1.xml`; both XMLs through `wayland-scanner`; deps `wayland` + `wayland-protocols` | 4 (Steps 1-3) |
| Error: RetroArch not running → no-op | 14, on-device 16.7 |
| Error: command port unreachable → abort before anything visible | 14 (probe first), tested in 13 (`TestSessionAbortsWhenUnreachable`) |
| Error: cannot connect / layer-surface request fails → exit non-zero, daemon resumes | 10 (`qm_wl_open()` returns NULL with a specific stderr line), 12 (`return 3`), 14 (`rc != 0` logged, resume regardless) |
| Error: crash/hang after the surface is up → 5s watchdog | 14, on-device 16.6 |
| Error: MODE mid-transition ignored | 14, on-device 16.7 |
| Off-device: compiles against Buildroot `libwayland-client` + generated headers | 4.6, 10.2, 12.5 |
| Off-device: host-unit-testable pure rendering | 6, 7.2, 12.4 |
| Off-device: daemon debounce/busy unit tests, Python syntax | 13, 14.4, 14.5 |
| On-device, flagged, never claimed otherwise: overlay actually composites above RetroArch | **2** (labnag, no rebuild) and **11** (our own client, no reflash) |
| On-device, flagged: `EVIOCGRAB` vs. focused RetroArch | 16.4 |
| On-device, flagged: transition timing/flicker on create and destroy | 11.5, 16.2, 16.5 |
| On-device, flagged: debounce tuning | 16.8 |
| Out of scope: WiFi scanning/password, ES/RetroArch existing paths, calibration, low battery | no task touches any of these |

**Placeholder scan:** no "TBD", no "add appropriate error handling", no "similar to Task N". Every code block is complete and self-contained; every command is literal. The only intentional fill-in-later blanks are inside findings-log *templates* (`<fill in>`, `pass/fail`, `<n>`), which the plan states are populated during execution.

**Type/signature consistency:** `quickmenu.h` (Task 5) is the single source of truth.
- `qm_fb` fields `pixels/width/height/pitch` are used identically in `qm_font.c` (Task 7), `qm_wl.c` (Task 10, `w->fb.*`), `quickmenu.c` (Task 12), `qm_wl_selftest.c` (Task 11) and the host tests (Task 6).
- `qm_wl` is an opaque `typedef struct qm_wl qm_wl;` in the header and `struct qm_wl {...}` in `qm_wl.c` — the definitions match, and every consumer only ever holds a `qm_wl *`.
- `qm_wl_open(void) -> qm_wl *` (NULL on failure) is NULL-checked at both call sites (Task 11 Step 1, Task 12 Step 3).
- `qm_wl_fb(qm_wl *) -> qm_fb *`, `qm_wl_present(qm_wl *) -> int`, `qm_wl_close(qm_wl *) -> void` are used with exactly those signatures in Tasks 11 and 12.
- `qm_wl_pump(qm_wl *, int input_fd, int timeout_ms) -> int` is documented as `1 = input readable / 0 = nothing / -1 = error`, and both call sites branch on exactly those three cases; Task 11 passes `input_fd = -1`, which `qm_wl.c` handles explicitly (`nfds = 1`).
- `qm_input_poll(fd, timeout_ms)` is called with `timeout_ms = 0` in Task 12 (non-blocking drain), which Task 8's implementation supports (`poll(&pfd, 1, 0)`).
- `enum qm_event` values produced by `qm_input_poll` are exactly the ones `main()` switches on.
- `QM_GLYPH_W/H/ADVANCE` are used consistently by `qm_draw_char`, `qm_text_width` and the tests.
- Generated-header filename `wlr-layer-shell-unstable-v1-client-protocol.h` is identical in Task 4's `wayland-scanner client-header` output path, Task 4 Step 7's verification, and Task 10's `#include`.
- On the Python side, `ModeButton.update(raw, now) -> bool` / `.reset()`, `retroarch_running() -> bool`, `retroarch_cmd_query(cmd, timeout_s) -> bytes|None`, `send_pause_toggle() -> bool`, `run_quickmenu_session() -> None` are used with matching signatures in Task 13's tests and Task 14's implementation. No `vt_*` helper exists in either — Task 13 asserts their absence.
- The `/usr/bin/circuitsword-quickmenu` path is identical in Task 4's `.mk` install rule, Task 14's `QUICKMENU_BIN`, and Task 13's constant test. Port `55355` is identical in Task 3's configgen setting and Task 14's `RETROARCH_CMD_PORT`. `WAYLAND_DISPLAY=wayland-0` / `XDG_RUNTIME_DIR=/var/run` are identical in Task 2's spike, Task 10's `qm_wl_open()` fallback, Task 11's `ssh` invocation, and Task 14's `QUICKMENU_ENV`.
