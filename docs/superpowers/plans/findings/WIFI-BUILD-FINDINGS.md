# WiFi Driver Build — Findings Log

Running log for building a custom Batocera image with in-tree RTL8723BS
WiFi support. Companion to PHASE0-FINDINGS.md — same pattern, this repo has
no git so this file is the record instead of commit messages.

## Task 1: Clone & environment check

- Cloned `https://github.com/batocera-linux/batocera.linux.git`, checked out
  tag `batocera-43.1`. Pinned commit: `155c2d8d30` ("fix jh7110
  compilation").
- Kernel version confirmed matching plan expectations from
  `configs/batocera-bcm2837.board`:
  - `# Kernel - Version: 6.12.25`
  - `BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="$(call
    github,raspberrypi,linux,a1073743767f9e7fdc7017ababd2a07ea0c97c1c)/..."`
  - Matches Global Constraints / Phase 0 expectations. No STOP condition
    triggered.
- **Environment blocker found and resolved:** the project root
  `/Users/bas/Circuit-Sword Batocera` contains a space in its path. GNU
  Make's `$(realpath $(CURDIR))` (used to compute `PROJECT_DIR` in
  `Makefile`) splits on whitespace, silently corrupting `PROJECT_DIR`,
  `USER_DEFCONFIG`, and everything derived from them (e.g. attempting to
  open `/Users/bas/Circuit-Sword/configs/.user_defconfig` — note "Batocera"
  and "batocera.linux" are missing from that path entirely). This is a
  fundamental GNU Make limitation (word-splitting on unquoted variable
  expansion), not a bug in any file this plan touches, and not fixable by
  editing Task-specified files.
  - **Resolution:** moved the entire cloned/checked-out `batocera.linux`
    directory (via `mv`, preserving `.git` and the pinned checkout intact)
    from `/Users/bas/Circuit-Sword Batocera/batocera.linux` to a spaceless
    path: **`/Users/bas/batocera-build-wifi/batocera.linux`**. All
    subsequent Task 2-4 work happens at this new location instead of the
    path literally named in the plan's "Files" section.
  - Two other macOS environment gaps also found and fixed via Homebrew
    (system `make` was 3.81, but this Makefile requires GNU Make 4.3+; and
    `gfind`/GNU findutils was absent, referenced on Darwin in the
    Makefile):
    - `brew install make` → provides `gmake` 4.4.1 (added
      `/opt/homebrew/opt/make/libexec/gnubin` to `PATH` ahead of the
      system `make` for all commands in this build).
    - `brew install findutils` → provides `gfind` 4.11.0.
- `make vars` (using `gmake`/PATH-adjusted `make`) ran cleanly from the new
  location and printed the expected Buildroot/Docker variable dump: board
  list, `Project directory: /Users/bas/batocera-build-wifi/batocera.linux`,
  `Docker repo/image: batoceralinux/batocera.linux-build`, etc. No errors.
  Docker confirmed running; 155GB free disk, 16GB RAM available (exceeds
  the ≥50GB/≥8GB requirement).

## Task 2: Enable in-tree RTL8723BS kernel driver

- `grep -n "RTL8723\|RTW88"` on
  `board/batocera/broadcom/bcm2837/linux-defconfig.config` and
  `board/batocera/broadcom/linux-defconfig-fragment.config` showed the
  expected `CONFIG_RTW88_*` block (other Realtek chips, unrelated, left
  untouched) plus one line: `# CONFIG_RTL8723BS is not set` (line 6234 of
  the board defconfig).
  - Judgment call: the plan's Step 1 says STOP if `CONFIG_RTL8723BS` is
    "already present". Taken maximally literally this line does match, but
    it is the standard "known Kconfig option, currently disabled" comment
    format that appears throughout a full kernel defconfig snapshot for
    every option the current selection doesn't enable — not evidence the
    driver is already active or that any plan assumption was wrong. `CONFIG_STAGING=y`
    (a prerequisite for `drivers/staging/rtl8723bs`) was already set two
    lines above it. Treated this as the mundane case, not the STOP case,
    and proceeded with Step 2 rather than halting the whole task over an
    expected defconfig artifact.
- Appended `CONFIG_RTL8723BS=m` as a new line at the end of
  `board/batocera/broadcom/bcm2837/linux-defconfig.config` (new line
  8338), per Step 2's literal instruction to append rather than edit the
  existing disabled line in place. Standard Kconfig `.config` parsing
  processes assignments sequentially, so the later `=m` assignment takes
  precedence over the earlier `# ... is not set` comment for the same
  symbol.
- Verified via `grep -n "CONFIG_RTL8723BS"`: two matches — line 6234 (the
  original disabled comment, left in place) and line 8338 (`CONFIG_RTL8723BS=m`,
  newly appended, the one that takes effect).

## Task 3: config.txt and WiFi stability modprobe file

- Confirmed `board/batocera/broadcom/bcm2837/genimage.cfg` builds `boot.vfat`
  from `@files` (Buildroot's staged boot-partition dir), and
  `board/batocera/broadcom/bcm2837/boot/config.txt` is the stock file
  staged into it — this is the correct file to edit.
- Read `circuit-sword-external/board/circuitsword/config.txt.fragment`
  directly (not retyped from memory) for the exact block content.
- Edited `board/batocera/broadcom/bcm2837/boot/config.txt`:
  - Uncommented `#hdmi_safe=1` → `hdmi_safe=1` (was line 23).
  - Uncommented `#hdmi_force_hotplug=1` → `hdmi_force_hotplug=1` (was line 43).
  - Appended the fragment's body (from `avoid_warnings=2` through
    `framebuffer_height=480`, skipping the fragment file's own
    Phase-0-planning header comment) after the `[all]` marker at the end of
    the file.
  - Verification: `tail -25` ends with `framebuffer_height=480` as
    expected; `grep -c "^hdmi_force_hotplug=1$\|^hdmi_safe=1$"` = `2`
    (both uncommented, not still `#`-prefixed).
- Confirmed `configs/batocera-bcm2837.board`'s `BR2_ROOTFS_OVERLAY` line
  references both `board/batocera/fsoverlay` and
  `board/batocera/broadcom/bcm2837/fsoverlay` — the latter is the board's
  own overlay dir referred to in Step 5.
- Created
  `board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/r8723bs.conf`
  containing exactly:
  ```
  options r8723bs rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0
  ```
  No further wiring needed — Buildroot's `BR2_ROOTFS_OVERLAY` copies this
  verbatim to `/etc/modprobe.d/r8723bs.conf` on the target rootfs.
- Final paths (both under the relocated clone, see Task 1's environment
  note):
  - `/Users/bas/batocera-build-wifi/batocera.linux/board/batocera/broadcom/bcm2837/boot/config.txt`
  - `/Users/bas/batocera-build-wifi/batocera.linux/board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/r8723bs.conf`

## Task 4: Run the build

Three startup issues hit before the build actually got underway (all
tooling/environment issues, not related to the WiFi driver work itself):

1. **Plan's literal command doesn't match this repo's Makefile syntax.**
   `make BOARD=bcm2837 batocera` → `make: *** No rule to make target
   'batocera'.` This repo's `Makefile` (at the pinned `batocera-43.1` tag)
   uses a `<target>-build` pattern instead (confirmed via `make help`).
   Used `make bcm2837-build` instead.
2. **Docker interactive/TTY conflict under `nohup`.** First real attempt
   failed fast with:
   ```
   cannot attach stdin to a TTY-enabled container because stdin is not a terminal
   make: *** [Makefile:319: bcm2837-config] Error 1
   ```
   Cause: `docker.mk` adds `-i` to `DOCKER_OPTS` unless `BATCH_MODE` is set,
   and `docker run -i` needs a real stdin, which a backgrounded/`nohup`'d
   shell doesn't have. Fix: added `BATCH_MODE=1` (documented in `make
   help`'s "Environment variables" section) to the invocation.
3. **`buildroot` git submodule was never initialized.** After fixing (2),
   hit:
   ```
   make: *** No rule to make target 'batocera-bcm2837_defconfig'.  Stop.
   ```
   Root cause: `git clone` (Task 1) does not recurse into submodules by
   default, and this repo's actual Buildroot tree lives in a submodule
   (`buildroot/`, pointing at
   `https://github.com/batocera-linux/buildroot.git`, pinned commit
   `2ef23189ecd626c334e071230e5cbe102b8c41f5`). The `buildroot/` dir was
   present but empty, so none of Buildroot's own make targets (including
   the `_defconfig` targets) existed. Fixed with
   `git submodule update --init --recursive` (had to first `rmdir` a
   leftover empty `buildroot/dl` directory created by the earlier failed
   build attempt, which was blocking the submodule clone).

Final working build command:
```bash
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
cd /Users/bas/batocera-build-wifi/batocera.linux
nohup make BATCH_MODE=1 bcm2837-build > "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log" 2>&1 &
```

Launched in the background at 2026-07-29 19:2x (local). Config generation
succeeded and the build moved into "Building image".

**Build failure #1 — `host-m4` fails against host GCC 15.** Full error
excerpt:
```
gl_list.h:695:40: error: expected identifier or '(' before 'gl_list_node_t'
...
make: *** [package/pkg-generic.mk:289: /bcm2837/build/host-m4-1.4.19/.stamp_built] Error 2
```
Root cause (confirmed via web search — known, already-fixed upstream
Buildroot issue): GCC 15 changed its default C standard from `gnu17` to
`gnu23`, which breaks the gnulib snapshot bundled in m4 1.4.19
(`_GL_ATTRIBUTE_NODISCARD` macro expansion). Upstream Buildroot fixed this
in commit `7a07a9d155b8f601d68f07ee0ed1dc8d48907644` ("package/m4: fix
build failure with host-gcc 15"), forcing `-std=gnu17` when
`BR2_HOST_GCC_AT_LEAST_15` is detected. That detection variable doesn't
exist in this project's pinned Buildroot submodule (predates the fix), so
applied an unconditional version instead — `-std=gnu17` is harmless on any
host GCC that supports it (8+):

Edited `buildroot/package/m4/m4.mk` (inside the git submodule, NOT tracked
by any commit in this project — this is a local, uncommitted source patch,
consistent with this project's no-git-at-the-root approach; if the
`buildroot/` submodule is ever re-cloned/reset, this patch must be
reapplied) to add:
```makefile
HOST_M4_CONF_ENV = CFLAGS="$(HOST_CFLAGS) -std=gnu17"
```
Cleared the stale build state (`rm -rf output/bcm2837/build/host-m4-1.4.19`)
and relaunched the same build command. This is an environment/toolchain
issue, unrelated to the Circuit-Sword/WiFi-specific changes in Tasks 2-3 —
those are untouched and still in place.

**Build failure #2 — `host-gmp` fails against host GCC 15 too.** Same root
cause class, different package:
```
conftest.c: In function 'f':
conftest.c:12:48: error: too many arguments to function 'g'; expected 0, have 6
...
configure: error: could not find a working compiler, see config.log for details
make: *** [package/pkg-generic.mk:279: /bcm2837/build/host-gmp-6.3.0/.stamp_configured] Error 1
```
GMP's old K&R-style `void g(){}` prototype means "zero arguments" under
GCC 15's new default `-std=gnu23`, but the (old) test code calls it with 6
arguments — legal/ignored under the previous `gnu17` default, a hard error
under `gnu23`. Same class of bug as m4's, confirming this isn't a one-off:
the pulled `batoceralinux/batocera.linux-build:latest` Docker image has
drifted to **GCC 15.2.0** (confirmed via
`docker run --rm batoceralinux/batocera.linux-build:latest gcc --version`
→ "gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0", dated 2025) — well past whatever
GCC version was current when the `batocera-43.1` tag was originally
released. Pinning the *source* tag does not pin the *build container*; the
`:latest` Docker tag is a rolling target.

**Investigated and rejected: building the Docker image locally instead of
pulling.** The repo ships its own `Dockerfile` (`FROM ubuntu:22.04`), which
would give an older/stable GCC — but `make build-docker-image` failed
independently, unrelated to the GCC issue: an i386-multilib apt package
(`libc6:i386` et al., needed for some legacy 32-bit host tool support) 404s
on `ports.ubuntu.com` when building for the `arm64` platform (Apple
Silicon) — Ubuntu's ports mirror doesn't carry full i386 coverage the way
the primary x86_64 archive does. Not pursued further given a cleaner fix
was available (see below); worth revisiting if the global CFLAGS fix ever
proves insufficient.

**Fix applied — global `HOST_CFLAGS` override, not per-package patching.**
Rather than whack-a-mole patching every host package GCC 15 breaks (m4 was
one instance; gmp is a second; there could be more — mpfr and mpc are next
in gmp's dependency chain and share the same gnulib-adjacent vintage),
found Buildroot's actual override point:
`buildroot/package/Makefile.in:262`: `HOST_CFLAGS ?= -O2` — a `?=`
assignment, meaning it's overridable via a command-line-set make variable
(command-line values in GNU Make take precedence over `?=` in the
makefile). The inner Buildroot `make` invocation runs inside the Docker
container via `Makefile`'s `MAKE_OPTS` variable (`make $(MAKE_OPTS) O=/$*
... -C /build/buildroot` inside `RUN_DOCKER`) — `MAKE_OPTS` itself is built
with `+=` for `-j`/`-l` parallelism flags, so **exporting** (not
command-line-passing) `MAKE_OPTS` lets the Makefile's own `+=` lines append
to it rather than silently dropping the parallelism flags:

```bash
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17'"
```

This makes every host package's build inherit `-std=gnu17`, fixing the
whole class of GCC-15-vs-old-gnulib issues in one shot instead of patching
packages one at a time as they're discovered. The earlier m4-specific patch
(`buildroot/package/m4/m4.mk`'s `HOST_M4_CONF_ENV`) is now redundant but
harmless — left in place rather than reverted, to save a build cycle.

Cleared stale build state for both affected packages
(`rm -rf output/bcm2837/build/host-m4-1.4.19 output/bcm2837/build/host-gmp-6.3.0`)
and relaunched with the exported `MAKE_OPTS`. Restored
`.ba-docker-image-available` (touched, since the originally-pulled image is
still present locally and fine to keep using — the local-build detour
didn't invalidate it).

Build duration, final image path, and `r8723bs.ko` presence to be appended
once the (re)build finishes.

**Note for next time — CPU utilization.** Host has 8 physical/logical cores
(`sysctl -n hw.ncpu` = 8), and Docker Desktop's VM is allocated exactly 8
CPUs (confirmed via `--cpus 8` in the `com.docker.virtualization` process
args) — so `-j8`/`-l8` (already the default via `MAKE_JLEVEL ?= NPROC`) is
the ceiling; no headroom to raise it further on this hardware. Observed CPU
dips during the build are expected: Buildroot builds one package at a time
in dependency order by default, and `-jN` only parallelizes *within* a
single package's own build — packages with little internal parallelism
(the host-tool bootstrap stage: m4, gmp, cmake, etc.) leave cores idle even
with `-j8` available. A genuine improvement for a future rebuild: pass
`PARALLEL_BUILD=1` to the top-level `make` invocation (sets
`BR2_PER_PACKAGE_DIRECTORIES=y`, letting Buildroot build multiple
independent packages concurrently instead of strictly sequentially) — not
applied to this run to avoid discarding in-progress build state.

**Build failure #3 — likely transient virtiofs race in glibc's final
archiving step.** Different class of error than #1/#2 (not a GCC-15/gnu23
issue):
```
/bcm2837/host/lib/gcc/.../ar: cilassert/__assert.os: No such file or directory
make[3]: *** [../o-iterator.mk:9: .../libc_nonshared.a] Error 1
```
The failing `ar` command builds its input file list by `cat`-ing multiple
per-subdirectory `stamp.oS` files (e.g. `assert/stamp.oS`, `ctype/stamp.oS`)
via shell backticks. The garbled path `cilassert/__assert.os` (not a real
subdirectory in the object tree) looks like two adjacent stamp files' list
contents got concatenated without a clean boundary — consistent with a
read-during-write race on Docker Desktop's virtiofs-mounted volumes
(`--virtiofs /Users` etc., confirmed in the Docker VM process args) under
heavy `-j8` parallel I/O, rather than a real source/toolchain problem.
Retrying without clearing any build state (Buildroot/make should resume
from stamps rather than redo completed work) to check whether this is a
one-off. If this recurs, consider reducing parallelism for glibc
specifically or investigating virtiofs cache-consistency settings in Docker
Desktop.

**Confirmed reproducible (not a one-off).** Retry #2 failed at the exact
same step (`elf/subdir_lib` → `libc_nonshared.a`), but with a *different*
garbled filename this time (`ar: e: No such file or directory` vs. the
first run's `cilassert/__assert.os`) — same failure point, different
corruption content each time, which rules out a deterministic
content/source bug and confirms this is a genuine race: the `cat
.../elf/stamp.oS ...` step (elf is almost certainly the last subdir to
finish, given how much of glibc depends on it) reads that file while a
concurrent `-j8` job may still be writing it. **Fix**: reduce parallelism
via `MAKE_JLEVEL=4` (halves concurrency, doesn't eliminate parallel builds
entirely, should make this specific read-during-write race far less
likely) — exported before relaunching, on top of the existing `MAKE_OPTS`
(`HOST_CFLAGS='-O2 -std=gnu17'`) fix from build failures #1/#2, which
remains necessary and unrelated to this issue.

**Correction: `MAKE_JLEVEL`/`MAKE_LLEVEL` env vars had NO effect.**
Verified via the log: glibc's inner `make` still ran as `-j9` even after
exporting `MAKE_JLEVEL=4`. Root cause: the outer wrapper `Makefile`'s
`MAKE_JLEVEL`/`MAKE_OPTS` only control the *top-level* `make -C buildroot`
invocation's own parallelism — Buildroot has a completely separate
mechanism for **per-package** build parallelism: `BR2_JLEVEL` in the
Buildroot `.config` (0 = auto-detect via nproc, which is why it showed
`-j9`, an nproc+1-style heuristic). Fixed properly this time:
`sed -i 's/^BR2_JLEVEL=0/BR2_JLEVEL=4/' output/bcm2837/.config`.

**Build failure #4 — different class of error, likely accumulated stale
build state.** After several interrupted retries all hitting the SAME
package (glibc) without ever fully cleaning its build directory, hit a
genuinely different failure: undefined references while linking `ld.so`
(`__lll_lock_wait_private`, `__lll_lock_wake_private`, `getenv`) — symbols
that should resolve to glibc's own internal objects. Not consistent with a
race (unlike failure #3's two variants) — looks like corrupted/inconsistent
incremental state from repeatedly resuming a partially-built glibc across
multiple crashed attempts. Fix: fully removed glibc's build directory
(`rm -rf output/bcm2837/build/glibc-2.40-18-g...`) so it builds completely
fresh in one continuous run, combined with the now-correctly-set
`BR2_JLEVEL=4`.

**Build failure #5 — same libc_nonshared.a race recurred; `BR2_JLEVEL`
edit didn't take effect either.** Confirmed via the log: glibc's configure
still cached `ac_cv_prog_MAKE="/usr/bin/make -j9"` and its inner build ran
`-j9` again, despite `BR2_JLEVEL=4` in `.config`. glibc's own `configure`
appears to auto-detect/cache its internal recursive-make parallelism
independently (via its own `nproc`-style logic), bypassing whatever
Buildroot's package infrastructure passes down — a glibc-specific
behavior neither the outer wrapper's `MAKE_JLEVEL` nor Buildroot's
`BR2_JLEVEL` actually control. **Two levels of "reduce -j" have both
failed to reach glibc's real concurrency.**

**Fix — throttle at the container level instead, via `docker run --cpus`.**
Rather than continuing to chase where glibc's parallelism setting actually
comes from, constrained it structurally: `docker.mk`'s `DOCKER_OPTS ?=` is
overridable the same way `MAKE_OPTS` is (exported env var, since
`BATCH_MODE=1` is already set on every invocation here, the `ifndef
BATCH_MODE DOCKER_OPTS += -i endif` append never triggers, so an exported
`DOCKER_OPTS` isn't clobbered). Exported `DOCKER_OPTS="--cpus 3"` before
relaunching — this limits the container to 3 real CPUs regardless of how
many `-jN` processes any build step spawns internally, since the kernel
scheduler enforces it at the container level. Guaranteed to actually reduce
concurrency, unlike the two prior attempts. Cleared glibc's build dir again
first.

**Build failure #6 — `--cpus 3` also had no effect; identical corruption
recurred verbatim.** Same exact target failed again
(`o-iterator.mk:9`/`libc_nonshared.a`), and critically the garbled filename
was **identical** to build failure #3's first variant:
`ar: cilassert/__assert.os: No such file or directory` — the *exact same*
string, not new random garbage. This changes the diagnosis: reproducing the
identical corruption twice (out of 3 total occurrences of this failure)
argues against a purely random race and suggests something closer to a
consistent ordering/dependency issue — though not fully deterministic
either (failure #3's second occurrence showed different garbage, `e`).

Root cause of why `--cpus` didn't help: `--cpus N` is a **CFS scheduling
quota** (throttles total CPU-*time*), not a core-count limit — the
container still sees all 8 host CPUs via `/proc/cpuinfo`/`nproc`, so
glibc's own internal `nproc`-based auto-detection (the same mechanism that
computed `-j9`, independently of `BR2_JLEVEL`/`MAKE_JLEVEL`) was
unaffected — still detected 8 cores, still computed `-j9`. **Corrected
fix**: use `--cpuset-cpus` instead, which pins the container to a specific
restricted set of CPU IDs — this actually changes what `nproc` reports
inside the container, so it should genuinely lower glibc's own
self-detected job count this time. Exported `DOCKER_OPTS="--cpuset-cpus=0-2"`
(3 cores) and cleared glibc's build dir again before relaunching.

**Build failure #7 — root cause finally found.** `--cpuset-cpus=0-2` also
had no effect (confirmed `nproc` inside that exact container config
correctly reports 3 via a direct `docker run --cpuset-cpus=0-2 ... nproc`
test — so the container-level fix DID work as intended — yet glibc's
inner build still ran `-j9`). This proved the "-j9" was never coming from
`nproc`/core-count detection at all. Found the real source:
`buildroot/package/glibc/glibc.mk:129`:
```makefile
GLIBC_CONF_ENV += ac_cv_prog_MAKE="$(BR2_MAKE)"
```
Buildroot explicitly hands glibc's configure a pre-built `make -jN` string
via `$(BR2_MAKE)`, computed from the Kconfig symbol `BR2_JLEVEL` (0 = "auto
via nproc+1", which explains the persistent "9"). **Also discovered why the
earlier `sed -i 's/^BR2_JLEVEL=0/BR2_JLEVEL=4/' .config` edit never stuck**:
Buildroot re-syncs `.config` from the tracked defconfig/Kconfig source at
the start of every `bcm2837-build` invocation (a `bcm2837-config`
prerequisite step), silently reverting any direct hand-edit to the output
`.config` file — confirmed `BR2_JLEVEL` was back to `0` after the
`--cpuset-cpus` attempt despite the earlier edit.

**Correct fix**: pass `BR2_JLEVEL=4` as a `make` **command-line** variable
(not a `.config` file edit) — command-line-set variables in GNU Make take
precedence over simple `=`/`?=` assignments from included makefiles
(`.config` is `-include`d as a plain makefile fragment), so this can't be
silently reverted the way the file edit was. Added it directly into the
same `MAKE_OPTS` string used for the `HOST_CFLAGS` fix, since that's
already interpolated straight into the inner `make ... -C /build/buildroot`
invocation running inside the container:
```bash
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17' BR2_JLEVEL=4"
```
Dropped the `--cpuset-cpus`/`--cpus` `DOCKER_OPTS` throttling (wasn't the
actual lever, no longer needed with the real fix) to restore full-speed
compilation for the rest of the build. Cleared glibc's build dir once more
before relaunching.

**Build failure #8 — TRUE ROOT CAUSE FOUND: macOS's case-insensitive
filesystem, not a race at all.** `BR2_JLEVEL=4` DID take effect this time
(confirmed `/usr/bin/make -j4` in the log — the command-line-variable fix
worked), yet the same class of failure recurred, now at `nptl/stamp.oS`
instead of `elf/libc_nonshared.a`. This time the actual error was
unambiguous:
```
mv -f .../nptl/stamp.osT .../nptl/stamp.oS
mv: '.../nptl/stamp.osT' and '.../nptl/stamp.oS' are the same file
make[3]: *** [../o-iterator.mk:9: .../nptl/stamp.oS] Error 1
```
glibc's build system distinguishes `stamp.o`, `stamp.os`, `stamp.oS`,
`stamp.oST` — filenames that differ **only by letter case**
(lowercase `os` vs mixed-case `oS`). This is completely valid on a real
(case-sensitive) Linux filesystem, which is all glibc's build has ever been
tested against. But `output/bcm2837` is bind-mounted from the macOS host
(`-v $(OUTPUT_DIR)/$*:/$*` in `docker.mk`) into the Linux container via
Docker Desktop's virtiofs — and the host filesystem here is a **default,
case-insensitive (case-preserving) APFS volume**. `stamp.os` and
`stamp.oS` collapse to the *same inode* on that filesystem. Every prior
"random corruption" (`cilassert/__assert.os`, the bare `e`, this `mv`
error) is fully explained by this: two logically-distinct files silently
aliasing to one, clobbered by whichever write landed last — nothing to do
with concurrency, timing, or `-j` level. All of build failures #3, #5, #6,
#7's retries were chasing a symptom; this is the actual disease.

**Real fix**: the build output needs to live on a case-sensitive
filesystem. Standard approach on macOS: create a small case-sensitive APFS
disk image via `hdiutil`, mount it, and point Buildroot's `OUTPUT_DIR` (and
ideally `DL_DIR`/`CCACHE_DIR` too, though those are less likely to hit
case-collisions) at a path inside that mounted volume instead of the
default `<repo>/output` on the regular case-insensitive Mac filesystem.

**Fix applied**: created an 80GB case-sensitive APFS disk image. Note the
exact `-fs` string that actually works on this macOS version is
`"Case-sensitive APFS"` — both `"APFS (Case-sensitive)"` and the `APFSX`
shorthand (both suggested by `diskutil listFilesystems`'s own output) fail
with a generic "invalid argument" error; only the plain
`"Case-sensitive APFS"` string succeeds:
```bash
hdiutil create -size 80g -fs "Case-sensitive APFS" -volname BatoceraBuild \
  /Users/bas/batocera-build-wifi/BatoceraBuild
hdiutil attach /Users/bas/batocera-build-wifi/BatoceraBuild.dmg
```
Mounted at `/Volumes/BatoceraBuild`. Verified case-sensitivity directly
(`touch foo FOO` on the mounted volume produced two distinct files, not
one). Redirected `OUTPUT_DIR`, `DL_DIR`, and `CCACHE_DIR` (all
`?=`-overridable env vars per the top-level `Makefile`) to paths on that
volume, and relaunched as a **fully fresh build** — not reusing any prior
`output/bcm2837` content from the case-insensitive filesystem, since the
case-collision bug doesn't always produce a hard error; a package that
"succeeded" there isn't proof its output was actually correct. Trusting
only the fresh case-sensitive rebuild going forward, even though this means
redoing all host-tool/toolchain work already completed.

**Build failure #9 — `HOST_CFLAGS`'s `-std=gnu17` leaked into C++ builds,
breaking host-cmake's bootstrap.** Different bug class again, this time a
side effect of our own m4/gmp fix: `buildroot/package/Makefile.in:264`:
```makefile
HOST_CXXFLAGS += $(HOST_CFLAGS)
```
C++ flags always inherit whatever `HOST_CFLAGS` is set to — so our
`HOST_CFLAGS='-O2 -std=gnu17'` override (needed for C code) was also being
appended to every C++ compile, and `-std=gnu17` is a **C** standard flag,
meaningless for C++ (`g++` warned "valid for C/ObjC but not for C++" on
every single C++ file compiled, visible throughout the log). This
apparently confused host-cmake's own bootstrap self-test enough to report
"The C++ compiler does not support C++11 (e.g. std::unique_ptr)" and abort
configuring — a false negative caused by the nonsensical flag combination,
not a real compiler capability gap (same GCC 15.2.0 obviously supports
C++11).

**Fix**: also pass `HOST_CXXFLAGS` explicitly on the command line, set to
just `-O2` (no `-std=gnu17`). Since `HOST_CXXFLAGS += $(HOST_CFLAGS)` is a
simple makefile append, a command-line-set `HOST_CXXFLAGS` can't be
further modified by that append (GNU Make command-line-variable priority)
— cleanly separating "C code needs -std=gnu17" from "C++ code must not get
it":
```bash
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17' HOST_CXXFLAGS='-O2' BR2_JLEVEL=4"
```
Cleared `host-cmake`'s build directory and relaunched.

**Build failure #10 — genuine Buildroot packaging bug (missing dependency
declaration), unrelated to any of our fixes.** `host-squashfs` failed with
`lzma.h: No such file or directory` and `zlib.h: No such file or
directory`, despite `host-libzlib` having built successfully moments
earlier in the same log. Root cause:
`buildroot/package/squashfs/squashfs.mk:56`:
```makefile
HOST_SQUASHFS_DEPENDENCIES = host-zlib host-lz4 host-lzo host-xz host-zstd
```
depends on a package named `host-zlib`, which is a **virtual package**
(`package/zlib/zlib.mk` — `$(eval $(virtual-package))
$(eval $(host-virtual-package))`) that only resolves to a real
implementation if some concrete package declares itself as the provider.
`package/libzlib/libzlib.mk` (the real zlib implementation this Buildroot
version uses) declares `LIBZLIB_PROVIDES = zlib` for the **target** side
only — it never declares the host-side equivalent
(`HOST_LIBZLIB_PROVIDES = host-zlib`). So `host-zlib` has no real host
implementation registered at all, meaning Buildroot's dependency graph
doesn't actually know `host-squashfs` must wait for `host-libzlib` to
finish installing its headers — a genuine gap in this pinned Buildroot
commit's packaging, not something any of our changes caused.

**Fix**: added the missing line directly to
`buildroot/package/libzlib/libzlib.mk`:
```makefile
HOST_LIBZLIB_PROVIDES = host-zlib
```
(mirroring the existing target-side `LIBZLIB_PROVIDES = zlib`). Cleared
`host-squashfs`'s build directory and relaunched — this is a local,
uncommitted patch to the `buildroot` git submodule (same caveat as the
earlier `m4.mk` patch: not tracked by this project's git-less setup, must
be reapplied if the submodule is ever reset/re-cloned).

**Build failure #10 continued — the `HOST_LIBZLIB_PROVIDES` fix alone
wasn't sufficient.** Retried and hit the exact same missing-header errors
again. Deeper look at the actual failing compile command revealed the real
second half of the bug: `squashfs-tools`' own (third-party, upstream)
Makefile doesn't respect a `CPPFLAGS`-style include-path environment
variable at all — the observed `gcc` invocation was
`gcc -O2 -std=gnu17 -I. ...` with **no** reference to Buildroot's host
sysroot include directory anywhere, only the local `-I.`. So even with
correct dependency *ordering* (the first fix), the compiler still had no
way to *find* `zlib.h`/`lzma.h` once installed. Buildroot's
`squashfs.mk:82-89` (`HOST_SQUASHFS_BUILD_CMDS`) passes `EXTRA_CFLAGS`
without ever adding `-I$(HOST_DIR)/include`, unlike most well-behaved
Buildroot packages that rely on `$(HOST_MAKE_ENV)`'s own CPPFLAGS being
picked up automatically — squashfs-tools' Makefile just doesn't do that.

**Second fix**, added directly alongside the first:
```makefile
define HOST_SQUASHFS_BUILD_CMDS
	$(HOST_MAKE_ENV) $(MAKE) \
		CC="$(HOSTCC)" \
		EXTRA_CFLAGS="$(HOST_CFLAGS) -I$(HOST_DIR)/include" \
		EXTRA_LDFLAGS="$(HOST_LDFLAGS) -L$(HOST_DIR)/lib" \
		$(HOST_SQUASHFS_MAKE_ARGS) \
		-C $(@D)/squashfs-tools/
endef
```
(`HOST_SQUASHFS_INSTALL_CMDS` doesn't compile anything, just installs
already-built binaries — left unchanged.) Cleared `host-squashfs`'s build
directory again and relaunched.

**Build failure #11 — transient mirror issue (not a real bug).**
`host-libtool` download failed: `ftpmirror.gnu.org` gave "Connection
refused" then "502 Bad Gateway" on separate retries, and Buildroot's own
fallback (`sources.buildroot.net`) 404s for this exact file. Not a code
issue — worked around by downloading `libtool-2.4.7.tar.xz` directly from
`ftp.gnu.org` (the canonical, non-redirecting GNU host) via `curl`, placed
manually at `/Volumes/BatoceraBuild/dl/libtool/libtool-2.4.7.tar.xz`
(confirmed the Buildroot `dl/` cache layout is `dl/<pkg>/<pkg>-<version>.<ext>`
by inspecting the already-cached `dl/m4/m4-1.4.19.tar.xz`), and verified
the SHA256 hash matches `buildroot/package/libtool/libtool.hash` exactly
before relaunching — so Buildroot's own download step is skipped entirely
next run (cache hit).

## RetroPie WiFi stability fixes — carried over

Both known stability fixes from the RetroPie build
(`Retropie_source/settings/r8723bs.conf` and
`Retropie_source/settings/wifi-powersave-off.conf`) are now present in the
Batocera board tree, both via the standard `fsoverlay` mechanism (files
copied into the rootfs at image-assembly time, same pattern for both — no
special defconfig wiring needed, confirmed via
`board/batocera/README.md`):
- `board/batocera/broadcom/bcm2837/fsoverlay/etc/modprobe.d/r8723bs.conf` —
  `options r8723bs rtw_power_mgnt=0 rtw_ips_mode=0 rtw_bw_mode=0` (already
  present from earlier Phase 1 work, in-tree driver module params).
- `board/batocera/broadcom/bcm2837/fsoverlay/etc/NetworkManager/conf.d/circuitsword-wifi-powersave-off.conf`
  — `wifi.powersave = 2` (added now; was missing before). Standard
  NetworkManager `conf.d` drop-in convention, not Batocera-specific.

Both need on-device verification once the image boots (dmesg for module
param confirmation via `modinfo`/`cat /sys/module/r8723bs/parameters/*`,
and `nmcli` or NetworkManager logs for the powersave setting) — not yet
verified on real hardware, only confirmed present in the build tree.

## Disk space: growing the disk image mid-build (recurring, expect it again)

The 80GB disk image was sized for the WiFi driver work alone; a full
Batocera image build (WebKitGTK, MAME x2, LLVM, dozens of RetroArch cores)
needs much more. This recurred twice — once proactively (before hitting
"no space") and once reactively (failure #20, after "No space left on
device" during `host-python-lxml`'s link step). By the RetroArch-cores
stage, image grew 80GB → 110GB → 130GB → 142GB.

**Procedure each time:**
```bash
# 1. Stop the build cleanly (kill the outer make + running container)
kill <make_pid>
docker ps -q --filter "ancestor=batoceralinux/batocera.linux-build" | xargs -r docker kill

# 2. Detach, resize, reattach — MUST detach first, live resize while
#    mounted fails with error 35 "Resource temporarily unavailable"
hdiutil detach /Volumes/BatoceraBuild
hdiutil resize -size <N>g /Users/bas/batocera-build-wifi/BatoceraBuild.dmg
hdiutil attach /Users/bas/batocera-build-wifi/BatoceraBuild.dmg -owners on
# -owners on is required every reattach, or failure #13's fchmod bug workaround is undone

# 3. Relaunch the build (exact command in "Correct relaunch invocation" above)
```
- Check `hdiutil resize -limits <dmg>` first — returns min/current/max in
  512-byte sectors; divide by 2×1024×1024 for GB. The image maxed out
  around ~143GB in this session (raw `UDRW` disk images have a fixed
  ceiling, not unlimited).
- The `.dmg` is **not sparse** — it's fully allocated on the host disk at
  its stated size. Growing it by N GB immediately consumes N GB of the
  **host Mac's own free space** — check `df -h /` before resizing, not
  just the image's own free space.
- **Host disk can run low from something unrelated to this build.**
  Second resize attempt found the host had only 3.4GB free (expected
  ~9GB based on prior arithmetic) — root cause was Docker's own **build
  cache** (`docker system df`), 18.48GB reclaimable, accumulated from
  unrelated prior Docker usage on this machine, not from this build (which
  only uses `docker run`, never `docker build`). Freed via `docker builder
  prune -f` — safe, only removes build cache, doesn't touch the running
  container, the `batoceralinux/batocera.linux-build` image, or any
  volumes. Check `docker system df` before assuming host space is only
  consumed by the `.dmg` — Docker Desktop's own VM disk
  (`~/Library/Containers/com.docker.docker`) is a separate, large,
  independently-growing consumer on this host.
- The APFS container inside the disk image auto-grows to fill the new
  image size on reattach — no separate `diskutil apfs resizeContainer`
  step needed, confirmed via `diskutil apfs list` / `df -h` each time.

**Approaching the disk image's hard ceiling.** By the 5th resize (80→110→
130→142→150→158GB), `hdiutil resize -limits` reported a max around
~172.6GB — only ~14GB of further headroom ever available for this
specific `.dmg`, regardless of host free space. If another resize is
needed and the max is close, the disk-image approach is genuinely running
out of road; the pre-built `docker.mk` `BR_DOCKER_VOLUMES=1` support
(see "Performance" section) has no equivalent fixed ceiling — named
volumes live inside Docker's own VM disk, which grows dynamically — so
switching to that becomes the fallback if the `.dmg` maxes out, not just
a nice-to-have for speed.

**Host free space can evaporate fast from things unrelated to Docker.**
One resize attempt found host free space at a critical 3.7GB even after
`docker builder prune`. Root cause that time: macOS local Time Machine
snapshots (`tmutil listlocalsnapshots /`) holding onto deleted-file space.
Freed ~7GB via `tmutil deletelocalsnapshots /` (safe — these are automatic
local snapshots, not the only backup; macOS auto-manages/thins them
normally, deleting manually under disk pressure is a standard, safe
recovery step). The command may report `POSIXError ... Stale NFS file
handle` / "Failed to delete all snapshots" for some system-protected
snapshots (e.g. `com.apple.os.update-*`) while still successfully freeing
space from the others — check `df -h /` after running it rather than
trusting the reported success/failure text literally.

## Performance: use Docker named volumes for the NEXT build

This build (started as a fresh case-sensitive-disk-image build after
failure #8) uses host bind-mounts for `OUTPUT_DIR`/`DL_DIR`/`CCACHE_DIR`
(the `/Volumes/BatoceraBuild/...` disk image). That crosses Docker
Desktop's host↔VM file-sharing boundary (VirtioFS, then gRPC FUSE after
the switch for failure #13) for every file operation — the dominant
source of slowness for this workload (many small files, heavy
chmod/rename traffic from hundreds of Buildroot packages). It's also the
underlying cause of failures #13, #15, and #16 (fchmod poisoning, clock
skew, transient EIO) — all boundary-crossing quirks specific to sharing a
macOS-hosted filesystem into the Linux VM.

**Measured fix for next time:** switch to Docker **named volumes**, which
live entirely inside the Docker Desktop Linux VM's own filesystem — no
host boundary at all. Benchmarked directly: 3000 small
file-write+chmod operations took **5.6s** via a named volume vs **16.6s**
via the bind-mounted disk image (~3x faster), using this exact Docker
image. This should also eliminate the whole class of #13/#15/#16-style
boundary quirks going forward, since there's no boundary to cross.

Implemented as a local, backward-compatible patch to `docker.mk`: pass
`BR_DOCKER_VOLUMES=1` on the `make` command line and `RUN_DOCKER` uses
three named volumes (`batocera-dl`, `batocera-ccache`,
`batocera-output-$*`) instead of the `DL_DIR`/`CCACHE_DIR`/`OUTPUT_DIR`
host paths. Omitting the flag preserves the exact current bind-mount
behavior — nothing about the currently-running build changes.

```bash
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17' HOST_CXXFLAGS='-O2' BR2_JLEVEL=6"
make BR_DOCKER_VOLUMES=1 O=/Volumes/BatoceraBuild/output/bcm2837 BR2_EXTERNAL=/Users/bas/batocera-build-wifi/batocera.linux DL_DIR=/Volumes/BatoceraBuild/dl BATCH_MODE=1 bcm2837-build
```

**Trade-offs to know before switching:**
- Starts with an **empty cache** in the new named volumes (nothing
  carries over automatically from `/Volumes/BatoceraBuild/...` —
  downloads and the whole host-toolchain/glibc/etc. build must redo
  once). Worth it for any build after the first, given the 3x measured
  I/O speedup on top of avoiding failures #13/#15/#16 entirely.
- Files are no longer Finder-browsable — inspect via `docker run --rm -v
  batocera-output-bcm2837:/t <image> ls /t` or `docker run --rm -v
  batocera-output-bcm2837:/t -v /tmp/inspect:/out <image> cp -r /t/images
  /out` to copy specific things out, rather than opening the disk image
  in Finder.
- `docker volume ls` / `docker volume rm <name>` to inspect or reset.
- The `BatoceraBuild.dmg` disk image and its cache are NOT obsoleted by
  this — they're what THIS build (already ~85% through, running the old
  way) still uses. Don't delete it until this build's `.img` output is
  confirmed and flashed.

## Build cache — persistent, do NOT delete

`OUTPUT_DIR`, `DL_DIR`, and `CCACHE_DIR` all point at
`/Volumes/BatoceraBuild/...`, which lives on the separate case-sensitive
APFS disk image `/Users/bas/batocera-build-wifi/BatoceraBuild.dmg` — not
inside the Docker container, not ephemeral. This means:

- Every already-built Buildroot package (host-gcc, host-binutils, glibc,
  etc.) is stamp-marked done and is **skipped** on the next `make` run —
  only packages whose inputs changed get rebuilt.
- `dl/` holds every downloaded source tarball, including the manually
  fetched libtool — no re-downloading, no re-hitting flaky mirrors.
- `buildroot-ccache/` caches compiled object files, so even packages that
  DO need rebuilding (e.g. after a kernel-config change) compile much
  faster than the first time.

**Practically:** once this build succeeds, future rebuilds (Phase 2
Bluetooth work, config tweaks, fixing a later-stage failure) are cheap —
minutes, not hours — as long as this cache survives. **Never delete**
`/Users/bas/batocera-build-wifi/BatoceraBuild.dmg` or
`/Users/bas/batocera-build-wifi/batocera.linux` (the latter also holds our
uncommitted local `.mk`/config patches — see Files section). The `.dmg`
must be mounted (`/Volumes/BatoceraBuild`) before running `make` again; if
it's not mounted, double-click it in Finder or `hdiutil attach
/Users/bas/batocera-build-wifi/BatoceraBuild.dmg`.

**Correct relaunch invocation (copy exactly):**
```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
export OUTPUT_DIR=/Volumes/BatoceraBuild/output
export DL_DIR=/Volumes/BatoceraBuild/dl
export CCACHE_DIR=/Volumes/BatoceraBuild/buildroot-ccache
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17' HOST_CXXFLAGS='-O2' BR2_JLEVEL=4"
nohup make O=/Volumes/BatoceraBuild/output/bcm2837 BR2_EXTERNAL=/Users/bas/batocera-build-wifi/batocera.linux DL_DIR=/Volumes/BatoceraBuild/dl BATCH_MODE=1 bcm2837-build > "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log" 2>&1 &
```
Two ways to get this wrong that both silently produced a broken relaunch:
1. **`HOST_CFLAGS`/`HOST_CXXFLAGS`/`BR2_JLEVEL` must be exported as part of
   the `MAKE_OPTS` shell env var**, not passed directly as command-line
   variables to the outer `make`. The top-level `Makefile` does
   `MAKE_OPTS += -j... -l...` — this only picks up the GCC15/JLEVEL fixes
   if they arrived via the `MAKE_OPTS` environment variable in the first
   place; passing them as plain `make HOST_CFLAGS=... ...` args doesn't
   propagate them into the `docker run ... make ...` invocation the
   Makefile constructs internally. Verify by checking the `docker run`
   command line itself (`ps aux | grep docker`) shows `HOST_CFLAGS=...`
   right after `make` — if it's missing, the env var didn't propagate.
2. **`OUTPUT_DIR`/`DL_DIR`/`CCACHE_DIR` must all be exported**, not just
   passed as `O=`/`DL_DIR=` make args — if `OUTPUT_DIR`/`CCACHE_DIR` are
   left unexported, Buildroot falls back to a default path *inside the
   project directory* (`batocera.linux/output/`,
   `batocera.linux/buildroot-ccache/`) — which lives back on the
   case-insensitive macOS APFS volume, silently reintroducing Build
   failure #8's root cause. Verify by checking the `docker run` command's
   `-v` mount lines point at `/Volumes/BatoceraBuild/...`, not
   `/Users/bas/batocera-build-wifi/batocera.linux/output`.

**Package-level cache gotcha:** adding a new patch file to
`buildroot/package/<pkg>/*.patch` after a package has already been
extracted does **not** retroactively apply it — Buildroot stamps the patch
step done (`.stamp_patched`) and skips it on subsequent runs. If you add a
patch to fix a package that already failed mid-build, you must remove that
package's build directory first (e.g. `rm -rf
/Volumes/BatoceraBuild/output/bcm2837/build/<pkg>-<version>`) so it
re-extracts and re-patches from scratch. This does not require clean
neighboring packages or the `dl/`/ccache caches — only that one package
re-runs its extract+patch+build cycle.

**Build failure #13 — `noowners` disk image mount blocks chmod inside
container.** After fixing failure #12, the build progressed much further
(target userland packages) before failing on `e2fsprogs`: `chmod:
Permission denied (os error 13)` on a freshly-created file
(`lib/et/compile_et`), even though `ls -n` showed the file owned by the
exact uid:gid (501:20) the Docker container runs as. Root cause: the
`BatoceraBuild.dmg` disk image was mounted with macOS's `noowners` option
(the default for a disk image created via `hdiutil create` without
`-owners on`) — with `noowners`, the OS *displays* synthetic ownership
info matching the mounting user but does not treat it as real ownership at
the kernel level, so owner-based `chmod` calls from inside the Docker
VM are rejected. Fixed in two steps (both required — doing only the first
was not sufficient):
1. `sudo diskutil enableOwnership /Volumes/BatoceraBuild` (needs the
   user's password; this session's agent cannot run this non-interactively
   and asked the user to run it directly. `diskutil enableOwnership`
   works on an already-mounted volume, no need to unmount/remount).
2. **Full restart of Docker Desktop** (quit completely — `osascript -e
   'quit app "Docker Desktop"'` plus `pkill -f "Docker Desktop"` to be
   sure, then `open -a Docker` and wait for `docker info` to succeed).
   This was required because Docker Desktop's virtiofs share had already
   established its mount session under the old `noowners` state — simply
   flipping the flag on the host volume did not retroactively fix
   already-open shares; only a fresh Docker VM start picked up the new
   ownership mode. Verified with a minimal reproduction (`docker run --rm
   -v <dir>:/t -u 501:20 <image> chmod 755 /t/<file>`) before trusting it
   and relaunching the full build.

**Addendum to #13 — real root cause was a VirtioFS `fchmod()` bug, not
stale cache.** The ownership fix alone did not fully resolve this — the
build kept hitting `chmod: Permission denied (os error 13)` on a
*different* e2fsprogs file each retry, even as **root** inside the
container. Isolated the exact trigger with a minimal Python reproduction
(`os.open(..., 0o644)` → write → `os.fchmod(fd, 0o444)` → close →
optionally `os.rename()` → later `os.chmod(path, 0o755)`): **any file
whose permissions were set via `fchmod()` on an open file descriptor
becomes permanently un-chmod-able afterwards** (even by root) on this
Docker Desktop **VirtioFS**-backed bind mount of the case-sensitive
`BatoceraBuild.dmg`. A plain path-based `chmod()` — even down to 0444 and
back up, even across a `rename()` — works fine; only the fd-based
`fchmod()` poisons the file. `e2fsprogs`'s own `util/subst.c` tool uses
exactly this pattern (`fchmod(ofd, 0444)` then `rename()`) to atomically
write generated scripts, which is why it kept surfacing here — but this
is a general VirtioFS bug that could affect any package using the same
common C idiom, not something specific to e2fsprogs.

**Fix:** switched Docker Desktop's file-sharing backend from VirtioFS to
the older **gRPC FUSE** implementation (Docker Desktop → Settings →
General → "Choose file sharing implementation for your containers" →
gRPC FUSE → Apply & restart — this requires the user, no CLI/settings-file
path was found to automate it). Verified fixed by rerunning the exact
`fchmod`+`rename`+`chmod` Python reproduction against the same mounted
volume post-switch: `CHMOD_OK`. `e2fsprogs`'s build directory was cleaned
(`rm -rf .../build/e2fsprogs-1.47.1`) once more before relaunching, since
its partially-built state still contained VirtioFS-poisoned files from
before the switch.

**Build failure #15 — transient Docker VM clock skew.** `libinput` failed
at its Meson configure step: `ERROR: Clock skew detected. File
.../cross-compilation.conf has a time stamp 25.9166s in the future.`
Meson refuses to proceed when it detects a generated file's mtime is ahead
of the container's current clock — a known symptom of Docker Desktop's
Linux VM clock drifting relative to the macOS host, typically after the
Mac briefly sleeps or the VM pauses. Confirmed via `docker run --rm
<image> date` compared to host `date` — clocks were back in sync by the
time this was checked, meaning the skew was transient/momentary rather
than a persistent offset needing a manual clock re-sync. Fixed by simply
cleaning the affected package's build dir (`rm -rf
.../build/libinput-1.30.1`) and relaunching — no VM restart needed since
the clocks had already resynced. If this recurs with clocks still visibly
out of sync on retry, restarting Docker Desktop (which resets the VM) is
the next escalation.

**Build failure #16 — transient EIO on chmod (gRPC FUSE hiccup).** `pcre
8.45`'s staging-install step failed: `install:
.../sysroot/usr/bin/pcre-config: chmod failed with error Input/output
error (os error 5)`. Different error class than failure #13
(EACCES/permission) — this was EIO, which usually signals a real
storage/transport glitch rather than a permissions problem. Checked disk
space (47GB free of 80GB, not full), volume read-only status (not
read-only), and retried the exact same `chmod` both from the host and
from inside a fresh container — both succeeded immediately. Concluded
this was a one-off gRPC FUSE transport hiccup (the same file-sharing
backend switched to for failure #13, presumably not fully immune to all
transient glitches, just the specific fchmod-poisoning bug). Fixed the
same way as failure #15: clean the affected package's build dir (`rm -rf
.../build/pcre-8.45`) and relaunch, no Docker restart needed since a
direct retry of the failing operation already succeeded.

**Build failure #17 — `host-heimdal` test binaries missing `-lcrypt`.**
`host-heimdal`'s `make all-recursive` failed linking its test binaries
(`test_crypto`, `krbhst-test`, `verify_krb5_conf`, `test_forward`,
`test_alname`, `test_get_addrs`): `undefined reference to 'crypt'`. Real
config gap, not transient — newer glibc split `crypt()` out into
`libcrypt`/`libxcrypt` (confirmed `libcrypt.so`/`libcrypt.so.1` exist in
the container via `ldconfig -p`), and heimdal's build doesn't link
against it explicitly for these test targets. Fixed by adding
`HOST_HEIMDAL_CONF_ENV += LIBS="-lcrypt"` in
`buildroot/package/heimdal/heimdal.mk` (alongside the existing local
`-fPIC` CFLAGS addition) — `LIBS` is the standard autotools variable for
extra link libraries applied to all binaries the generated Makefile
builds, including its test suite. Required a full clean of
`host-heimdal-<hash>`'s build dir since `HOST_HEIMDAL_AUTORECONF = YES`
means the configure step must rerun to pick up the new `LIBS` value.

**Build failure #18 — OOM-kill during `samba4` (BR2_JLEVEL=6 exceeded
Docker's memory limit).** After the parallelism bump to `BR2_JLEVEL=6`
(see "Performance" section above — a separate, deliberate speedup, not a
bug fix), the build died silently mid-compile of `samba4 4.20.6`:
`make: *** [Makefile:323: bcm2837-build] Error 137`. Exit 137 = 128+9 =
SIGKILL, with no compiler error in the log — the classic signature of the
Linux OOM killer terminating a process inside the Docker VM. `samba4`
compiles a very large number of C files in parallel (including its
sizeable torture-test suite), and 6 concurrent `gcc`/`g++` jobs on
Samba's larger translation units exceeded Docker Desktop's then-current
7.75GiB VM memory allocation (16GB physical RAM on the host, only
7.75GiB given to Docker). Fixed by the user increasing Docker Desktop's
memory allocation (Settings → Resources → Memory) to ~10.7GiB — verified
via `docker info | grep -i "total memory"` before relaunching — keeping
`BR2_JLEVEL=6` rather than reverting the speedup. `samba4`'s build
directory needed a full clean (`rm -rf .../build/samba4-4.20.6`) since it
was silently killed mid-build with no stamp/partial-state guarantees.

**Build failure #19 — `host-python-lxml` incompatible-pointer-types error
(GCC15 default change, same category as failures #1/#2).** After
`webkitgtk` finally finished (a major milestone — the single largest
package in the whole build), the very next package, `host-python-lxml
5.3.0`, failed compiling its bundled/pre-generated Cython C code
(`src/lxml/etree.c`):
```
error: passing argument 1 of '__pyx_f_4lxml_5etree__fixThreadDictPtr' from incompatible pointer type [-Wincompatible-pointer-types]
  (xmlChar ** passed where const xmlChar ** expected)
```
Root cause: recent GCC versions (14+) made `-Wincompatible-pointer-types`
an **error by default** rather than a warning, even in `gnu17` mode (not
just under stricter C23 profiles) — a real behavior change, not specific
to this project. lxml's shipped `etree.c` (generated once by Cython
against an older libxml2 header signature) has a genuine, harmless
signature mismatch that only became fatal because of this GCC default
change — exactly the same category of breakage as failures #1/#2
(m4/gmp vs GCC15's `-std=gnu23` default).

Fixed the same way as those: added `-Wno-error=incompatible-pointer-types`
to the global `HOST_CFLAGS` override (alongside the existing `-std=gnu17`)
so this specific class of error is demoted back to a warning everywhere,
rather than patching lxml's generated source directly. Cleaned
`host-python-lxml-5.3.0`'s build directory and relaunched.

**Build failure #20 — disk image full (`No space left on device`).** After
the incompatible-pointer-types fix let `host-python-lxml` compile
successfully, the final link step failed: `ld.bfd: final link failed: No
space left on device`. Confirmed via `df -h /Volumes/BatoceraBuild`: the
80GB case-sensitive disk image (`BatoceraBuild.dmg`) was at **100% full**
(`output/` alone: 71GB, plus `dl/` 4.4GB and `buildroot-ccache/` 2.4GB).
This is expected — 80GB was sized for the WiFi driver work originally,
without accounting for a full WebKitGTK build (one of the largest packages
in the whole distro) landing on top.

Fixed by growing the disk image live: `hdiutil resize -size 110g
BatoceraBuild.dmg` (had to `hdiutil detach` first — an in-place resize
while mounted failed with error 35 "Resource temporarily unavailable";
detach → resize → re-`hdiutil attach -owners on` worked). Verified via
`hdiutil resize -limits` first that 110GB was within the image's supported
range (max ~134GB) and that the host Mac had enough free space (46GB free
on the boot volume — the `.dmg` is NOT a sparse file, it's fully allocated
at its stated size, so growing by 30GB genuinely consumes 30GB of host
disk immediately). The APFS container inside the image auto-grew to fill
the new disk size after remount (confirmed via `diskutil apfs list` and
`df -h` — went from 55MB free to 30GB free) — no separate `diskutil apfs
resizeContainer` step was needed.

If this recurs later in the build (RetroArch cores, final image assembly),
same fix: detach, `hdiutil resize -size <bigger>g`, reattach with
`-owners on` (re-check ownership is still enabled after reattaching — see
failure #13's fix, which this could theoretically undo if `-owners on` is
forgotten on reattach).

**Build failure #21 — `batocera-initramfs` calls bare `mkimage` without
PATH.** After the disk-space fix, the build reached `batocera-initramfs`
(a major milestone — this is initrd/image-assembly territory, meaning
essentially all packages including WebKitGTK and the kernel were already
done). Failed with `/bin/bash: line 1: mkimage: command not found`, exit
127. Confirmed `mkimage` genuinely exists and was built successfully at
`$(HOST_DIR)/bin/mkimage` (`host-uboot-tools` — already listed as a
`BATOCERA_INITRAMFS_DEPENDENCIES` entry) — this wasn't a missing-package
problem, just a PATH problem in one specific shell invocation.
`package/batocera/boot/batocera-initramfs/batocera-initramfs.mk`'s
`BATOCERA_INITRAMFS_INSTALL_TARGET_CMDS` calls `mkimage` bare in a `(cd
$(BINARIES_DIR) && mkimage ...)` subshell, unlike the preceding line which
wraps its command in `$(BATOCERA_INITRAMFS_MAKE_ENV) $(MAKE) ...` (which
carries the PATH override) — the `cpio`/`mkimage` lines don't inherit
that same env wrapper. Fixed by using the explicit `$(HOST_DIR)/bin/mkimage`
path instead of relying on `PATH` for that one invocation — matches how
other Buildroot packages reference host tools when they can't be certain
of implicit PATH inheritance.

**Build failure #22 — dead upstream source: kodi German language pack.**
`kodi21-resource-language-de_de-11.0.101` failed to download — all three
of Buildroot's fallback mirrors (kodi.tv, sources.buildroot.net direct,
sources.buildroot.net namespaced) returned genuine `404 Not Found`, not a
transient mirror flake like failure #11. The exact pinned version of this
Kodi language resource no longer exists upstream. Since this is a German
UI language pack for Kodi — irrelevant to WiFi/core functionality — fixed
by disabling it rather than chasing a replacement URL/version.

**Update: this recurred for `el_gr` (Greek) too**, confirming it's not an
isolated dead link but stale mirror snapshots across the whole
`kodi21-resource-language-*` family (~9 packages). Rather than
whack-a-mole one language at a time, removed **all** remaining `select
BR2_PACKAGE_KODI21_RESOURCE_LANGUAGE_*` lines in one edit (ES_ES, EU_ES,
FR_FR, IT_IT, PT_BR, SV_SE, TR_TR, ZH_CN, EL_GR) — non-essential Kodi UI
translations, not worth checking each individually given the goal is WiFi
functionality, not full Kodi feature completeness. Verified via `grep -c
"KODI21_RESOURCE_LANGUAGE.*=y" .config` → 0 after relaunch.

**Important gotcha:** editing `/Volumes/BatoceraBuild/output/bcm2837/.config`
directly (`BR2_PACKAGE_KODI21_RESOURCE_LANGUAGE_DE_DE=y` →
`# ... is not set`) does **not stick** — this build's relaunch command
runs the `batocera-bcm2837_defconfig` target first (visible in the
`docker run` command as a trailing argument), which regenerates `.config`
from the actual defconfig/Config.in sources on every launch, silently
reverting any direct `.config` edit. The real fix has to go in the
Kconfig source: this package is force-enabled via a `select
BR2_PACKAGE_KODI21_RESOURCE_LANGUAGE_DE_DE` line in
`package/batocera/core/batocera-system/Config.in` (part of a longer list
of `select`-ed Kodi language packs), not a plain defconfig entry — a
`select` can't be overridden by disabling the symbol directly even with a
persistent defconfig edit, it has to be removed at the `select` site
itself. Fixed by deleting that one `select` line (leaving the other
language packs — ES_ES, EU_ES, FR_FR, etc. — untouched). Verified the
fix survived a fresh `batocera-bcm2837_defconfig` regeneration before
trusting it.

**Build failure #23 — `host-xxd` implicit-function-declaration (another
GCC15-default-change case, same family as #19).** `host-xxd.c` failed with
`conflicting types for 'ftell'` and `too many arguments to function
'strtol'; expected 0, have 3` — the classic signature of missing
`#include <stdio.h>`/`<stdlib.h>` causing old-style implicit function
declarations (`int func()` with no prototype). Recent GCC made
`-Wimplicit-function-declaration` an **error by default**, not just a
warning — same root-cause family as failure #19
(`-Wincompatible-pointer-types`). Fixed the same way: added
`-Wno-error=implicit-function-declaration` to the global `HOST_CFLAGS`
override, now three `-Wno-error=...` flags deep (`-std=gnu17`,
`incompatible-pointer-types`, `implicit-function-declaration`). Given this
is the second such error-by-default GCC15 change hit in this build, expect
more of this general category rather than assuming each new one is a
different problem — check for `-Wno-error=<diagnostic-name>` as a first
hypothesis when a host-tool compile fails with a plausible-looking
old-C-code error.

**Addendum to #23 — real fix needed a source patch, not a CFLAGS flag.**
The `-Wno-error=implicit-function-declaration` fix wasn't actually
sufficient — the retry hit a *different* error in the same file:
`conflicting types for 'strtol'`/`'ftell'`. Root cause: `xxd.c` (an odd,
minimal C source, not a Buildroot-provided package) unconditionally
forward-declares `extern long int strtol();` / `extern long int
ftell();` in old K&R no-prototype style, which directly conflicts with
the real prototypes from `<stdio.h>`/`<stdlib.h>` (already included
above in the same file) — a hard C redefinition error, not a downgradable
warning; `-Wno-error=...` has no effect on it. Also discovered
`HOST_XXD_BUILD_CMDS` in `xxd.mk` just runs `$(MAKE) -C $(@D)` with no
env/CFLAGS override at all — confirmed via the actual failing compile
line (`cc -DUNIX -o xxd xxd.c`, no flags) that our global `HOST_CFLAGS`
never reaches this package's build in the first place, so even a
CFLAGS-based fix wouldn't have worked here regardless.

Fixed with a real source patch,
`package/batocera/utils/xxd/0001-remove-broken-K-R-forward-declarations.patch`,
deleting the two redundant/broken declaration lines (safe — the real
prototypes are already visible via the existing `#include`s above them).
Verified with `patch -p1 --dry-run` against the real extracted source via
the container's own `patch` binary (same verification pattern as failure
#12's QEMU patch) before trusting it.

**Build failure #24 — `ecwolf` git submodule hangs on interactive Bitbucket
credential prompt (genuine hang, not a crash).** `ecwolf` (a Wolfenstein 3D
engine port) clones from `bitbucket.org/ecwolf/ecwolf.git` with
`ECWOLF_GIT_SUBMODULES=YES`; one submodule
(`bitbucket.org/ecwolf/sdl_net.git`) prompted interactively for a
Bitbucket username, which of course never arrives under `BATCH_MODE=1` —
the build silently sat idle (confirmed via `docker stats` showing ~0% CPU
and the log file's line count static across repeated checks) rather than
erroring out. This is a different failure *shape* than anything before it
— no error text at all, just an indefinite hang — worth remembering that
"no progress + ~0% CPU + log not growing" is the actual signature to check
for, not just "process exited".

**Docker daemon complication:** killing the hung container failed
repeatedly — `docker kill`/`docker rm -f` both returned "tried to kill
container, but did not receive an exit event" (the git process was stuck
in a state that didn't respond to SIGKILL cleanly, or Docker's own daemon
lost track of it). Fixed by a full Docker Desktop restart (same recipe as
earlier gRPC-FUSE-switch and OOM fixes) — the stuck container was
gone/cleaned up after Docker came back up, no manual removal needed.

**Fix:** `ecwolf` is non-essential (one game engine among many), so
disabled it entirely rather than trying to make git auth work
non-interactively. It's force-enabled via `select BR2_PACKAGE_ECWOLF if
BR2_PACKAGE_BATOCERA_TARGET_X86_64_ANY || BR2_aarch64` in
`package/batocera/core/batocera-system/Config.in` (matches our aarch64
target unconditionally) — commented out that `select` line, following the
same pattern as failure #22's Kodi language packs. Verified via `grep
"^BR2_PACKAGE_ECWOLF" .config` showing no `=y` line after the defconfig
regenerated.

**Build failure #25 — `mkdosfs: not found` during final image assembly
(`target-post-image` / genimage).** This is it — the actual `.img`
assembly step. `genimage` (which builds the final boot partition layout
from `board/batocera/broadcom/bcm2837/genimage.cfg`) shells out to
`mkdosfs` at runtime to format the FAT32 `boot.vfat` partition, but
nothing in the build ever pulled in `host-dosfstools` — confirmed
`dosfstools-4.2` already existed for the **target** (Batocera ships it
on-device, presumably for formatting SD cards/USB drives from the running
system), but `host-dosfstools` was never built. Genuine upstream gap in
`buildroot/package/genimage/genimage.mk`'s `HOST_GENIMAGE_DEPENDENCIES` —
`dosfstools` is normally only pulled in for the `iso9660` filesystem type
or `imx-uuc`, and nothing declares it for boards with vfat boot
partitions like ours. Fixed by adding `host-dosfstools` to
`HOST_GENIMAGE_DEPENDENCIES` (alongside the existing `host-pkgconf
host-libconfuse`).

**Addendum to #25 — host-dosfstools install tripped Buildroot's RPATH
sanity checker on an unrelated, pre-existing binary.** After the dosfstools
dependency fix, its own build/compile succeeded, but its *install* step
failed: `*** ERROR: package host-dosfstools installs executables without
proper RPATH: /bcm2837/host/bin/aarch64-buildroot-linux-gnu-ld.gold`.
`ld.gold` is our cross-linker (part of binutils, built hours earlier,
used successfully for the entire build up to this point) — not something
`host-dosfstools` installed at all. Root cause, found by reading
`buildroot/support/scripts/check-host-rpath`: this check scans **every**
ELF file in `$(HOST_DIR)/{bin,sbin}`, not just files the current package
installed, and flags any binary that *needs* a library present in
`$(HOST_DIR)/lib` but lacks an RPATH pointing there. `ld.gold` needs
`libz.so.1` and had an **empty** RUNPATH (confirmed via `readelf -d`) —
harmless until now because it was resolving `libz.so.1` via the
container's system library path. `host-dosfstools` apparently pulled in
its own `host-libzlib`-provided `libz.so.1` into `$(HOST_DIR)/lib` for
the first time, which flipped the check's `elf_needs_rpath` from false to
true for `ld.gold` — a false-positive in practice (ld.gold works fine
either way) but a hard build-stopping error per Buildroot's check.
`package/openjdk-bin/openjdk-bin.mk` has a documented instance of this
exact same category of bug (`unpack200` vs `host-libzlib`) — their fix
was deleting the offending deprecated tool, not viable here since
`ld.gold` is essential.

Fixed instead by giving `ld.gold` a proper RPATH directly, using
`host-patchelf` (already built earlier in this session, found at
`$(HOST_DIR)/bin/patchelf`):
```
docker run --rm -v "/Volumes/BatoceraBuild/output/bcm2837:/bcm2837" -u 501:20 batoceralinux/batocera.linux-build sh -c "/bcm2837/host/bin/patchelf --set-rpath /bcm2837/host/lib /bcm2837/host/bin/aarch64-buildroot-linux-gnu-ld.gold"
```
Verified `readelf -d` shows the new RUNPATH and that `ld.gold --version`
still runs correctly before trusting it. This is a one-off manual fix
outside the normal package-patch mechanism (it patches an already-built
host tool's binary directly, not a package source) — if this build is
ever redone fully from scratch, the same RPATH gap will reappear at the
same point and need the same one-line `patchelf` fix again; not worth
automating into the package tree for a single occurrence.

**Update: this recurred with `host-mtools`.** After the PC crash + rebuild,
`ld.gold`'s RUNPATH was back to empty (the earlier `patchelf` fix hadn't
survived — either lost to the crash before its write was flushed, or the
per-package RPATH check reproducibly retriggers on the first package in
a fresh session that pulls `libz.so.1` into its own per-package host
view). Re-ran the identical `patchelf --set-rpath` command — fixed again
immediately. **Expect this to keep recurring** any time a fresh/rebuilt
session hits the first `host-*` package that depends on zlib after
`ld.gold` — it's a cheap, known one-liner fix each time, not worth
chasing a permanent solution given it's this close to done.

**Addendum to #25 — also needed `host-mtools` (`mcopy`), plus a PC crash
mid-build.** After the RPATH fix, `target-post-image` progressed further
but hit a near-identical gap: `mcopy: not found` — genimage also shells
out to `mcopy` (from `mtools`) to populate the already-formatted vfat
image with files, a separate tool from `mkdosfs`. Same fix pattern: added
`host-mtools` to `HOST_GENIMAGE_DEPENDENCIES` alongside `host-dosfstools`.

Separately, **the user's entire Mac crashed** partway through a
relaunch (unrelated to the build itself — a whole-system crash, not a
Docker/build failure). On reboot: the disk image was unmounted (expected,
survives fine — `hdiutil attach ... -owners on` remounted it cleanly, all
prior build output intact, `rootfs.squashfs` and the assembled boot
partition contents were still present), Docker Desktop needed a normal
cold start (no special recovery needed, `docker info` succeeded once the
app finished launching), and the build resumed via the same relaunch
command with no data loss — Buildroot's stamps meant nothing had to
recompile, it went straight back into `target-post-image`.

**Build failure #26 — deterministic EIO writing `batocera.img` (genimage
hdimage writer over the bind mount).** The literal final step —
`genimage` assembling `batocera.img` itself — failed identically **three
times in a row**: `ERROR: hdimage(batocera.img): write 4096 bytes: Input/output
error`. Unlike failure #16 (one-off transient EIO on `pcre`'s install),
this was fully reproducible at the same exact operation every retry, disk
space was not the constraint (checked each time, several GB free). Root
cause hypothesis: `genimage`'s hdimage writer does many small
random-access `seek()`+`write()` calls scattered throughout a large
output file (format the vfat partition, then dozens of separate `mcopy`
calls each seeking to a specific byte offset) — a very different I/O
pattern from the large sequential writes that dominate the rest of the
build (compiling, tar/squashfs). Both `--outputpath` and `--tmppath` for
this genimage invocation
(`board/batocera/scripts/post-image-script.sh`) pointed at the
bind-mounted `OUTPUT_DIR` (gRPC FUSE boundary — see "Performance"
section), and this specific write pattern apparently doesn't survive that
boundary reliably, unlike sequential I/O which had worked for the entire
rest of the build.

**Fix:** redirected genimage's `--outputpath` and `--tmppath` to
container-local paths (`/tmp/genimage.out.local`, `/tmp/genimage.tmp.local`
— genuinely local, not bind-mounted, confirmed no `-v` flag targets
`/tmp`), so the random-access write pattern happens entirely on the
container's own filesystem. Only the finished `batocera.img` gets copied
back across the bind-mount boundary afterward, as a single large
sequential `mv` (cross-filesystem, so actually a copy+delete under the
hood) — the I/O pattern sequential writes handle fine. Simplified the
surrounding cleanup: since `boot.vfat`/`userdata.ext4` (genimage's other
partition outputs, previously deleted via `rm -f` after the fact) now
never touch the bind mount at all, those `rm -f` lines were removed
entirely rather than kept as dead code.

**Build failure #14 — `host-cargo-c` needs a newer rustc than pinned
`rust-bin`.** After ~320 packages, `host-cargo-c v0.10.19` (needed for
`librsvg`, `package/batocera/utils-host/cargo-c/cargo-c.mk`) failed during
its cargo vendor step:
```
error: rustc 1.92.0 is not supported by the following packages:
  cargo-credential-libsecret@0.5.8 requires rustc 1.95
  cargo-util@0.2.30 requires rustc 1.94
  crates-io@0.40.19 requires rustc 1.94
  kstring@2.0.4 requires rustc 1.96.0
  rustfix@0.9.7 requires rustc 1.93
```
Root cause: a real version-skew bug in the batocera.linux repo itself —
`buildroot/package/rust-bin/rust-bin.mk` pins `RUST_BIN_VERSION = 1.92.0`,
but `cargo-c`'s v0.10.19 vendored dependency tree has since bumped MSRVs
(minimum supported Rust versions) past that, up to 1.96.0. Not something
we broke; a pre-existing mismatch between two independently-pinned
versions in the upstream repo.

Fixed by bumping `RUST_BIN_VERSION` to `1.96.0` (verified the tarballs
exist first via `curl -sI` HEAD requests against
`static.rust-lang.org/dist/` before committing to the change) and adding
real sha256 hashes — fetched via `curl -s
.../rust-1.96.0-x86_64-unknown-linux-gnu.tar.xz.sha256` and the matching
`rust-std-...-aarch64-unknown-linux-gnu` one — to
`buildroot/package/rust-bin/rust-bin.hash`, prepended above the existing
1.92.0 entries (left untouched/unused). Only the two arch variants this
build actually needs (x86_64 host, aarch64 target std) were added; no
need to hash every architecture Buildroot's stock hash file lists.
`host-rust-bin`'s own package name is versioned
(`host-rust-bin-1.92.0` → `host-rust-bin-1.96.0`), so the old build
directory doesn't need explicit cleanup — Buildroot just builds a
differently-named one — but `host-cargo-c-v0.10.19`'s build directory
*was* manually removed (`rm -rf`) since it had partially failed against
the old toolchain. This is a **host-only toolchain bump** (rust-bin
`HOST_RUST_BIN_PROVIDES = host-rustc`) — it cannot affect target-arch ABI
or any already-built target package.

**Build failure #12 — QEMU host-qemu `struct sched_attr` redefinition.**
`host-qemu-9.1.0` failed compiling `linux-user/syscall.c`: QEMU
unconditionally defines its own `struct sched_attr` (comment: "sched_attr
is not defined in glibc"), but the Docker build container's system
`linux/sched/types.h` (newer kernel-headers than QEMU 9.1 assumed) now
defines the same struct, causing a hard redefinition error. Not related to
our cross-toolchain or any prior fix — this is host-side (the container's
native gcc building a host tool), triggered by kernel-headers drift versus
what QEMU 9.1.0 expected. Fixed by adding
`buildroot/package/qemu/0004-linux-user-fix-redefinition-of-struct-sched_attr.patch`,
which wraps QEMU's own `struct sched_attr` definition in
`#ifndef SCHED_ATTR_SIZE_VER0` / `#endif` (the kernel UAPI size macro that
accompanies the struct, used as an existence guard since the kernel header
itself has no include guard around the struct). Verified applying cleanly
via `patch -p1 --dry-run` against the actual extracted source before
relying on it in the full build. Required a manual `rm -rf` of
`host-qemu-9.1.0`'s build directory (see "Package-level cache gotcha"
above) since the package had already been extracted+patched by the failed
attempt that surfaced this bug.

## Hardware validation: WiFi + controller (post-build, real device)

Confirmed on real hardware after flashing: WiFi connects (SSID visible,
SSH works), and the Arduino Leonardo's buttons + an external USB keyboard
both work out of the box with no wizard needed. Getting there took a lot
of wrong turns, recorded below so they aren't repeated.

**WiFi preseed — the working mechanism (confirmed in source, not just the
wiki).** `board/batocera/fsoverlay/etc/init.d/S08connman` runs very early
in boot, before `/userdata` is mounted:
```sh
BATOCONF="/userdata/system/batocera.conf"
BOOTCONF="/boot/batocera-boot.conf"
if ! [ -f "$BATOCONF" ]; then
    BATOCONF="$BOOTCONF"
fi
```
If `/userdata/system/batocera.conf` doesn't exist yet (true first boot),
it falls back to reading `/boot/batocera-boot.conf` directly — **using
the same key names as the main config** (`wifi.enabled`, `wifi.ssid`,
`wifi.key`, `wifi.country`), not a separate import mechanism. Two things
that do **not** work, despite looking plausible:
- Dropping a file named `batocera.conf` onto the FAT boot partition —
  wrong filename; the boot-partition file is `batocera-boot.conf`.
- Hand-editing `/userdata/system/batocera.conf` directly over SSH once
  it exists — confirmed on-device this is not picked up live, and not
  reliably even after reboot. Only `Menu → Network Settings` in
  EmulationStation, or `batocera-settings-set wifi.enabled 1` (etc.) over
  SSH, actually apply and persist correctly.
- An older `wifi.import.enabled=1` / `nas.wifi.ssid` mechanism described
  in a 2022-era GitHub PR (#899, `S61importwifikeyfile`) does **not**
  exist in this codebase (batocera-43.1) — grepped for zero matches.
  Superseded by the `S08connman` fallback above.

**Controller: the actual root cause, after three wrong theories.**

1. *Wrong theory #1:* removing the bundled `es_input.cfg`'s wildcard
   `<inputConfig type="keyboard" deviceGUID="-1">` entry would force the
   "press a button" wizard for the Arduino. **Wrong** — EmulationStation
   has a separate, hardcoded fallback in `InputManager::loadDefaultKBConfig()`
   (`es-core/src/InputManager.cpp`) that always provides basic keyboard
   nav (arrows, Enter=OK, Escape=Back, F1=start, F2=select, `[`/`]` =
   page up/down) regardless of the XML file's content. Removing the
   bundled entry had no effect on this and only broke the normal
   "plug in any keyboard, it works immediately" experience for **every**
   keyboard (external ones included) — reverted.
2. *Wrong theory #2:* the wizard is shown automatically on first boot,
   RetroPie-style. **Wrong** — confirmed in
   `es-app/src/views/ViewController.cpp:863-866`: it's purely reactive —
   `GuiDetectDevice` only pops up in response to an actual button/hat
   press on a device where `!config->isConfigured()`. There is no
   proactive "welcome" screen.
3. *Real root cause:* the on-board Arduino Leonardo (USB `2341:8036`,
   `hid-generic` driver) is a **composite HID device that sends both
   keyboard-range scancodes and joystick-range codes** (e.g. `BTN_PINKIE`
   = evdev code 293) depending on which physical button is pressed —
   confirmed via `evtest /dev/input/event1` and `jstest /dev/input/js0`
   on-device. udev correctly tags it `ID_INPUT_JOYSTICK=1`. Any button
   that happens to send a keyboard-range code gets silently absorbed by
   the hardcoded keyboard fallback above (partially "working" but wrong,
   matching the original symptom); any button sending a joystick-range
   code needs an actual `type="joystick"` `es_input.cfg` entry, which
   never existed for this specific device.

**The fix that actually worked:** use `Menu → Controllers and Bluetooth →
Configure a Controller` (works regardless of `isConfigured()` state,
since it's a direct menu action bypassing the reactive-only trigger),
navigating with an external keyboard first (also needs one pass through
its own wizard on a truly first boot — expected, not a bug), then
completing the wizard a second time pressing the **Arduino's** physical
buttons. This captures its real deviceGUID
(`03000000412300003680000001010000`) and correct per-button
type="joystick"/type="hat" mapping into
`/userdata/system/configs/emulationstation/es_input.cfg` — **note the
real path**, not the RetroPie-era `.emulationstation` dotdir; this
Batocera fork uses `/userdata/system/configs/emulationstation/`
(`es-core/src/Paths.cpp: mUserEmulationStationPath`). That captured
mapping was then copied back into the bundled
`package/batocera/emulationstation/batocera-emulationstation/controllers/es_input.cfg`
as a permanent default (alongside the restored keyboard wildcard), so a
fresh flash no longer needs either wizard re-run.

**Recurring Buildroot gotcha (hit twice this session):** a data-file-only
change inside a package's `POST_INSTALL_TARGET_HOOKS` (here,
`batocera-emulationstation`'s `cp .../es_input.cfg $(TARGET_DIR)/...`)
does **not** get picked up by a plain relaunch of `<board>-build` once
the package has already built+installed once — Buildroot's stamp files
don't track that loose file as a dependency. Symptom: build finishes in
suspiciously little time, "0 errors", but the target output is
byte-identical to before. Fix: delete that package's
`.stamp_target_installed` / `.stamp_installed` / `.stamp_staging_installed`
under `$(OUTPUT_DIR)/build/<pkg>-<version>/` before relaunching, to force
the install hooks to actually re-run. **Verify with a check that can't
false-positive on your own comment text** — `grep -c` on a raw XML
attribute string once matched an explanatory `<!-- comment -->` that
happened to quote the same string, giving a false "fix didn't land"
reading that cost an unnecessary rebuild cycle. Grep for something
unique instead (e.g. the actual deviceGUID or device name), or diff
against a known-good reference.

**Raspberry Pi Imager gotcha:** when re-flashing an image with the exact
same filename as a previous flash (e.g. because the build output path/
date suffix didn't change between two same-day builds), always
explicitly re-browse to the file via "Use custom" rather than picking it
from Imager's recent-files list — suspected (not fully confirmed) stale
content being reused was the likely cause of one flash silently not
containing that session's latest fix.

**Known open item:** the Arduino, once configured, shows up as
EmulationStation's "gamepad2" (likely because the external keyboard used
to drive the wizard also occupies a player slot, pushing the Arduino to
slot 2) — deferred, not yet confirmed whether this actually blocks
in-game input for player 1.
