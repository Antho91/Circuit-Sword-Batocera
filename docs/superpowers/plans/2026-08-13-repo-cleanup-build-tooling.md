# Repo Cleanup & Build Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the main project directory into something a new contributor can actually clone and build from: clean it up, document the real build workflow, guard against the space-in-path landmine, and prove the whole regenerable-build-tree mechanism actually works end to end.

**Architecture:** File-tree cleanup and reorganization first (deletions, moves, cross-reference fixes), then a defensive fix to `env.sh`, a bounded read-only audit of the existing build scripts, a new `BUILDING.md`, THEN `git init` (the main directory has never been a git repo — this is genuinely the first commit), and finally a real end-to-end build to prove the documented path works.

**Tech Stack:** Bash (env.sh fix, audit), Markdown (BUILDING.md), git, Buildroot/Docker build pipeline (validation only, no new build-system code).

## Global Constraints

- Working directory for this entire plan is the MAIN project directory, `/Users/bas/Circuit-Sword Batocera` — NOT `/Users/bas/batocera-build-wifi/batocera.linux` (every other plan this project touched that tree; this plan does not).
- The main project directory has **no `.git` today**. There is no BASE commit, no existing history, and this plan's own patch-capture convention (used by every other plan this session) does **not** apply here — this plan produces no `batocera-linux.patch` changes.
- Task order matters: all file-tree/content changes (deletions, moves, env.sh fix, BUILDING.md) land **before** `git init`, so the first commit is already the clean, organized state.
- Out of scope, explicitly: creating a git remote (user handles this separately later), moving the main project directory to a space-free path, auditing the runtime C/Python/firmware codebase, building a CI pipeline.
- Any finding requiring a real judgment call (not a trivial/obviously-safe fix) gets flagged in a task's report, never silently changed.

---

### Task 1: Delete stray large files and dead `circuit-sword-external/`

**Files:**
- Delete: `/Users/bas/Circuit-Sword Batocera/batocera-bcm2837-43.1-20260530.img`
- Delete: `/Users/bas/Circuit-Sword Batocera/batocera-bcm2837-43.1-20260530.img.gz`
- Delete: `/Users/bas/Circuit-Sword Batocera/circuit-sword-external/` (entire directory)
- Modify: `/Users/bas/Circuit-Sword Batocera/.gitignore`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: a main directory with no stray multi-GB image files, no dead `circuit-sword-external/` tree, and a `.gitignore` with a `.DS_Store` entry — later tasks (Task 6's `git init`) rely on this being done first.

- [ ] **Step 1: Record before/after disk usage**

```bash
du -sh "/Users/bas/Circuit-Sword Batocera"
```

Note the output — you'll report the after-value too, for a concrete "space reclaimed" figure.

- [ ] **Step 2: Delete the stray image files**

```bash
rm "/Users/bas/Circuit-Sword Batocera/batocera-bcm2837-43.1-20260530.img"
rm "/Users/bas/Circuit-Sword Batocera/batocera-bcm2837-43.1-20260530.img.gz"
```

- [ ] **Step 3: Delete `circuit-sword-external/`**

```bash
rm -rf "/Users/bas/Circuit-Sword Batocera/circuit-sword-external"
```

This is a stub br2-external tree from an abandoned Phase-1 design (its `package/` directory contains only a placeholder README — confirmed dead; see `docs/superpowers/specs/2026-08-13-repo-cleanup-build-tooling-design.md` Context section). The real mechanism is `batocera-build/patches/batocera-linux.patch` applied by `setup-build-tree.sh`.

- [ ] **Step 4: Delete stray `.DS_Store` files and update `.gitignore`**

```bash
find "/Users/bas/Circuit-Sword Batocera" -name ".DS_Store" -delete
```

Read the current `.gitignore` first:

```
# Batocera build tree: regenerable via batocera-build/scripts/setup-build-tree.sh
# (a full git clone of batocera-linux/batocera.linux + submodule -- large,
# and the patches/ + overlay/ directories are the actual source of truth).
batocera-build/build/
batocera-build/*.dmg

# Build output/downloads/ccache: regenerable, multi-hundred-GB working set.
/output/

# Build logs.
docs/superpowers/plans/*.log

# Never commit filled-in WiFi credentials.
batocera.conf
```

No existing pattern covers `.DS_Store`. Append this section at the end:

```

# macOS Finder metadata.
.DS_Store
```

- [ ] **Step 5: Verify**

```bash
ls "/Users/bas/Circuit-Sword Batocera/batocera-bcm2837-43.1-20260530.img" 2>&1  # expect: No such file or directory
ls "/Users/bas/Circuit-Sword Batocera/circuit-sword-external" 2>&1              # expect: No such file or directory
find "/Users/bas/Circuit-Sword Batocera" -name ".DS_Store"                       # expect: no output
du -sh "/Users/bas/Circuit-Sword Batocera"                                       # record the new (smaller) size
```

The `.DS_Store` `.gitignore` entry itself can't be verified with `git check-ignore` yet — there's no `.git` in this directory until Task 6. Note in the report that this specific check is deferred to Task 6's verification, not skipped.

---

### Task 2: Reorganize `docs/superpowers/plans/`

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/` (and move files into it)
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/reference/` (and move files into it)
- Delete: 9 `wifi-build*.log` files
- Modify: any file found by Step 4's grep to reference an old path

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: a reorganized `docs/superpowers/plans/` with no stray top-level FINDINGS/log/reference files — Task 6's `git init` commits this final state.

- [ ] **Step 1: Create the two new subdirectories**

```bash
mkdir -p "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings"
mkdir -p "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/reference"
```

- [ ] **Step 2: Move the FINDINGS files**

This exact list, confirmed via a fresh `ls` of the directory:

```bash
cd "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans"
mv PHASE0-FINDINGS.md \
   PHASE3-HARDWARE-DAEMON-FINDINGS.md \
   PHASE4-OVERLAY-ICONS-FINDINGS.md \
   PHASE4-QUICKMENU-EVERYWHERE-RESTYLE-FINDINGS.md \
   PHASE4-QUICKMENU-FINDINGS.md \
   PHASE4-REAL-ICONS-FINDINGS.md \
   PHASE4-STATUSBAR-FINDINGS.md \
   PHASE4-TRANSLUCENT-STATUSBAR-FINDINGS.md \
   PHASE4-VOLUME-COMBO-FINDINGS.md \
   PHASE6-CHARGING-DECOUPLE-FINDINGS.md \
   PHASE6-DAEMON-SETTINGS-FINDINGS.md \
   PHASE6-JOYSTICK-CALIBRATION-FINDINGS.md \
   WIFI-BUILD-FINDINGS.md \
   findings/
```

(Before running this, re-run `ls docs/superpowers/plans/*FINDINGS*.md` yourself to confirm this list is still exactly current — a task may have landed between plan-writing and execution time.)

- [ ] **Step 3: Move the loose reference artifacts**

This exact list, confirmed via the same fresh `ls`:

```bash
cd "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans"
mv WINDOWS-WSL2-MIGRATION.md \
   batocera.conf.wifi-template \
   sd-batocera-boot-wifi-snippet.txt \
   sd-circuitswordlog \
   sd-config-hdmitest.txt \
   sd-config.txt \
   reference/
```

- [ ] **Step 4: Delete the build logs**

```bash
cd "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans"
rm -f wifi-build.log wifi-build2.log wifi-build3.log wifi-build4.log \
      wifi-build5.log wifi-build6.log wifi-build7.log wifi-build8.log wifi-build9.log
```

- [ ] **Step 5: Find and fix stale cross-references**

Search the whole main project directory for any reference to the old (pre-move) paths:

```bash
cd "/Users/bas/Circuit-Sword Batocera"
grep -rln "plans/PHASE[0-9]*-.*FINDINGS\|plans/WIFI-BUILD-FINDINGS\|plans/WINDOWS-WSL2-MIGRATION\|plans/batocera\.conf\.wifi-template\|plans/sd-batocera-boot-wifi-snippet\|plans/sd-circuitswordlog\|plans/sd-config-hdmitest\|plans/sd-config\.txt\|plans/wifi-build[0-9]*\.log" \
  docs/ README.md CLAUDE.md tests/ batocera-build/ 2>/dev/null
```

For every file this returns, open it and update the reference to point at the new `findings/` or `reference/` path (e.g. `docs/superpowers/plans/PHASE4-QUICKMENU-FINDINGS.md` becomes `docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md`). If the grep returns nothing, that's a valid, complete outcome — say so in the report rather than inventing a change.

- [ ] **Step 6: Verify**

```bash
ls "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/"
```

Expected: only `.md` plan files (named `YYYY-MM-DD-topic.md`) plus the `findings/` and `reference/` subdirectories — no stray `PHASE*-FINDINGS.md`, no `*.log`, no loose reference files at the top level.

```bash
ls "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/"
ls "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/reference/"
```

Expected: the moved files, per Steps 2-3's lists.

Re-run Step 5's grep once more — expect zero remaining hits.

---

### Task 3: `env.sh` space-in-path early-fail check

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh`
- Test: `/Users/bas/Circuit-Sword Batocera/tests/test_env_sh_space_check.sh`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: `env.sh` now fails fast with a clear message whenever the effective `$BATOCERA_SRC` contains a space, instead of silently corrupting the Buildroot build. Task 5 (BUILDING.md) documents this behavior and its workaround; Task 7 (end-to-end validation) must use that workaround (an explicit space-free `BATOCERA_SRC` override) to get past this check.

- [ ] **Step 1: Add the check to `env.sh`**

The current relevant lines (near the top of the file, right after `REPO_ROOT` is computed):

```bash
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# --- Paths -------------------------------------------------------------
# BATOCERA_SRC: the actual git checkout of batocera.linux with our patches
# applied. Lives inside this repo, under batocera-build/build/ -- this
# checkout is source code only (no case-sensitivity requirement, unlike
# OUTPUT_DIR/DL_DIR/CCACHE_DIR below), so it's fine on the project's
# normal filesystem. It's git-ignored (see .gitignore) and fully
# regenerable via setup-build-tree.sh -- never commit it directly, commit
# changes to batocera-build/patches/ instead.
#
# NOTE: setup-build-tree.sh runs `git clean -fdx` + `git checkout --` in
# $BATOCERA_SRC to guarantee a clean patch-apply -- don't hand-edit files
# in there expecting them to survive a re-run; edit batocera-build/patches/
# (or the overlay/ files) and re-run setup-build-tree.sh instead.
: "${BATOCERA_SRC:=$REPO_ROOT/batocera-build/build/batocera.linux}"
```

Insert this new block immediately after the `: "${BATOCERA_SRC:=...}"` line (so the check runs against the FINAL effective value, whether it came from the default or an explicit override), and before anything else in the file uses `$BATOCERA_SRC`:

```bash
# GNU Make's $(realpath $(CURDIR)) -- used by Buildroot's own top-level
# Makefile to compute PROJECT_DIR -- silently truncates at the first
# space in the path. A BATOCERA_SRC under a space-containing directory
# (e.g. this project's own default location, if REPO_ROOT itself
# contains a space) corrupts PROJECT_DIR deep inside Buildroot with no
# error message -- confirmed on this project: builds failed with
# "Makefile:69: *** open: /Users/bas/Circuit-Sword/configs/.user_defconfig:
# No such file or directory" (the space silently truncated
# "/Users/bas/Circuit-Sword Batocera" down to "/Users/bas/Circuit-Sword").
# Fail fast and explicitly instead of letting this corrupt a multi-hour
# build partway through.
if [[ "$BATOCERA_SRC" == *" "* ]]; then
    echo "ERROR: BATOCERA_SRC contains a space: $BATOCERA_SRC" >&2
    echo "" >&2
    echo "GNU Make's \$(realpath \$(CURDIR)), used by Buildroot's own" >&2
    echo "top-level Makefile, silently truncates at the first space in a" >&2
    echo "path -- this corrupts the build's PROJECT_DIR with no clear" >&2
    echo "error, deep inside Buildroot, not here." >&2
    echo "" >&2
    echo "Fix: set BATOCERA_SRC to an explicit, space-free path before" >&2
    echo "sourcing this script, e.g.:" >&2
    echo "  export BATOCERA_SRC=/path/without/spaces/batocera.linux" >&2
    echo "  source \"\$(dirname \"\$0\")/env.sh\"" >&2
    exit 1
fi
```

- [ ] **Step 2: Write the host test**

Check `/Users/bas/Circuit-Sword Batocera/tests/` for an existing convention for testing sourced shell scripts before writing this — this project has C tests (`run-c-tests.sh`), Python tests, and (as of the AC/battery-detection plan) a bash test for a standalone script, but no existing convention for testing a *sourced* library script like `env.sh`. Since none exists, write a simple standalone script that sources `env.sh` in subshells with different `BATOCERA_SRC` values and checks exit codes — this is the first of its kind, keep it minimal.

Create `/Users/bas/Circuit-Sword Batocera/tests/test_env_sh_space_check.sh`:

```bash
#!/bin/sh
# Host test for batocera-build/scripts/env.sh's space-in-BATOCERA_SRC
# early-fail check. Sources env.sh in subshells (never in this shell
# directly -- it has `set -euo pipefail` and calls `exit` on failure,
# which would kill the test runner too) with different BATOCERA_SRC
# values and checks exit codes / stderr content.
set -e

ENV_SH="/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"

failures=0
check() {
    if [ "$1" = "$2" ]; then
        echo "  ok   $3"
    else
        echo "  FAIL $3 (expected [$2], got [$1])"
        failures=$((failures + 1))
    fi
}

# --- a space-containing BATOCERA_SRC must fail fast with a clear message ---
output=$(BATOCERA_SRC="/tmp/has a space/batocera.linux" sh -c ". '$ENV_SH'" 2>&1) && rc=0 || rc=$?
check "$rc" "1" "space-containing BATOCERA_SRC exits non-zero"
case "$output" in
    *"BATOCERA_SRC contains a space"*) check "matched" "matched" "error message names the problem" ;;
    *) check "not matched" "matched" "error message names the problem" ;;
esac

# --- a space-free BATOCERA_SRC must NOT trip this specific check ---
# (env.sh may still exit non-zero later for other reasons -- e.g.
# require_src's missing-checkout check, if called -- but merely sourcing
# env.sh itself, with no other function called, must succeed for a
# space-free path.)
output=$(BATOCERA_SRC="/tmp/no-space/batocera.linux" sh -c ". '$ENV_SH'" 2>&1) && rc=0 || rc=$?
check "$rc" "0" "space-free BATOCERA_SRC does not trip the space check"
case "$output" in
    *"BATOCERA_SRC contains a space"*) check "matched" "not matched" "space-free path does not trigger the space error" ;;
    *) check "not matched" "not matched" "space-free path does not trigger the space error" ;;
esac

echo ""
echo "$([ "$failures" -eq 0 ] && echo PASSED || echo FAILED) ($failures failures)"
[ "$failures" -eq 0 ]
```

- [ ] **Step 3: Run the test**

```bash
chmod +x "/Users/bas/Circuit-Sword Batocera/tests/test_env_sh_space_check.sh"
"/Users/bas/Circuit-Sword Batocera/tests/test_env_sh_space_check.sh"
```

Expected: `PASSED (0 failures)`.

- [ ] **Step 4: Confirm the main project's own real default path correctly fails**

This is expected, intentional behavior, not a bug to fix in this task — the main directory's actual path (`/Users/bas/Circuit-Sword Batocera`) contains a space, so this check is DESIGNED to reject the default:

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
bash -c "source ./env.sh" 2>&1 | head -5
```

Expected: the new error message, confirming this is exactly the case this check exists to catch. Note this explicitly in the report — this is why Task 7's end-to-end validation must use an explicit space-free `BATOCERA_SRC` override, which BUILDING.md (Task 5) documents.

---

### Task 4: Bounded dead-code audit of `batocera-build/scripts/` and `patches/`

**Files:**
- Modify (trivial fixes only, if any are found): any of `batocera-build/scripts/*.sh`
- Report only (no code changes expected for non-trivial findings): the task report itself

**Interfaces:**
- Consumes: nothing from other tasks (read-only audit).
- Produces: a per-file verdict list in the report — Task 5 (BUILDING.md) and Task 7 (end-to-end validation) both read this report before writing/running their own steps, so any real discrepancy flagged here should inform (but not silently change) how those tasks proceed.

- [ ] **Step 1: Read all 7 files in full**

`build-all.sh`, `build-image.sh`, `build-kernel.sh`, `build-wifi.sh`, `setup-disk-image.sh`, `extract-artifacts.sh`, `setup-build-tree.sh` — all in `/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/`.

- [ ] **Step 2: Check a specific, already-identified discrepancy**

`env.sh` defines `MAKE_OPTS` with a comment explaining it's needed for GCC 15 host-package build failures:

```bash
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17 -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration' HOST_CXXFLAGS='-O2' BR2_JLEVEL=4"
```

`build-kernel.sh` applies `$MAKE_OPTS` to its two synchronous `linux-reconfigure`/`linux-rebuild` calls (`env $MAKE_OPTS make ...`) but does **NOT** apply it to either script's final `nohup make ... "$BOARD-build"` line (neither `build-image.sh`'s nor `build-kernel.sh`'s). Confirm this by re-reading both files' final build commands.

This session's actual full-image builds (every prior plan) also never manually included `$MAKE_OPTS`/`HOST_CFLAGS` overrides and still succeeded with zero errors — so either (a) `env.sh`'s comment is now stale (the GCC-15 workaround isn't actually needed against the current Docker build image), or (b) the scripts have a real latent bug that happened not to matter for the packages built this session. **Do not resolve this yourself** — it requires a judgment call about which the two scripts' own history doesn't answer. Report it as a specific, named finding: "build-image.sh and build-kernel.sh's final nohup build command does not apply \$MAKE_OPTS, unlike build-kernel.sh's own earlier synchronous calls in the same file — inconsistent, and env.sh's comment claims this matters for GCC 15, but this session's real builds succeeded without it."

- [ ] **Step 3: Check general accuracy of the other scripts against this session's actual usage**

Cross-check each script's `make` invocation shape (target names like `$BOARD-build`, flags like `BR_DOCKER_VOLUMES`, `BR2_EXTERNAL`, `O=`, `DL_DIR=`, `BATCH_MODE=1`) against the pattern used across this whole project's prior plans (every plan this session that ran a Buildroot build used: `BR_DOCKER_VOLUMES=1`, `BR2_EXTERNAL=<the dev-tree path>`, `O=<output path>/bcm2837`, `DL_DIR=<download path>`, target `bcm2837-build` for full builds or `PKG=<name>-rebuild`/`-reinstall`/`-dirclean bcm2837-pkg` for single-package force-refreshes per CLAUDE.md Hard Rule #7). Note: none of these 7 scripts have a dedicated "force-rebuild a single package" mode — this session always ran that pattern as a raw ad-hoc `make` command, not through any script. This is not a bug (these scripts were never meant to cover that case), but note it as a gap BUILDING.md (Task 5) needs to cover explicitly since it's the single most-used command pattern from every prior plan this session.

For each of the 7 files, record a verdict: (a) still accurate and usable as-is, (b) stale/needs a fix, or (c) fully superseded/dead. Apply ONLY trivial, obviously-safe fixes inline (a stale comment, an outdated path/filename reference) — anything bigger goes in the report as a finding, not a silent change.

- [ ] **Step 4: Check the two loose patch files' continued relevance**

`batocera-build/patches/0001-remove-broken-K-R-forward-declarations.patch` (targets `xxd.c`'s K&R-style forward declarations conflicting with modern glibc headers) and
`batocera-build/patches/0004-linux-user-fix-redefinition-of-struct-sched_attr.patch` (targets QEMU's `linux-user/syscall.c` redefining `struct sched_attr`, already provided by newer host `linux/sched/types.h`).

Both are host-toolchain-version fixes (GCC/glibc version mismatches on the Docker build container), not board-specific patches — so they're not tied to `PINNED_COMMITS.txt`'s `batocera.linux`/`buildroot` commits directly, but rather to whatever base image the Docker build container uses. Note in the report whether anything in this session's own build logs (if recalled/known) suggests either of these fixes was actually exercised (i.e. did a build actually hit `xxd.c` or QEMU's `linux-user` build step) — if genuinely unknown, say so; do not guess. **Do not attempt to verify these still apply cleanly by trying to apply them** — Task 7's real `setup-build-tree.sh` run is what proves or disproves that; this task is a read-only relevance check only.

- [ ] **Step 5: Write the report**

The report itself is this task's deliverable for the audit portion. Include: the 7-file verdict table, the `$MAKE_OPTS` discrepancy write-up from Step 2, the "no dedicated force-rebuild script" gap from Step 3 (flagged for Task 5 to cover), and the two patch files' relevance notes from Step 4. If zero trivial fixes were warranted anywhere, say so plainly — that is a complete, valid outcome, not a shortfall.

---

### Task 5: Write `BUILDING.md`

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/BUILDING.md`

**Interfaces:**
- Consumes: Task 3's `env.sh` space-check behavior (must document the workaround), Task 4's audit findings (must incorporate the `$MAKE_OPTS` discrepancy and the "no dedicated force-rebuild script" gap as real, documented facts — not glossed over).
- Produces: the document Task 7 follows verbatim to run its end-to-end build — if Task 7 finds a command in this doc doesn't work, that's a bug in THIS task, not Task 7's problem to silently paper over.

- [ ] **Step 1: Write the document**

Create `/Users/bas/Circuit-Sword Batocera/BUILDING.md`:

```markdown
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
`batocera-build/scripts/build-image.sh` does.

Once finished, the image lands **inside the Docker named volume**
(`batocera-output-bcm2837`), not directly on the host filesystem — named
volumes aren't Finder-browsable. Extract it:

```bash
batocera-build/scripts/extract-artifacts.sh
```

This copies the built image(s) out to `output/images/` on the host.

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

This project doesn't currently have a dedicated script for the
force-rebuild-a-single-package workflow above — every prior feature plan
ran this as an ad-hoc `make ... PKG=<name>-rebuild bcm2837-pkg` command
directly, not through `batocera-build/scripts/`. Use the pattern above.

### A known rough edge

`build-image.sh`/`build-kernel.sh`'s scripted full-build commands don't
apply `env.sh`'s own `$MAKE_OPTS` (host-compiler flags meant to work
around GCC 15 host-package build failures) to their final build step,
even though `env.sh`'s comment describes them as necessary. In practice,
every build this project has actually run succeeded without them — so
this may be stale, or may only matter for host packages this project
hasn't rebuilt recently. If a build fails on a host-package compile
error mentioning `incompatible-pointer-types` or
`implicit-function-declaration`, try prefixing the `make` command with
`env $MAKE_OPTS` (after sourcing `env.sh`, which defines it).

## Timing

(Record real numbers here after Task 7's end-to-end run — filled in as
part of that task, not invented ahead of time.)
```

Leave the "Timing" section's actual numbers for Task 7 to fill in with real, measured data — do not invent plausible-sounding numbers now.

- [ ] **Step 2: Self-review checklist**

Confirm the document covers every item the design doc's "4. BUILDING.md" section requires: prerequisites ✓, first-time setup + space-in-path constraint + `BATOCERA_SRC` override ✓, the exact build invocation shape ✓, `extract-artifacts.sh` usage ✓, Hard Rule #7 in workflow terms with the plain-copy-vs-patch-based contrast ✓, the patch-capture workflow ✓. Confirm no `TBD`/placeholder text remains (the "Timing" section is an intentional exception, explicitly deferred to Task 7, not a placeholder left by accident). Confirm every command example is copy-pasteable (no `<...>` left unexplained without surrounding context saying what to substitute).

- [ ] **Step 3: Cross-check against the actual scripts**

Re-read `build-image.sh` and `extract-artifacts.sh` (from Task 4's read) and confirm the documented commands' flags/paths genuinely match what those scripts do — don't let the doc silently diverge from the real scripts.

---

### Task 6: `git init` + first commit

**Files:**
- Create: `/Users/bas/Circuit-Sword Batocera/.git/` (via `git init`)

**Interfaces:**
- Consumes: the clean, reorganized state from Tasks 1-5 — this task must run after all of them.
- Produces: the main project directory's first-ever commit. No later task in this plan depends on a specific commit hash (Task 7 doesn't touch this repo's git history at all).

- [ ] **Step 1: Confirm Tasks 1-5 are complete**

This is the first time this directory has ever been through `git init` — there's no history to lose, but the point of doing this last is that the first commit should already be the clean, final state. Confirm (via your own todo/ledger tracking, not by re-doing the work) that Tasks 1-5 have landed before proceeding.

- [ ] **Step 2: `git init`**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
git init
```

- [ ] **Step 3: Review what would be staged, before staging**

```bash
git status
```

Read the full output carefully. In particular, check:

- Does `Retropie_source/` show up as untracked? Check its size (`du -sh Retropie_source`) — this session's context describes it as read-only reference material carried over from the prior RetroPie-based build. If it's large, note this in the report as a potential concern — **do not unilaterally add a new `.gitignore` rule for it** that the design doc didn't request; just flag it as a finding for the human to decide on, separately from this task's job of doing the actual `git init`.
- Same check for `nixos-reference/` (an external reference clone).
- Confirm `/output/` and `batocera-build/build/` (both already in `.gitignore`) are correctly excluded from `git status`'s untracked-files list — this is the first real-world test of those two `.gitignore` patterns ever, since this directory has never been through `git add` before.
- Confirm no leftover large/regenerable files slipped through Task 1's cleanup (re-run `find . -size +100M -not -path './.git/*'` to catch anything unexpectedly large).

- [ ] **Step 4: Stage and commit**

If Step 3 didn't surface a blocking concern:

```bash
git add -A
git status  # final check of what's actually staged
git commit -m "Initial commit: Circuit-Sword Batocera port

Project history to this point lived only in conversation context and
a separate, unversioned checkout. This is the first commit of the
main project directory itself -- docs, specs, plans, tests, and the
batocera-build/ patch-capture tooling that drives the actual buildable
tree at \$BATOCERA_SRC."
```

If Step 3 DID surface a real concern (e.g. `Retropie_source/`/`nixos-reference/` are enormous and clearly shouldn't be committed as-is), stop and report it rather than deciding unilaterally — this is exactly the kind of judgment call that belongs to the human, not silently resolved mid-task.

- [ ] **Step 5: Verify**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
git log --oneline   # expect exactly one commit
git status          # expect: clean
git check-ignore -v output batocera-build/build .DS_Store  # each should print a match (confirms Task 1's .gitignore addition works, deferred from Task 1's own verification)
```

---

### Task 7: End-to-end build validation

**Files:**
- None created/modified by default — this task runs the documented build. If it uncovers a real bug (patch-apply failure, build break), fix it in whichever file is actually broken (`batocera-build/patches/batocera-linux.patch`, `batocera-build/PINNED_COMMITS.txt`, or a `batocera-build/scripts/*.sh` file) and note the fix in the report.

**Interfaces:**
- Consumes: `BUILDING.md` (Task 5) as the exact command sequence to follow; `env.sh`'s space-check (Task 3) as a constraint requiring an explicit `BATOCERA_SRC` override.
- Produces: a real, verified-working flashable image, and real timing numbers to fill into `BUILDING.md`'s "Timing" section.

- [ ] **Step 1: Set up a space-free `BATOCERA_SRC`**

Per Task 3's new check and `BUILDING.md`'s documented workaround:

```bash
export BATOCERA_SRC=/tmp/circuitsword-repo-cleanup-validation/batocera.linux
```

(Or any other space-free path of your choosing — the point is that this task must genuinely use the documented override, not bypass the check some other way.)

- [ ] **Step 2: Run `setup-build-tree.sh`, following `BUILDING.md` exactly**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
batocera-build/scripts/setup-build-tree.sh
```

If this fails (patch doesn't apply cleanly against the pinned commit, or any other error): this is a real bug per the design doc's explicit instruction ("Any patch-apply failure or build break found here is a real bug to fix... not just a doc note"). Diagnose and fix it — likely candidates: `batocera-build/patches/batocera-linux.patch` has drifted from what `PINNED_COMMITS.txt`'s pinned commit expects (unlikely, since this patch was regenerated fresh at the end of every plan this session), or a loose per-file patch (Task 4's audit covered these) no longer applies. Do not skip or abbreviate this step to save time.

- [ ] **Step 3: Run the full build, following `BUILDING.md` exactly**

```bash
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 \
     O=/tmp/circuitsword-repo-cleanup-validation/output/bcm2837 \
     BR2_EXTERNAL="$BATOCERA_SRC" \
     DL_DIR=/tmp/circuitsword-repo-cleanup-validation/output/dl \
     BATCH_MODE=1 \
     bcm2837-build
```

Budget for a full image build's worth of time — the same order of magnitude as this project's largest prior builds this session (multiple hours on a cold cache). Do not abbreviate or skip this to save time; a partial/simulated "looks like it would probably work" is not what this task exists to prove.

If the build breaks: same rule as Step 2 — diagnose and fix the real cause, don't just note it for later.

- [ ] **Step 4: Verify the build log**

```bash
grep -c "Error [0-9]" <path-to-build-log-or-terminal-output>
```

Expected: `0`.

- [ ] **Step 5: Extract and verify the artifact**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
DEST=/tmp/circuitsword-repo-cleanup-validation/output/images
BATOCERA_SRC="$BATOCERA_SRC" OUTPUT_DIR=/tmp/circuitsword-repo-cleanup-validation/output/bcm2837 \
  DL_DIR=/tmp/circuitsword-repo-cleanup-validation/output/dl \
  batocera-build/scripts/extract-artifacts.sh "$DEST"
```

(Adjust env var overrides as needed if `extract-artifacts.sh`'s own env.sh sourcing doesn't pick these up automatically from the shell — read the script, from Task 4's earlier read, to confirm the right invocation.)

```bash
gzip -t "$DEST"/batocera-bcm2837-*.img.gz
```

Expected: exits 0, no error. This project has previously seen a transient Docker-volume read race produce a false-alarm truncated-looking artifact — if this fails, re-run the check once before treating it as a real problem (per this project's established practice this session).

- [ ] **Step 6: Fill in `BUILDING.md`'s Timing section with real numbers**

Update the "Timing" section in `/Users/bas/Circuit-Sword Batocera/BUILDING.md` with the actual wall-clock time this build took (cold cache, since this was a fresh `setup-build-tree.sh` run), and the machine specs it ran on (CPU core count, RAM allocated to Docker Desktop) for context.

- [ ] **Step 7: Report**

State plainly in the report: a flashable `.img.gz` was produced and its gzip integrity verified — this proves the documented build path is genuinely reproducible from a fresh checkout. **This is not a hardware/on-device test** — flashing and booting on real Circuit-Sword hardware remains unverified, consistent with this whole project's testing conventions (no hardware in CI).

If this task's Step 6 (in Task 6's commit) surfaced a `Retropie_source/`/`nixos-reference/` sizing concern, or if this task itself required any real fix to the patch/pinned-commit/scripts, summarize it clearly here as well — these are exactly the kind of findings the human needs to see, not bury in a wall of build log.
