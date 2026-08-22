# Circuit-Sword on Batocera

A from-scratch replacement of the [Circuit-Sword](https://circuit-sword.com/)
handheld's stock RetroPie image with a [Batocera](https://batocera.org/)-based
image, targeting the same hardware: a Raspberry Pi **Compute Module 3**
(CM3, BCM2837, aarch64, 1GB RAM) Game Boy mod kit with a DPI display,
RTL8723BS WiFi/Bluetooth, and an on-board Arduino Leonardo for
controls/battery/backlight.

This is **not** a migration or dual-boot of the existing RetroPie build —
it's a ground-up port. See
[`docs/superpowers/specs/2026-07-28-batocera-port-design.md`](docs/superpowers/specs/2026-07-28-batocera-port-design.md)
for full design rationale, phasing, and open gaps.

## Status

- **Phase 0** (base Batocera boot, DPI display, GPIO poweroff, SDIO, UART) — done, see `docs/superpowers/plans/findings/PHASE0-FINDINGS.md`.
- **Phase 2** (WiFi/connectivity — RTL8723BS driver baked into the image) — build succeeded, hardware validation in progress. See `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md`.
- **Phase 3** (hardware daemon: fan/battery/shutdown/backlight, cs-hud) — not started.

## Reproducing the build

The real `batocera.linux` checkout this project builds from is a git clone
of [batocera-linux/batocera.linux](https://github.com/batocera-linux/batocera.linux),
pinned to the `batocera-43.1` tag, with a small set of local changes on top.
Those changes are captured as patches in `batocera-build/`, so this repo
*is* self-contained — nothing needs to be copied by hand:

- `batocera-build/PINNED_COMMITS.txt` — exact upstream commit (outer repo +
  the `buildroot` submodule) the patches were generated against.
- `batocera-build/patches/batocera-linux.patch` — everything except WiFi
  driver enablement and the `buildroot` submodule: DPI/gpio-poweroff/sdio/
  uart0 `config.txt` block, the genimage EIO-over-bind-mount workaround in
  `post-image-script.sh`, Docker named-volumes support in `docker.mk`, a
  `mkimage` PATH fix, dead Kodi mirrors + the `ecwolf` hang disabled, and
  the controller-wizard fix in `es_input.cfg` (removed the stock wildcard
  `deviceGUID="-1"` keyboard default, which silently applied a generic
  mapping to the Arduino Leonardo — since it enumerates as a USB keyboard —
  and skipped the "press a button" config wizard entirely).
- `batocera-build/patches/buildroot.patch` — the WiFi enablement:
  `CONFIG_RTL8723BS=m` added to the kernel defconfig (this uses Linux's own
  **in-tree** `rtl8723bs` driver — no separate out-of-tree WiFi package was
  needed, once the right kernel option was found). Plus a few unrelated
  GCC‑15-compatibility fixes to host packages (`genimage`, `heimdal`,
  `libzlib`, `m4`, `rust-bin`, `squashfs`) that failed to build under this
  toolchain's default warning-as-error settings.
- `batocera-build/patches/0001-remove-broken-K-R-forward-declarations.patch`,
  `0004-linux-user-fix-redefinition-of-struct-sched_attr.patch` — two more
  GCC-15 source patches (for `xxd` and `qemu`) applied to downloaded
  sources at build time, not to repo files directly.
- `batocera-build/overlay/` — two new runtime config files, carried over
  from the RetroPie build's known-good WiFi stability fix:
  `etc/modprobe.d/r8723bs.conf` (`rtw_power_mgnt=0 rtw_ips_mode=0
  rtw_bw_mode=0`) and `etc/NetworkManager/conf.d/circuitsword-wifi-powersave-off.conf`
  (`wifi.powersave = 2`).

The full story behind each of these — what broke, why, and the fix — is
logged in `docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md`.

### One command, from a clean clone

```bash
batocera-build/scripts/build-all.sh   # /all -- output-dir setup + full image build
```

**Corrected 2026-08-22**: `batocera-build/build/batocera.linux` (the
full buildable dev-tree) is tracked directly in this repo now — a
plain `git clone` already gives you everything, no separate build-tree
checkout step. `build-all.sh` just chains output-dir setup + the build
step below, mirroring the original RetroPie build's `build.sh all`
one-shot entry point. Like `build-image.sh`, the final build step still
runs in the background and logs to
`docs/superpowers/plans/findings/wifi-build.log`; the script returns
once it's kicked off rather than blocking for the multi-hour build.
Once it finishes, run `batocera-build/scripts/extract-artifacts.sh` to
copy the built image out to the host (see "Docker named volumes" below
for why that's a separate step).

`setup-disk-image.sh` (macOS-only, case-sensitive disk image) is no longer
part of the default path — see "Docker named volumes" below.
`setup-build-tree.sh` still exists but only as a standalone
verification tool, not part of normal setup — see its own header
comment.

### Building

```bash
batocera-build/scripts/build-image.sh        # /buildimg    -- full image build
batocera-build/scripts/build-kernel.sh       # /buildkernel -- kernel-only rebuild + repackage
batocera-build/scripts/build-wifi.sh         # /buildwifi   -- WiFi is an in-tree kernel driver here, delegates to build-kernel.sh
batocera-build/scripts/rebuild-package.sh    # rebuild one package after editing its source/patches + repackage (see "Build command" below)
batocera-build/scripts/extract-artifacts.sh  # copies the built image out of the Docker named volume onto the host
```

All log to `docs/superpowers/plans/findings/wifi-build.log` and run in the
background (`nohup ... &`) so a closed terminal doesn't kill a multi-hour
build. Shared paths/flags live in `batocera-build/scripts/env.sh` — override
via environment variables (`BATOCERA_SRC`, `BATOCERA_BUILD_ROOT`, etc.)
rather than editing the scripts.

### Docker named volumes (default build mode)

By default (`BR_DOCKER_VOLUMES=1` in `env.sh`), the multi-hundred-GB
Buildroot output/downloads/ccache live inside **Docker named volumes**
(`batocera-output-bcm2837`, `batocera-dl`, `batocera-ccache`) instead of
being bind-mounted from the host. These volumes live entirely inside
Docker Desktop's own Linux VM — measured ~3x faster than the old
host-bind-mount path for this workload (many small files, heavy
chmod/rename traffic — see `WIFI-BUILD-FINDINGS.md` "Performance"
section), and it sidesteps macOS's case-insensitive-APFS problem
entirely (no case-sensitive disk image needed at all — the thing
`setup-disk-image.sh` used to work around).

Trade-offs to know:
- Named volumes aren't Finder-browsable. Use
  `batocera-build/scripts/extract-artifacts.sh` after a build to copy the
  image out, or `docker volume ls` / `docker run --rm -v
  batocera-output-bcm2837:/t <image> ls /t` to inspect directly.
- The cache starts cold the first time — nothing carries over
  automatically from an old host-bind-mount build.
- To fall back to the old host-bind-mount path (e.g. the disk image is
  approaching its size ceiling and you'd rather manage growth by hand,
  or you need Finder access to intermediate build files): set
  `BR_DOCKER_VOLUMES=0` and run `setup-disk-image.sh` (macOS) first.

**Note:** for a targeted rebuild after only editing one or two package's
files (rather than a from-scratch clone), a plain re-run of `build-image.sh`
may silently skip your changes — Buildroot only reinstalls a package whose
stamp file is older than its source; a data/script-only edit to an
already-built package doesn't always bump that automatically. If a rebuilt
image doesn't reflect an edit, delete that package's
`.stamp_target_installed` (and the `_installed`/`_staging_installed`
siblings) under `build/<pkg>-<version>/` — inside the
`batocera-output-$BOARD` named volume in the default mode (`docker run
--rm -v batocera-output-bcm2837:/t <image> rm -f
/t/build/<pkg>-<version>/.stamp_target_installed ...`), or under
`$OUTPUT_DIR/$BOARD/build/<pkg>-<version>/` if using `BR_DOCKER_VOLUMES=0`
— before rebuilding.

## Directory layout

- `batocera-build/` — everything needed to reproduce the build:
  `scripts/` used to run it, and `build/batocera.linux` — the actual
  full buildable dev-tree (upstream batocera.linux + every
  Circuit-Sword commit, buildroot flattened in as plain files),
  **tracked directly in this repo since 2026-08-22**, not git-ignored.
  `patches/`/`overlay/`/`PINNED_COMMITS.txt` are a legacy record of
  what's inside that tree from before the merge, not the source of
  truth going forward.
- `output/` — small stamp/config files only in the default build mode
  (git-ignored, regenerable); the actual multi-hundred-GB build
  output/downloads/ccache live in Docker named volumes instead — see
  "Docker named volumes" above. Only becomes the real multi-hundred-GB
  tree (a mounted case-sensitive disk image on macOS) if you opt out with
  `BR_DOCKER_VOLUMES=0` — see `batocera-build/scripts/setup-disk-image.sh`.
- `docs/superpowers/specs/` — design docs.
- `docs/superpowers/plans/` — phase implementation plans and findings logs
  (the detailed "what broke and how it was fixed" record for each phase).

## Hard hardware rules

These come from real incidents on this exact board, not from RetroPie or
Batocera specifics — they apply regardless of OS:

1. **Never PWM the fan.** It's a 2-wire blower — on/off only, temperature-based.
2. **`-j2` max for on-device builds.** 1GB RAM; `-j3+` OOMs even with zram.
3. **Updates stay manual/user-triggered.** No auto-update behavior.
4. **WiFi (RTL8723BS) stability**: `rtw_power_mgnt=0 rtw_ips_mode=0
   rtw_bw_mode=0` (disables power-save + caps to 20MHz — HT40 causes
   intermittent drops on this chip) plus WiFi power-save disabled at the
   network-manager level. This build uses Linux's in-tree `r8723bs`
   driver (`CONFIG_RTL8723BS=m`), same module name as the RetroPie build,
   so the same option names carry over unchanged — see
   `batocera-build/overlay/`.
5. **An in-game overlay IS possible on this hardware.** An earlier
   assumption here (no DispmanX/overlay layer, so a HUD couldn't draw
   over a running emulator) turned out to be specific to the old
   RetroPie build's bare KMS/DRM stack, not a hardware limitation. This
   Batocera build runs `labwc` (a wlroots Wayland compositor) as
   EmulationStation's windowing backend for the whole session, and ships
   a real in-game overlay (`circuitsword-quickmenu`) built on that —
   see `CLAUDE.md`'s hard rules for the full correction and history.

Full details: [`CLAUDE.md`](CLAUDE.md).

## Building the image

Build system: Buildroot, via Docker (`batoceralinux/batocera.linux-build`,
multi-arch). Built and tested on macOS with Docker Desktop; should also work
on Linux/WSL2 (untested end-to-end — see
`docs/superpowers/plans/reference/WINDOWS-WSL2-MIGRATION.md` for a partial migration
checklist written mid-build).

### Requirements, timing, and the actual build workflow

See **[`BUILDING.md`](BUILDING.md)** for current, verified Prerequisites
(disk space, Docker RAM/CPU allocation) and Timing numbers — a real,
from-scratch cold-cache build was timed end-to-end (~32h on an 8-core
Apple Silicon Mac; see BUILDING.md for the full breakdown and speedup
recommendations), superseding any number that used to live in this
section.

- **First-time bring-up build (this project's actual history):** the
  build ran from 2026-07-29 to 2026-08-04 — about 6 calendar days
  wall-clock. This is **not** a realistic "build time" estimate: it
  includes a full macOS crash and recovery, several disk-image resizes,
  and roughly two dozen distinct package/toolchain failures that each
  needed diagnosis and a source-level fix (dead download mirrors, GCC 15
  strictness changes, a Buildroot RPATH false-positive, a hung git
  submodule prompt, a deterministic I/O error in the final image-write
  step). All of those fixes are now baked into the patch set above, so a
  fresh clone with the patches already applied should not hit them again.

### Build command

See **[`BUILDING.md`](BUILDING.md)** for the actual day-to-day build
workflow (prerequisites, the exact commands, and the non-obvious traps
this project has hit) — that document, not this README, is the source
of truth for how to actually run a build. Short version: the dev-tree
(`batocera-build/build/batocera.linux`) is tracked directly in this
repo now, so a plain `git clone` already gives you everything — no
separate setup step. Then:

- `build-image.sh` / `build-kernel.sh` / `build-wifi.sh` for a full
  build or repackage.
- **`batocera-build/scripts/rebuild-package.sh <package-name>
  [reinstall]`** after editing one package's source/patches — forces a
  clean rebuild of just that package (handling CLAUDE.md Hard Rule #7
  automatically: a full build alone does NOT pick up edited source in
  an already-built package) and repackages the image. Defaults to the
  always-safe `-dirclean`; pass `reinstall` as a second argument only
  for plain-copy/config-file packages where you've confirmed the
  cheaper reinstall is enough. Example:
  `batocera-build/scripts/rebuild-package.sh batocera-emulationstation`.

Full troubleshooting history (every error hit and its fix) is in
`docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md`.

## Flashing and first boot

1. Flash the resulting `batocera-bcm2837-*.img.gz` with Raspberry Pi Imager
   ("Use custom" — do not unzip first, the Imager handles `.gz` directly).
   `dd` on macOS hit permission issues in earlier testing; Raspberry Pi
   Imager was used instead.
2. Boot, then set WiFi **on the live system**. SSH is not reachable until
   the device already has network, so start with whichever of these gets
   you there:

   - **Option A — EmulationStation menu (no network needed, do this
     first):** `Menu (Start) → Network Settings` on the device itself,
     using the controls — enable WiFi, fill in SSID/password. This is
     the only step that works with zero prior network access.
   - **Option B — SSH, once you have network** (either from Option A
     already being done, or after a one-boot `boot.conf` bootstrap — see
     the warning below):
     ```bash
     ssh root@<device-ip>
     batocera-settings-set wifi.enabled 1
     batocera-settings-set wifi.ssid "your-ssid"
     batocera-settings-set wifi.key "your-password"
     reboot
     ```
     (never paste real credentials into chat or commit them).

   Either path writes to `/userdata/system/batocera.conf`, the persistent
   master config — the only place that survives.

   **Do not rely on pre-filling `/boot/batocera-boot.conf` on the SD card**
   (e.g. via a Mac-mounted `/Volumes/BATOCERA/batocera-boot.conf`) as a
   substitute for the step above. Confirmed on real hardware this is a trap:
   `S08connman` *does* fall back to reading `boot.conf` for WiFi setup, but
   only on the very first boot, while `/userdata/system/batocera.conf`
   doesn't exist yet — and it never copies those values into `/userdata`.
   The very first shutdown then runs `S65values4boot`, which syncs
   `boot.conf` *from* the (still-blank) `/userdata` master, silently wiping
   out whatever you'd pre-filled. Net effect: WiFi works for exactly one
   boot, then reverts to disabled on every boot after — looking like it
   "turned itself off". `boot.conf` pre-filling is a one-shot bootstrap at
   best; the values above are the only thing that actually persists.
3. Boot on the Circuit-Sword. Controller buttons (Arduino Leonardo) will
   prompt for manual configuration on first boot — see the "Configure a
   Controller" note in the findings log if it needs to be redone later via
   `Menu → Controllers and Bluetooth → Configure a Controller`.
4. **One-time: enable the hardware daemon.** The fan/battery/backlight/
   shutdown daemon (Phase 3) is built into the image but stays inactive
   until the `CIRCUITSWORD` power-switch profile is selected — this is a
   `system.power.switch` setting, which (like WiFi credentials) lives on the
   `/userdata` partition and is never baked into the image itself. Without
   this step the fan will spin continuously (GPIO 35 floats to its default
   state) and the physical power switch will do nothing. Over SSH:
   ```bash
   ssh root@<device-ip>
   batocera-settings-set system.power.switch CIRCUITSWORD
   /etc/init.d/S92switch restart
   ```
   (or set it via `Menu → System Settings` in EmulationStation, then
   reboot). This survives ordinary use and reboots — it only needs to be
   redone after a full SD-card reflash, never after a
   [safe update](#the-safe-update-procedure), since that only touches
   `/userdata/system/upgrade/`.

## Updating the device

This device runs a custom kernel (WiFi driver, and — as of Phase 3 — a
hardware daemon for fan/battery/backlight/shutdown) baked into the system
image. Batocera's own stock update server has no idea any of that exists —
accepting a stock update replaces the whole system image and silently loses
all of it until our custom image is reapplied.

**The automatic update-check prompt is disabled by default**
(`updates.enabled=0` in the shipped `batocera.conf`) specifically so this
never happens by surprise. If you ever see an update prompt anyway (e.g.
after manually re-enabling the check, or pressing "Update" in the
EmulationStation menu yourself), do not accept it — it always needs the
manual procedure below afterward regardless.

**Game saves are never at risk from this** — `batocera-upgrade` (in any
mode) only ever touches `/userdata/system/upgrade/`, confirmed by reading
its source. The one thing that *does* wipe saves is a full SD-card reflash
(Raspberry Pi Imager writes the whole disk), which is never necessary for
an update — only use that for the very first flash, or true recovery.

### The safe update procedure

1. Rebuild our own image against the newer upstream commit.
   **Corrected 2026-08-22**: since `batocera-build/build/batocera.linux`
   is tracked directly in this repo (not regenerated from
   `batocera-build/patches/` anymore), pulling in a newer upstream
   commit now means merging upstream's changes directly into that
   tracked tree — e.g. `git subtree pull --prefix=batocera-build/build/
   batocera.linux <upstream-remote> <branch>` (resolve any conflicts if
   upstream touched the same lines our commits did), or an equivalent
   manual merge. This workflow hasn't been exercised since the
   2026-08-22 restructuring — expect to work out the exact command the
   first time you do this. Then `batocera-build/scripts/build-image.sh`.
2. Publish the resulting `boot.tar.xz` (found alongside the
   `.img.gz` in the build output) somewhere you can get it back — a GitHub
   Release on this repo works well for this (Releases handle files up to
   2GB; this is purely an archive/download point, **not** wired up as a
   live `updates.url` target — GitHub has no clean way to serve the exact
   nested `<board>/<type>/last/boot.tar.xz` path structure
   `batocera-upgrade`'s automatic-download path expects, so don't try to
   make the ES "Update" button fetch from it directly).
3. Get `boot.tar.xz` onto the device at exactly
   `/userdata/system/upgrade/boot.tar.xz` — easiest via the device's SMB
   network share (drag the file onto `\\<device-ip>\share\system\upgrade\`
   or the macOS/Finder equivalent), no SSH needed for this step.
4. SSH in and run the one command that applies it:
   ```bash
   ssh root@<device-ip>
   batocera-upgrade manual
   ```
   This uses the local file instead of downloading, and — like every other
   `batocera-upgrade` mode — never touches `/userdata` outside the
   `upgrade/` staging folder, so saves are untouched.

## No CI, no hardware in CI

There is no hardware in CI for this project. Builds/lint/compilation can be
verified off-device; DRM/KMS behavior, SDIO WiFi stability, Arduino serial
protocol, GPIO polarities, audio, and first-boot flow all need on-device
validation on the real Circuit-Sword.

## License and attribution

This repository vendors and patches [Batocera Linux](https://batocera.org/)
(built on [Buildroot](https://buildroot.org/)), which is licensed under the
GNU General Public License v2, or (at the licensor's option) any later
version — see `batocera-build/build/batocera.linux/COPYING`. This project
exercises that later-version option: the whole repository, including this
repo's own scripts, patches, and documentation, is distributed under
**GPLv3** — see [`LICENSE`](LICENSE) — matching the original RetroPie-based
[Circuit-Sword](https://github.com/Antho91/Circuit-Sword) project this repo
replaces.

Early in this project, [jecaro/circuix-sword](https://github.com/jecaro/circuix-sword)
(an independent NixOS-based Circuit-Sword project) was kept as read-only
reference material and helped inform the WiFi stability fix and some
DPI/Arduino protocol details — credited here since the reference tree
itself was later removed once this port had absorbed what it needed.
