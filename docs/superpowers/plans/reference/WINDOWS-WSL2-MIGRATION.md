# Moving the Batocera WiFi build to a Windows/WSL2 machine (7800X3D)

Checklist for continuing this build on a faster Windows machine, without
repeating any of the 18 failures already fixed and documented in
`WIFI-BUILD-FINDINGS.md`.

## Before you start: is this even necessary?

Check the Mac build first — if it finished overnight, you already have a
working `.img` and none of this is needed:
```bash
find "/Volumes/BatoceraBuild/output/bcm2837/images" -iname "*.img*" -newer "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log" 2>/dev/null
```

## What to bring, what to leave behind

| Item | Path on Mac | Bring? | Why |
|---|---|---|---|
| Patched batocera.linux tree | `/Users/bas/batocera-build-wifi/batocera.linux` | **Yes — required** | Contains ALL local fixes: QEMU sched_attr patch, rust-bin 1.96.0 bump + hashes, heimdal `-lcrypt`, `docker.mk` named-volumes support, kernel `.config` (CONFIG_RTL8723BS=m), `config.txt` fragment, `fsoverlay` WiFi stability files. None of this is committed to git — it only exists as local file edits. Skipping this means repeating every failure in WIFI-BUILD-FINDINGS.md. |
| `dl/` (downloaded sources) | `/Volumes/BatoceraBuild/dl` (4.3GB) | Optional, saves time | Architecture-independent (source tarballs) — safe and useful to copy, avoids re-downloading and re-hitting the flaky-mirror issue (failure #11). |
| `output/` (build artifacts) | `/Volumes/BatoceraBuild/output` (70GB) | **No — do not bother** | Contains `host-*` packages (host-gcc, host-rust-bin, host-cargo-c, etc.) compiled for **aarch64** (this Mac's host arch). The `batoceralinux/batocera.linux-build` image is multi-arch (confirmed via `docker manifest inspect`); on Windows/x86_64, Docker pulls the **amd64** variant, so all aarch64-host build artifacts are useless there. Would waste transfer time for nothing. |
| `buildroot-ccache/` | `/Volumes/BatoceraBuild/buildroot-ccache` (2.3GB) | **No** | Same reason as `output/` — ccache entries are keyed to the host compiler, which differs between aarch64 (Mac) and amd64 (Windows). |

**Net effect:** the Windows build starts fresh on host-toolchain/package
compilation (same as this Mac build did originally), but skips re-download
time if you bring `dl/`, and — much more importantly — skips **all 18
already-diagnosed failures** because the patches travel with the
batocera.linux tree itself.

## Transfer

Copy via external drive, network share, or `scp`/`rsync` over LAN:
```bash
# On the Mac, package what to bring (adjust destination as needed):
rsync -av --progress \
  /Users/bas/batocera-build-wifi/batocera.linux/ \
  /Volumes/<external-drive>/batocera.linux/
rsync -av --progress \
  /Volumes/BatoceraBuild/dl/ \
  /Volumes/<external-drive>/dl/
```

## Windows/WSL2 setup

1. Install **WSL2** (a real Linux kernel VM, not the older translation-layer
   WSL1) if not already present: `wsl --install` in an admin PowerShell,
   reboot if prompted.
2. Install **Docker Desktop for Windows**, and in its settings confirm the
   **WSL2 backend** is selected (default in current versions) — this avoids
   the Hyper-V-only backend, which has its own separate quirks.
3. **Critical — file placement.** Copy the transferred `batocera.linux` and
   `dl` directories into the **WSL2 Linux filesystem itself**, not a
   Windows path:
   - Right: `\\wsl$\<distro-name>\home\<user>\batocera-build\...`, or from
     inside a WSL2 terminal: `~/batocera-build/...`
   - Wrong: `/mnt/c/Users/<user>/...` — this crosses the Windows↔WSL2
     filesystem boundary on every file operation (the same category of
     slowdown that VirtioFS/gRPC FUSE caused on macOS — see WIFI-BUILD-FINDINGS.md
     "Performance" section). Building from `/mnt/c/...` would silently
     reintroduce the exact bottleneck this whole migration is meant to avoid.
4. Open a terminal **inside WSL2** (not PowerShell/cmd) for all build
   commands — e.g. `wsl` from PowerShell, or the WSL2 distro's own terminal
   app.

## Running the build

From inside WSL2, in the copied `batocera.linux` directory:
```bash
# Adjust these paths to wherever you copied things inside WSL2:
export OUTPUT_DIR=~/batocera-build/output
export DL_DIR=~/batocera-build/dl
export CCACHE_DIR=~/batocera-build/buildroot-ccache
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17' HOST_CXXFLAGS='-O2' BR2_JLEVEL=$(nproc)"

make BR_DOCKER_VOLUMES=1 MAKE_JLEVEL=$(nproc) \
  O=$OUTPUT_DIR/bcm2837 \
  BR2_EXTERNAL=~/batocera-build/batocera.linux \
  DL_DIR=$DL_DIR \
  BATCH_MODE=1 bcm2837-build
```

Notes:
- `BR2_JLEVEL=$(nproc)` — on native Linux (no macOS virtualization tax,
  and presumably much more RAM than this Mac's 16GB), there's little
  reason to cap parallelism artificially the way we did on the Mac. Watch
  memory usage on the first heavy package (glibc, WebKitGTK) and back off
  only if you see OOM kills (`Error 137` in the log — see failure #18).
- `BR_DOCKER_VOLUMES=1` uses the named-volumes patch already in `docker.mk`
  — on native Linux this matters less (no host↔VM boundary to begin with),
  but it's harmless to keep and saves you from needing to think about it.
  Plain bind-mounts inside WSL2's own filesystem would perform
  equivalently, since there's no cross-boundary translation either way as
  long as you followed step 3 above.
- WebKitGTK's own parallelism is separately capped by available RAM
  (`RAM_GB / 4 + 1`, see WIFI-BUILD-FINDINGS.md) — with more RAM on the
  Windows machine, expect a much higher job count there automatically,
  no manual tuning needed.

## If a new failure appears

Check `WIFI-BUILD-FINDINGS.md` first — some failures (rust-bin version
mismatch, heimdal `-lcrypt`) are legitimate upstream batocera.linux repo
bugs unrelated to macOS and will reproduce identically on any platform;
those are already patched in the tree you copied. Anything host-arch- or
platform-specific (QEMU build container quirks, chmod/EIO/clock-skew) was
tied to the macOS/Docker Desktop VirtioFS boundary specifically and should
not reproduce on native Linux — if something superficially similar shows
up, don't assume it's the same root cause without checking.
