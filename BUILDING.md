# Building the Circuit-Sword Batocera Image

This covers the actual day-to-day build workflow — environment setup,
the exact commands, and the non-obvious traps this project has hit.
CLAUDE.md covers the project's hard rules and architecture; this covers
how to actually run a build.

## Prerequisites

- **Docker Desktop**, running, with enough resources allocated for a
  Buildroot build (this project's builds have used the default
  `BR_DOCKER_VOLUMES=1` mode — see below — with several CPU cores and
  **~12-13GB RAM** allocated to Docker Desktop if your host has the
  headroom; a full build is CPU- and memory-hungry). **This raises the
  ceiling but does not, by itself, make webkitgtk's build step safe** —
  see "webkitgtk `njobs`" below, which was re-tested at 12.65GB on a
  16GB host and still OOM'd. Manual `njobs` capping for that one package
  remains necessary regardless of how much memory you give Docker, at
  least up to the range this project has actually tested.
- **~160GB free disk** for a first full build. Measured on an actual
  from-scratch run: `batocera-dl` (package source downloads) ~28GB,
  `batocera-ccache` (compiler cache) ~4.5GB — this grows further with
  reuse across builds, which is a good thing, not a leak — `batocera-
  output-$BOARD` (build tree + target/host trees + final images) ~118GB,
  Docker images ~2GB, for **~153GB Docker-side**, plus ~2GB once you
  extract the finished `.img.gz` to the host.
- **macOS only**: GNU `make` and GNU `findutils`, via Homebrew
  (`brew install make findutils`) — macOS's stock BSD `make`/`find`
  don't work for this Buildroot tree. `batocera-build/scripts/env.sh`
  prepends these to `PATH` automatically when sourced on Darwin.
- **A space-free path for `BATOCERA_SRC`.** See "The space-in-path trap"
  below — this is the single most important thing to get right before
  your first build.
- **Internet access to pull the `alpine` image** the first time you run
  `build-image.sh`/`build-kernel.sh` — used by `env.sh`'s
  `seed_docker_volumes` preflight step. Small (~13MB), one-time, but
  will fail an offline first build.

## The space-in-path trap

GNU Make's `$(realpath $(CURDIR))`, used inside Buildroot's own
top-level `Makefile` to compute `PROJECT_DIR`, silently truncates at the
first space in a path. If this project's own directory lives under a
space-containing path (as this repo's own default checkout location
does — `.../Circuit-Sword Batocera/...`), Buildroot's build breaks with
a confusing, unrelated-looking error deep inside its own Makefile (not a
clear "your path has a space" message).

`batocera-build/scripts/env.sh` now fails fast and explicitly if the
effective `BATOCERA_SRC`, `OUTPUT_DIR`, `DL_DIR`, or `CCACHE_DIR`
contains a space, instead of letting this corrupt a build silently.
**Before your first build**, set `BATOCERA_SRC` to an explicit,
space-free path — and, since this repo's own default checkout location
(`.../Circuit-Sword Batocera/...`) contains a space, and `OUTPUT_DIR`/
`DL_DIR`/`CCACHE_DIR` default to living under this repo too, override
their shared parent, `BATOCERA_BUILD_ROOT`, at the same time:

```bash
export BATOCERA_SRC=/path/without/spaces/batocera.linux
export BATOCERA_BUILD_ROOT=/path/without/spaces/batocera-build-root
```

Put this in your shell profile, or export it before running any
`batocera-build/scripts/*.sh` command, or before sourcing `env.sh`
directly.

**`LOG_FILE` is the one path deliberately left unguarded.** It's only
ever used as a quoted shell redirection target (`> "$LOG_FILE"`), which
handles spaces fine — there's no GNU Make `realpath` codepath involved,
so guarding it would be pure friction with no real risk behind it.

## First-time setup

```bash
export BATOCERA_SRC=/path/without/spaces/batocera.linux
batocera-build/scripts/setup-build-tree.sh
```

This clones upstream `batocera.linux` at the commit pinned in
`batocera-build/PINNED_COMMITS.txt`, applies
`batocera-build/patches/batocera-linux.patch` (this project's single
cumulative source-of-truth diff against upstream) plus
`batocera-build/patches/buildroot.patch` (inside the `buildroot`
submodule) and two loose per-file patches, and copies in
`batocera-build/overlay/` files. Safe to re-run — it resets to the
pinned commit and re-applies from scratch each time, so a stray local
edit inside `$BATOCERA_SRC` never silently persists across a re-run
(see the warning below).

**Do not hand-edit files inside `$BATOCERA_SRC` expecting them to
survive.** `setup-build-tree.sh` runs `git clean -fdx` + `git checkout --`
there on every run. To make a real change: edit the checkout, then
capture it back into this project's version-controlled patch file:

```bash
cd "$BATOCERA_SRC"
# ... make your change, commit it locally in this checkout ...
git diff --submodule=diff --binary <pinned-commit-from-PINNED_COMMITS.txt> HEAD -- . ':!buildroot' \
  > /path/to/this/repo/batocera-build/patches/batocera-linux.patch
```

This is exactly the workflow every feature plan in this project's
history has used — the patch file, not the checkout, is what's actually
committed and shared.

**`--binary` is required, and its position matters.** It must come
*before* the `--` pathspec separator, not after — `git diff ... --
. ':!buildroot' --binary` silently treats `--binary` as a literal
pathspec argument instead of a flag, and `git diff` degrades to writing
a useless `Binary files ... differ` stub with no actual content and no
error. Task 7's end-to-end validation build hit exactly this: an earlier
patch regeneration had (accidentally) run the flag-less/misplaced form,
so `batocera-linux.patch`'s entry for
`circuitsword-quickmenu/fonts/Cabin-Regular.ttf` (a `.ttf`, added via
this same workflow) carried no binary content at all.
`setup-build-tree.sh` then failed outright on a fresh checkout with
`error: cannot apply binary patch ... without full index line` — this
doesn't surface until someone runs a build from a truly clean
`BATOCERA_SRC`, since an already-populated checkout still has the file
on disk from before. Any future patch regeneration that touches a
binary asset (fonts, images, prebuilt binaries) must use the
`--binary` form above, correctly positioned, or it will silently
reproduce this exact failure.

## Building

Default mode (`BR_DOCKER_VOLUMES=1`, the default — see `env.sh`'s own
comments for why): no case-sensitive disk image needed, the build lives
entirely inside Docker Desktop's own Linux VM as named volumes. The
`build-image.sh`/`build-kernel.sh` scripts create and prepare these
volumes for you automatically on first use (including a permission fix
Docker itself needs — see `env.sh`'s `seed_docker_volumes`) — nothing
manual required here, just be aware it's happening the first time you
run either script and it takes a few seconds.

```bash
export BATOCERA_SRC=/path/without/spaces/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"  # macOS only
source batocera-build/scripts/env.sh  # for $MAKE_OPTS -- see "A known rough edge" below
# ^ run from this repo's root -- env.sh sets -euo pipefail and can exit
#   your shell outright if BATOCERA_SRC contains a space (it checks for
#   exactly that, see "The space-in-path trap" above); a plain-BATOCERA_SRC
#   example like this one won't hit it, but keep that in mind before
#   sourcing it into a shell you care about keeping open.

cd "$BATOCERA_SRC"
make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES=1 \
     O=/path/to/output/bcm2837 \
     BR2_EXTERNAL="$BATOCERA_SRC" \
     DL_DIR=/path/to/output/dl \
     BATCH_MODE=1 \
     bcm2837-build
```

**If you're invoking `make` directly like this instead of through
`build-image.sh`, you're also on your own for `BR2_JLEVEL`** (see
"Controlling build parallelism" below) and for the Docker named-volume
permission fix (see `env.sh`'s `seed_docker_volumes`, which you'd need
to call yourself, or just run `build-image.sh` instead).

This is a full image build — expect it to take a long time on a cold
cache (multiple hours), faster on a warm one. It logs to stdout; for a
long-running build you likely want to background it and log to a file
(`... bcm2837-build > build.log 2>&1 &`), matching what
`batocera-build/scripts/build-image.sh` does (in practice, prefer
running that script directly — it sources `env.sh` for you, computes
`OUTPUT_DIR`/`DL_DIR` consistently, backgrounds the build with `nohup`,
and prints where the finished image will land):

```bash
batocera-build/scripts/build-image.sh
```

Once finished, the image lands **inside the Docker named volume**
(`batocera-output-bcm2837`), not directly on the host filesystem — named
volumes aren't Finder-browsable. Extract it:

```bash
batocera-build/scripts/extract-artifacts.sh
```

This copies the built image(s) out of the `batocera-output-bcm2837`
Docker volume to `output/images/` on the host (or a destination you pass
as its first argument).

### Rebuilding a single package after a source edit — Hard Rule #7

**A full `bcm2837-build` does NOT automatically pick up edited source
files in an already-built package.** Buildroot's incremental build only
reacts to a package's `.stamp_built` being absent — once a package has
been built once in the persistent Docker volume, editing its source and
re-running a full build silently ships the STALE version.

Any package whose source you've edited must be explicitly force-refreshed
first:

```bash
make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES=1 O=/path/to/output/bcm2837 BR2_EXTERNAL="$BATOCERA_SRC" \
     DL_DIR=/path/to/output/dl PKG=<package-name>-rebuild bcm2837-pkg
```

**There is no dedicated script for this in `batocera-build/scripts/` —
this is a gap, not an oversight to route around.** An audit of all seven
scripts in that directory (`build-all.sh`, `build-image.sh`,
`build-kernel.sh`, `build-wifi.sh`, `setup-disk-image.sh`,
`extract-artifacts.sh`, `setup-build-tree.sh`) confirmed none of them
wraps this workflow. Every prior feature plan in this project's history
that needed to force-refresh a single already-built package ran the
`make ... PKG=<name>-rebuild bcm2837-pkg` command above directly by
hand, not through any script — that is the intended, current way to do
this. Substitute the same `O=`, `BR2_EXTERNAL=`, and `DL_DIR=` values
`env.sh` would compute (or just `source batocera-build/scripts/env.sh`
first and use `$OUTPUT_DIR/$BOARD`, `$BATOCERA_SRC`, `$DL_DIR` in the
command above) so this run shares state with the rest of your build.

Which variant to use depends on the package:

- **Plain-copy/config-file packages** (e.g. a Python daemon script, a
  shell script installed via `INSTALL_TARGET_CMDS`): `PKG=<name>-reinstall`
  is enough — it re-runs only the install step, not a full recompile.
- **Compiled packages** (C/kernel modules): `PKG=<name>-rebuild`.
- **Patch-based packages** — a package whose own directory contains
  `.patch` files applied by Buildroot before the build (e.g.
  `batocera-emulationstation`, this project's first-ever EmulationStation
  source patch): a plain `-rebuild` reuses the already-extracted (and
  therefore NOT re-patched) source tree. Adding or changing a `.patch`
  file requires `PKG=<name>-dirclean` before the next build, or the new
  patch content silently never applies:

  ```bash
  make MAKE_OPTS="$MAKE_OPTS" BR_DOCKER_VOLUMES=1 O=/path/to/output/bcm2837 BR2_EXTERNAL="$BATOCERA_SRC" \
       DL_DIR=/path/to/output/dl PKG=<package-name>-dirclean bcm2837-pkg
  # then a normal full build or a plain PKG=<name> bcm2837-pkg rebuild
  ```

**Never trust a clean build log alone as proof the fix landed.** Verify
directly against the built artifact inside the Docker volume:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  sh -c "grep -n '<something-you-just-added>' /bcm2837/target/usr/bin/<the-binary-or-script>"
```

### A known rough edge — root cause found and fixed

**Root cause (confirmed via Task 7's end-to-end build):**
`build-image.sh`'s and `build-kernel.sh`'s `make` invocations were never
actually applying `env.sh`'s `$MAKE_OPTS` (the `HOST_CFLAGS`/
`HOST_CXXFLAGS` needed to work around GCC 15's C23-by-default mode
breaking older host tools — reproduced live: `host-yasm` fails to
compile with "cannot use keyword 'false' as enumeration constant"
without them). The reason: `docker.mk`'s Docker-wrapped recipe is
`$(RUN_DOCKER) make $(MAKE_OPTS) O=/$* BR2_EXTERNAL=/build -C
/build/buildroot` — it only forwards the *outer Makefile's own*
`$(MAKE_OPTS)` make-variable into the container, not arbitrary shell
environment variables. This means `env $MAKE_OPTS make ...` (the form
`build-kernel.sh` used for its `linux-reconfigure`/`linux-rebuild`
calls) doesn't work either — env-var injection isn't how this recipe
consumes the value.

**Fix**: both scripts now pass `MAKE_OPTS="$MAKE_OPTS"` as a `make`
**command-line variable**, which does populate the outer Makefile's own
`$(MAKE_OPTS)` and threads through correctly. This is no longer a
workaround you need to remember — it's applied automatically.

**Note**: `BR2_JLEVEL` is deliberately *not* part of `$MAKE_OPTS` — see
the next section for why and how it's actually controlled.

### Controlling build parallelism

`BR2_JLEVEL` does not work via `MAKE_OPTS`, even with the fix above.
`batocera.linux`'s own top-level Makefile (not Buildroot's — a
different, wrapping Makefile) only turns its internal `$(MAKE_OPTS)`
into `-j$(MAKE_JLEVEL)` when `PARALLEL_BUILD` is set — and setting
`PARALLEL_BUILD=1` *also* unconditionally force-enables
`BR2_PER_PACKAGE_DIRECTORIES=y`, a different Buildroot feature (isolated
per-package host-tool directories) that breaks resuming an
already-partially-built output tree — reproduced live: a partial build
switched to `PARALLEL_BUILD=1` failed with `cmake: No such file or
directory`, because host tools built under the old shared-directory
layout are missing from the new per-package paths. This project does
not set `PARALLEL_BUILD=1` for this reason.

(If you ever do experiment with `PARALLEL_BUILD=1`: passing
`MAKE_OPTS="$MAKE_OPTS"` on the command line, as the scripts now do,
overrides the `-j`/`-l` flags that mode's own Makefile logic would
otherwise append to `MAKE_OPTS` — harmless as long as you're not using
`PARALLEL_BUILD=1`, since it's not set here, but worth knowing if you
go there.)

**What actually works**: a `batocera.mk` file at `$BATOCERA_SRC`'s root
containing `$(call add-defconfig,BR2_JLEVEL=<N>)`. The top-level
Makefile does `-include $(LOCAL_MK)` early (`LOCAL_MK` defaults to
`$(PROJECT_DIR)/batocera.mk`), and `add-defconfig` writes straight into
Buildroot's `.config`, controlling every package's own build parallelism
independent of `PARALLEL_BUILD`.

`build-image.sh` and `build-kernel.sh` now regenerate this file
automatically on every run (`env.sh`'s `sync_batocera_mk`, from the
`$BR2_JLEVEL` env var, defaulting to `2`) — this has to happen on every
run because `setup-build-tree.sh`'s `git clean -fdx` wipes a
hand-created `batocera.mk` on each re-run of that script. If you're
invoking `make ... bcm2837-build` directly instead of through the
scripts (per the manual example earlier in this section), you need to
create/export this yourself.

**Exception**: webkitgtk ignores `BR2_JLEVEL` entirely — see below.

### webkitgtk `njobs` — a host-editable knob, not a patch

`buildroot/package/webkitgtk/webkitgtk.mk` computes its own `-j` value
for that one package via a memory-based formula (`njobs :=
min(total_memory_kb/4 + 1, cpu_threads)`), ignoring `BR2_JLEVEL`/
`MAKE_JLEVEL` completely. On this project's first validation build host
(~9.7GB allocated to Docker Desktop), this formula's own self-computed
value wasn't safe in practice — it took repeated OOM crashes on
`GeneratedSerializers.cpp` and other large WebKit translation units
before manually hardcoding `njobs := 1` for that run.

**Corrected 2026-08-22 — raising Docker's memory allocation does NOT
reliably fix this.** An earlier version of this document speculated
that ~12-13GB would let the formula self-compute a safe, higher value.
That was re-tested directly: at 12.65GB allocated (out of a 16GB host —
the most headroom this project's hardware has to give without starving
macOS itself), the formula computed `njobs=4`, and webkitgtk still
OOM'd — `Killed signal terminated program cc1plus` partway through
`WebCore`'s unified sources, which are individually heavy enough that 4
of them in parallel exceeded 12.65GB. Pushing Docker's allocation
meaningfully higher than that isn't realistic on a 16GB host. **Treat
the formula as unsafe on typical contributor hardware and always
hardcode `njobs := 1` for this package, regardless of Docker memory** —
this was confirmed to build the rest of the way through cleanly once
capped.

**This is deliberately left as a manual, host-editable knob — not
committed as a permanent patch**, since a contributor with genuinely
large amounts of host RAM to spare (32GB+ hosts giving Docker 20GB+,
untested by this project) may be able to safely raise it above `1`.
Nobody should assume that without testing it themselves first, though —
`njobs := 1` is the only value this project has actually confirmed safe.

If you hit an OOM during the webkitgtk build step: edit
`$BATOCERA_SRC/buildroot/package/webkitgtk/webkitgtk.mk` directly and
hardcode `njobs := 1` (or another value you've verified is safe on your
own hardware) for that run. This edit does not need to (and should not)
be captured in `batocera-linux.patch`/`buildroot.patch` —
`setup-build-tree.sh` regenerates this file fresh from upstream on every
run, so a local edit here is exactly as temporary as it needs to be. If
you already have a partially-built webkitgtk from a crashed attempt,
`PKG=webkitgtk-dirclean` (see "Rebuilding a single package" above)
before resuming, or the stale build directory won't pick up the edit.

Because webkitgtk protects itself this way regardless of the outer
`BR2_JLEVEL`, there's no need to wait for it to finish before running
the rest of the build at a higher `BR2_JLEVEL` — see the speedup note in
Timing below.

## Timing

Real numbers from Task 7's end-to-end validation build: a genuine
cold-cache full image build, run to completion and verified (checksum
matched, valid MBR boot signature, `gzip -t` clean). "Cold" confirmed via
Downloading/Extracting log lines for glibc, gcc, and ffmpeg near the
start of the run, with no pre-existing package stamps — the run followed
a Docker Desktop factory reset (recovering from an unrelated host-disk-
full incident) that wiped all prior build state, so this is a true
from-scratch timing, not a warm-cache best case.

**Machine**: Apple Silicon Mac, 8 CPU cores, Docker Desktop allocated
9.7GB of the host's 16GB total RAM.

- **Phase 1** (`BR2_JLEVEL=1`): 2026-08-15 09:03 → 2026-08-16 16:30,
  **≈31h 26m**.
- **Phase 2** (`BR2_JLEVEL=2`, switched partway through): 2026-08-16
  16:30 → 17:24, **≈54m**.
- **Total: ≈32h 20m** continuous compute wall-clock for a full cold
  build on this hardware/settings combination.

**Biggest single time sink**: an unbroken ~13.7h stretch covering
webkitgtk (single-threaded by its own hardcoded `njobs`) immediately
followed by host-clang/LLVM (single-threaded only because the *whole*
build was still at outer `BR2_JLEVEL=1` at that point) — this one
stretch alone was ~44% of the entire Phase 1 duration.

**Speedup recommendation for a future cold build, evidence-backed by a
second validation run (see below):**

**Set `BR2_JLEVEL=2`+ from the very start**, not partway through as this
run did (that mid-build switch was ad-hoc caution, not a required
sequencing) — webkitgtk protects itself independent of the outer
setting (always hardcode its `njobs`, see below), so there's no reason
to wait. This alone would have cut into the Clang portion of the 13.7h
stretch above, and was confirmed to hold for a full run in the second
validation run below.

(An earlier version of this section also recommended raising Docker
Desktop's memory allocation as a second lever, on the theory that it
would let webkitgtk's own formula self-compute a safe higher `njobs`.
That was tested directly and did not hold up — see "webkitgtk `njobs`"
above and the second validation run below. Memory allocation still
matters for the build in general, just not as a webkitgtk-specific
speedup.)

A future contributor following this document (all four bugs above
already fixed/documented) should expect something close to this ~32h
figure, likely less with both recommendations applied — and none of the
additional debugging/interruption time this session spent finding these
bugs in the first place. Any rebuild after the first is dramatically
faster via ccache reuse for anything untouched.

### Second validation run (2026-08-20 to 2026-08-22) — speedup tweaks tested

A second genuine cold-cache build (fresh, empty `batocera-dl`/
`batocera-ccache`/`batocera-output-$BOARD` volumes) was run specifically
to test the two speedup recommendations above: `BR2_JLEVEL=2` from the
very start, and raising Docker Desktop's memory to ~12.65GB. **Its
total wall-clock time is not a usable comparison figure** — the run hit
four unrelated interruptions, none caused by this project's own code:

- Two dead upstream download mirrors (`libtool` on `ftpmirror.gnu.org`,
  `pm-utils` on `pm-utils.freedesktop.org`) — both worked around by
  downloading the exact same tarball from a different host and
  verifying it against Buildroot's own recorded hash before manually
  seeding it into the `batocera-dl` volume.
- A transient git-over-HTTPS TLS failure cloning a `libretro-picodrive`
  submodule (`miniaudio`) — resolved by simply retrying once network
  connectivity was reconfirmed.
- A **multi-day upstream crates.io incident**: the `arrayref` crate
  (a transitive dependency of `blake3`, needed by `host-cargo-c` for
  `librsvg`) had every version matching `blake3`'s own version
  requirement yanked, with no advisory explaining why, coinciding with
  the crate maintainer's GitHub account being deleted. This fully
  blocked the build (bumping `host-cargo-c`'s own pinned version did
  not help — every recent `blake3` release requires the same yanked
  range) until the yanks were reverted upstream around a day later, at
  which point the build resumed and passed that step immediately with
  no changes on this project's side.

**What this run did confirm, on the segments actually measured:**

- **`BR2_JLEVEL=2` from the start**: held for 98%+ of all `make`
  invocations throughout, confirmed via both the generated `.config`
  and direct log inspection — this part of the speedup recommendation
  works exactly as documented.
- **The Docker-memory recommendation does not hold up** — see the
  corrected "webkitgtk `njobs`" section above. `njobs := 1` had to be
  reinstated for that one package even at 12.65GB.
- **One clean, uninterrupted stretch**: once webkitgtk was capped to
  `njobs := 1` and its partial build dircleaned, the build ran for
  **~11 hours continuously** (2026-08-21 14:49 → 2026-08-22 01:50, no
  crashes, no manual intervention) and got through the remainder of
  webkitgtk, `qt6declarative`, and a large batch of emulator cores
  including `mame` — all at `BR2_JLEVEL=2` for every package except
  webkitgtk itself. This is consistent with the first run's finding
  that `BR2_JLEVEL=2`-from-the-start meaningfully outperforms starting
  at `BR2_JLEVEL=1`.
- The build did complete successfully end-to-end once resumed past the
  external blockers, producing a verified `batocera-bcm2837-43.1-
  20260822.img.gz` (checksums and `gzip -t` confirmed).

**Practical takeaway**: use `BR2_JLEVEL=2`+ from the start (confirmed
real speedup); don't rely on Docker memory alone to make webkitgtk
safe (confirmed it isn't, at least up to 12.65GB on a 16GB host) —
always cap `njobs := 1` for that package instead.
