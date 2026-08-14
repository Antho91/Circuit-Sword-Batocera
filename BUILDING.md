# Building the Circuit-Sword Batocera Image

This covers the actual day-to-day build workflow — environment setup,
the exact commands, and the non-obvious traps this project has hit.
CLAUDE.md covers the project's hard rules and architecture; this covers
how to actually run a build.

## Prerequisites

- **Docker Desktop**, running, with enough resources allocated for a
  Buildroot build (this project's builds have used the default
  `BR_DOCKER_VOLUMES=1` mode — see below — with several CPU cores and
  10GB+ RAM allocated to Docker Desktop; a full build is CPU- and
  memory-hungry).
- **macOS only**: GNU `make` and GNU `findutils`, via Homebrew
  (`brew install make findutils`) — macOS's stock BSD `make`/`find`
  don't work for this Buildroot tree. `batocera-build/scripts/env.sh`
  prepends these to `PATH` automatically when sourced on Darwin.
- **A space-free path for `BATOCERA_SRC`.** See "The space-in-path trap"
  below — this is the single most important thing to get right before
  your first build.

## The space-in-path trap

GNU Make's `$(realpath $(CURDIR))`, used inside Buildroot's own
top-level `Makefile` to compute `PROJECT_DIR`, silently truncates at the
first space in a path. If this project's own directory lives under a
space-containing path (as this repo's own default checkout location
does — `.../Circuit-Sword Batocera/...`), Buildroot's build breaks with
a confusing, unrelated-looking error deep inside its own Makefile (not a
clear "your path has a space" message).

`batocera-build/scripts/env.sh` now fails fast and explicitly if the
effective `BATOCERA_SRC` contains a space, instead of letting this
corrupt a build silently. **Before your first build**, set
`BATOCERA_SRC` to an explicit, space-free path:

```bash
export BATOCERA_SRC=/path/without/spaces/batocera.linux
```

Put this in your shell profile, or export it before running any
`batocera-build/scripts/*.sh` command, or before sourcing `env.sh`
directly.

**Note — this check covers `BATOCERA_SRC` only.** `env.sh` also derives
`OUTPUT_DIR`, `DL_DIR`, `CCACHE_DIR`, and `LOG_FILE` from `REPO_ROOT`
(this repo's own checkout location), not from `BATOCERA_SRC` — so if
this repo itself lives under a space-containing path, those four still
silently retain the space even after the `BATOCERA_SRC` check passes.
In practice this hasn't caused a problem this project has hit: it was
specifically `BATOCERA_SRC` feeding Buildroot's own `PROJECT_DIR`
computation that was the landmine, and none of `OUTPUT_DIR`/`DL_DIR`/
`CCACHE_DIR`/`LOG_FILE` feed that same GNU Make `realpath` codepath. But
don't assume every path in the build is now space-safe just because a
build has succeeded — only `BATOCERA_SRC` is actively guarded.

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
git diff --submodule=diff <pinned-commit-from-PINNED_COMMITS.txt> HEAD -- . ':!buildroot' \
  > /path/to/this/repo/batocera-build/patches/batocera-linux.patch
```

This is exactly the workflow every feature plan in this project's
history has used — the patch file, not the checkout, is what's actually
committed and shared.

## Building

Default mode (`BR_DOCKER_VOLUMES=1`, the default — see `env.sh`'s own
comments for why): no case-sensitive disk image needed, the build lives
entirely inside Docker Desktop's own Linux VM as named volumes.

```bash
export BATOCERA_SRC=/path/without/spaces/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"  # macOS only

cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 \
     O=/path/to/output/bcm2837 \
     BR2_EXTERNAL="$BATOCERA_SRC" \
     DL_DIR=/path/to/output/dl \
     BATCH_MODE=1 \
     bcm2837-build
```

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
make BR_DOCKER_VOLUMES=1 O=/path/to/output/bcm2837 BR2_EXTERNAL="$BATOCERA_SRC" \
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
  make BR_DOCKER_VOLUMES=1 O=/path/to/output/bcm2837 BR2_EXTERNAL="$BATOCERA_SRC" \
       DL_DIR=/path/to/output/dl PKG=<package-name>-dirclean bcm2837-pkg
  # then a normal full build or a plain PKG=<name> bcm2837-pkg rebuild
  ```

**Never trust a clean build log alone as proof the fix landed.** Verify
directly against the built artifact inside the Docker volume:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  sh -c "grep -n '<something-you-just-added>' /bcm2837/target/usr/bin/<the-binary-or-script>"
```

### A known rough edge

Confirmed (by a read-only audit of `build-image.sh` and
`build-kernel.sh`, cross-checked against this project's build history):
`build-image.sh`'s and `build-kernel.sh`'s final scripted full-build
commands (the `nohup make ... "$BOARD-build"` line each runs) do not
apply `env.sh`'s own `$MAKE_OPTS` (host-compiler flags meant to work
around GCC 15 host-package build failures), even though `env.sh`'s
comment describes them as necessary. `build-kernel.sh` is internally
inconsistent about this: its own earlier synchronous calls
(`linux-reconfigure`, `linux-rebuild`) correctly prefix `env $MAKE_OPTS`,
but its final full-build call — identical in shape to `build-image.sh`'s
— omits it. Whether this is a latent bug (some host package could hit
the GCC-15 failure mode on a future run) or `env.sh`'s comment is simply
stale (the workaround may already be baked into
`batocera-linux.patch`/`buildroot.patch` at the Makefile level, making
`$MAKE_OPTS` vestigial at invocation time) is unresolved — both are
plausible and this hasn't been root-caused.

In practice, every build this project has actually run succeeded
without `$MAKE_OPTS` applied at this step. If a build fails on a
host-package compile error mentioning `incompatible-pointer-types` or
`implicit-function-declaration`, try prefixing the `make` command with
`env $MAKE_OPTS` (after sourcing `env.sh`, which defines it).

## Timing

(Record real numbers here after Task 7's end-to-end run — filled in as
part of that task, not invented ahead of time.)
