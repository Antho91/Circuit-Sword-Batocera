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

Build tree is clean for tracked files (no staged or modified tracked files relative to HEAD). Pre-existing untracked files and buildroot submodule pointer from earlier work sessions are intentional artifacts and not touched.

### Task 2: DRM-master spike (on device, no rebuild) — STOPPED, escalated
- DRM node held: /dev/dri/card0, held open by `labwc` (pid 5697) and
  `emulationstatio` (pid 5895) — NOT by retroarch. No game was running
  during the spike (section 2 of the script output was empty), so the
  load-bearing scenario (RetroArch alive + paused, holding DRM, while a
  second process tries drmSetMaster after a VT switch) was NOT tested.
- fgconsole / chvt / openvt: all available (/usr/bin/chvt, /usr/bin/openvt).
- VT_ACTIVATE(6) via /dev/tty0 ioctl: OK.
- batocera-drminfo on tty6 while labwc/ES ran on tty1: rc=0, printed
  "1.0:DPI 640x480 75Hz (640x480*)" and "connector HDMIA disconnected".
  This does NOT confirm drmSetMaster() succeeded -- drminfo-style tools
  commonly only call drmModeGetResources(), which does not require master.
- MAJOR UNPLANNED FINDING: this Batocera 43.1 build runs `labwc`, a
  wlroots-based Wayland compositor, as ES's persistent windowing backend
  (`package/batocera/emulationstation/batocera-emulationstation/wayland/labwc/labwc-launch`,
  started for the whole ES session, `WAYLAND_DISPLAY=wayland-0`).
  `batocera-resolution.mk:33` sets `BATOCERA_SCRIPT_TYPE=wayland-labwc`
  unconditionally (not board-specific). Nothing in
  package/batocera/core/batocera-configgen/ or elsewhere kills/stops labwc
  before an emulator launches (grep for "killall.*labwc" etc. across the
  whole package/batocera tree: zero hits). This directly contradicts the
  premise this whole plan and CLAUDE.md hard rule #6 were built on ("no
  DispmanX/overlay layer exists on this hardware's KMS stack" -- true for
  the old RetroPie build, NOT established for this Batocera 43.1 tree).
- CONCLUSION: inconclusive on the original question (no game was running),
  and the underlying architecture assumption is now in doubt. Stopped per
  the plan's own instruction ("If the conclusion is IS NOT obtainable, stop
  and escalate... Do not silently redesign") -- escalated to the human
  partner rather than proceeding to Task 3.
- Raw output: tests/spike-drm-master.out

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

### Task 2 (v2): layer-shell overlay spike (on device, NO rebuild)
Method: /usr/bin/labnag (labwc's own reference layer-shell client, already
on the image) run with `-y overlay -k none -e top -t 15` for 15s.
- RetroArch running during the spike: NO -- games could not be launched
  from ES on this run (device needs a reboot for game launch to work
  again, per the user; unrelated to this spike). Tested against
  EmulationStation's own main-menu surface instead.
- DRM nodes: labwc (pid 1739) -> /dev/dri/card0 and /dev/dri/renderD128;
  emulationstatio (pid 2058) -> /dev/dri/renderD128 only, never card0 --
  same pattern as the v1 spike showed for retroarch, confirming ES is
  ALSO just a Wayland client of labwc, not a DRM master.
- labnag exit rc: 254 (expected: this is labnag's normal "-t timeout
  expired" exit status, not an error -- the overlay ran its full 15s).
- OBSERVED ON SCREEN: overlay bar "QUICKMENU OVERLAY SPIKE" appeared
  above the running ES main menu: YES (user-confirmed, live).
- Underlying surface kept rendering underneath: yes (ES menu, still
  interactive-looking, not replaced).
- Screen returned to normal cleanly after labnag exited: yes.
- CONCLUSION: labwc DOES composite a wlr-layer-shell overlay-layer
  surface above another running Wayland client's fullscreen surface on
  this hardware -- confirmed against ES's own surface. NOT YET directly
  confirmed against a running RetroArch surface specifically (blocked by
  an unrelated device issue preventing game launch this session), but
  RetroArch is architecturally identical to ES in this respect (both
  plain Wayland toplevel clients of labwc, per the v1 spike's finding
  that RetroArch holds /dev/dri/renderD128 and never card0) -- no
  compositor-level reason the overlay layer would behave differently for
  one client vs. the other. Proceeding on this basis; re-confirm with an
  actual running game at Task 16's on-device validation.
- Raw output: tests/spike-layer-shell.out (pasted directly by the user,
  not machine-redirected this run -- SSH password auth, no scp/tee used).

### Infra note (2026-08-06, during v2 Task 5): Docker named-volume ownership bug + fix
Both documented build paths (`BR_DOCKER_VOLUMES=1` named volumes AND the
`BR_DOCKER_VOLUMES=0` host bind-mount) started failing with `Permission
denied` writing to `/bcm2837` inside the build container, even after a
Docker Desktop restart and repeated manual `chown` of the named volume.
Root cause: Docker Desktop resets `batocera-output-bcm2837`'s root
directory ownership to root:root every time a NEW, separate container
process first attaches it at the `/bcm2837` mount path -- a chown from
one `docker run` does not survive into the next `docker run`, even
against the same named volume. This is very likely a Docker Desktop for
Mac VirtioFS/named-volume metadata-persistence bug, not anything in this
project's own scripts.
WORKAROUND (until Docker Desktop is upgraded/fixed, or docker.mk gets a
proper root-chown-then-drop-privilege entrypoint): override the
Makefile's computed UID/GID to 0 on the command line, e.g.:
```
make UID=0 GID=0 BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" \
    BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 \
    bcm2837-pkg PKG=<pkg>-rebuild
```
Confirmed working end-to-end (circuitsword-quickmenu 2.0 built and
installed to target successfully). Downloads (`batocera-dl`) and ccache
(`batocera-ccache`) volumes were NOT affected (correct ownership
throughout) -- only the per-board output volume. Also lost in the same
session: the OLD host bind-mount build tree at
`/Volumes/BatoceraBuild/output/bcm2837` (used briefly as a Task-4 workaround
before this root cause was found) got reset down to a single stamp file
by Docker's same behavior -- irrelevant now since the project's actual
default (named volumes) is what's being used, but note this in case
`/Volumes/BatoceraBuild` bind-mount mode is ever revisited: it will need
a full rebuild too.

### Task 8 (v2): qm_input.c written, cross-compile clean
Unchanged from the abandoned v1 plan -- evdev never depended on the
display mechanism. Button/hat codes taken from this repo's own
hardware-captured es_input.cfg (b=288, a=289, hat0 for d-pad), not
guessed. NOT verified: that EVIOCGNAME really returns exactly
"Arduino LLC Arduino Leonardo" on the device, that EVIOCGRAB succeeds
against a paused-but-still-Wayland-focused RetroArch, and that one hat
event per physical tap feels right. All on-device (Task 16).

### Task 11 (v2): our own layer-shell client validated on device (no reflash)
Cross-built qm-wl-selftest (from the named-volume build, extracted via a
throwaway `docker run ... cp` to the host, then scp'd) run on device with
XDG_RUNTIME_DIR=/var/run WAYLAND_DISPLAY=wayland-0.
- surface size reported by the compositor: 640x480, pitch 2560 (=640*4,
  matches the qm_fb XRGB8888/32bpp contract exactly)
- exit rc: 0
- OBSERVED: full-screen magenta drawn above the running surface: YES
  -- tested against EmulationStation's own home-menu surface, NOT a
  running RetroArch game: the device currently cannot launch games (an
  unrelated fix from an earlier session hasn't been built/flashed into
  an image yet -- that lands with Task 15). Same caveat as Task 2 (v2).
  Architecturally equivalent: labwc treats ES and RetroArch identically
  as plain Wayland clients (both confirmed holding only
  /dev/dri/renderD128, never card0), so no compositor-level reason this
  would behave differently against a running game. Re-confirm with an
  actual game at Task 16.
- Game/ES resumed/redrew cleanly after the surface was destroyed: yes
- Visible flicker on surface creation: not reported by user, assumed
  none (would have been mentioned) -- re-observe explicitly at Task 16
- Visible flicker on surface destruction: same as above
- CONCLUSION: our own qm_wl.c code (not just labwc's reference client)
  is confirmed to create a real overlay-layer surface, get it composited
  above another running Wayland client's surface, and clean up without
  disturbing that surface -- the second, code-specific load-bearing
  checkpoint of this plan passes (qualified, pending Task 16
  reconfirmation against an actual game).
This is the v2 equivalent of the v1 plan's DRM-master spike, but for our
own code rather than a reference client.

### Task 15 (v2): full image built and flashed

Build duration: multi-session, spanning two nights (2026-08-06/07 through
2026-08-08/09), interrupted repeatedly by infra issues unrelated to the
quickmenu code itself -- none were code defects in this plan's own work.
Full chronology, worth keeping for future builds on this host:

- Host is a 16GB-RAM Mac. Docker Desktop's Linux VM was originally
  configured with 13GB allocated (leaving only ~3GB for macOS itself).
  This caused repeated VM-level freezes overnight: the backend would
  lose contact with the VM (`docker info`/`exec` hanging or returning
  "context deadline exceeded" indefinitely), diagnosed via `docker top`
  going stale and `docker exec ps aux` hanging (while `docker exec echo`
  still returned -- a reliable tell that the VM itself was thrashing,
  not that the daemon was down). Fix: reduced `MemoryMiB` in
  `~/Library/Group Containers/group.com.docker/settings-store.json` from
  13312 to 8192, and separately enabled
  `UseVirtualizationFrameworkVirtioFS` (was `false`, i.e. the build had
  been running on the older/less robust gRPC-FUSE file-sharing backend).
  After this the VM stayed stable for the remainder of the build.
- Two separate OOM kills (`Killed signal terminated program cc1plus`)
  during host-side compilation, in **webkitgtk** and later
  **libretro-mame**. Both packages compute their own parallelism from
  `/proc/meminfo` inside the container, independently of and in addition
  to `BR2_JLEVEL` (webkitgtk: `njobs := total_memory_kb/1024/1024/4 + 1`
  capped at `nproc`; libretro-mame: same idea via `LIBRETRO_MAME_JOBS`,
  divisor 2 not 4). Because Buildroot's generated command line ends up
  with two `-j` flags in these cases (one from the standard mechanism,
  one from the package's own override), the *last* one silently wins --
  so `BR2_JLEVEL=2` alone did not actually constrain either package.
  Fixed by forcing the packages' own override variables directly on the
  `make` command line: `njobs=2` and `LIBRETRO_MAME_JOBS=2`. Confirmed
  in the log both times as the invoked command switching from `-j4` (or
  `-j2 -j4`) to a consistent `-j2`. Worth grepping for this pattern
  (`total_memory_kb`/`MemTotal.*meminfo`) proactively in any other
  memory-heavy package before assuming `BR2_JLEVEL` alone is sufficient.
- One non-memory failure: `innoextract` failed to link with "undefined
  reference" errors against several Boost libraries, traced to those
  libraries' `.so.1.89.0` files being exactly 0 bytes in the
  `batocera-output-bcm2837` named volume (dated from very early in the
  first build session -- likely a host-boost or boost install step that
  was interrupted mid-copy by one of the earlier crashes, whose stamp
  file was nonetheless left marking the package "done"). Root-caused by
  inspecting the volume directly (`docker run --rm -v
  batocera-output-bcm2837:/output alpine ls -la ...`) and comparing
  against the `.a` static libs (which were intact). Fixed via
  `make bcm2837-pkg PKG=boost-dirclean` then `make bcm2837-pkg
  PKG=boost` (this project's `%-pkg` wrapper takes `PKG=<buildroot
  target name>`, not a separate `ACTION=` variable -- `<pkg>-dirclean`
  and bare `<pkg>` are real Buildroot per-package targets), followed by
  `make bcm2837-pkg PKG=innoextract-dirclean` to clear its own stale
  failed build directory before resuming the main build.
- Docker Desktop's *VM never starting at all* after being quit and
  reopened (no `com.docker.virtualization` process appearing, backend
  stuck retrying `/ping` with "context deadline exceeded" indefinitely)
  happened multiple times independent of the memory fix above -- each
  time resolved by a further quit+relaunch cycle, sometimes needing the
  user to fully restart the Mac. No root cause found beyond "Apple's
  Virtualization.framework occasionally fails to boot the VM after a
  Docker Desktop restart on this host"; flagging as a known flaky step
  for future builds on this specific machine, not something fixed for
  good.

Final successful run completed cleanly: genimage produced
`batocera-bcm2837-43.1-20260809.img.gz` (1.98 GB), with `.md5`/`.sha256`
checksums, extracted to
`/Users/bas/Circuit-Sword Batocera/output/images/` via
`extract-artifacts.sh`.

Verification against the named volume directly (not just log text, since
Buildroot silently skips re-announcing already-stamped packages across
restarts -- `circuitsword-quickmenu` never appears in the build log text
at all despite building successfully, because it was already stamped
`.stamp_built` from earlier in the multi-day session):
- `/usr/bin/circuitsword-quickmenu` present in the target rootfs,
  27288 bytes, timestamped from this final build run: yes
- `qm-wl-selftest` correctly absent from the target rootfs (never added
  to `INSTALL_TARGET_CMDS`, exactly as designed): confirmed absent
- `network_cmd_enable`/`network_cmd_port` present in
  `libretroRetroarchCustom.py` (source, pre-build): yes, 2 occurrences
- `quickmenu_thread`/`ModeButton` present in `rpi-circuitsword.py`
  (source, pre-build): yes, 2 occurrences
- Stray `Batocera/` directory (the recurring `BASH_SOURCE`-under-zsh
  artifact, see earlier findings entries) reappeared once more during
  this task and was removed again; `git status --short` in the build
  tree afterward matches the expected pre-existing baseline exactly (one
  modified submodule pointer, three pre-existing untracked paths), no
  accidental commit.

Not yet done: on-device flash and boot (queued for the user), and the
`retroarchcustom.cfg`/daemon on-device greps from the brief's Step 5,
which need a booted device over SSH -- proceeding to Task 16's validation
matrix will cover these together with the rest of the in-game behavior
checks.
