# Repo Cleanup & Build Tooling — Design

Turns this project from a working-but-undocumented pile of scripts and a
loose dev-tree checkout into something a new contributor can actually
clone and build from, in place of a real OTA update mechanism this
project can't deliver itself.

## Context

Today, "the project" is really three loosely-connected things:

1. The main project directory (`/Users/bas/Circuit-Sword Batocera`) — docs,
   tests, `batocera-build/` (patches + scripts), and gitignored `output/`.
   **This directory is not a git repository at all** — no history, nothing
   to clone.
2. `circuit-sword-external/`, a stub br2-external tree from an abandoned
   "Phase 1" design (`package/` contains only a placeholder README). The
   project moved to a different mechanism (see #3) without ever removing
   this.
3. The actual mechanism in use: `batocera-build/scripts/setup-build-tree.sh`
   clones upstream `batocera.linux` at a pinned commit
   (`batocera-build/PINNED_COMMITS.txt`), applies
   `batocera-build/patches/batocera-linux.patch` (one cumulative diff) plus
   a couple of loose per-file patches and an overlay directory. This
   regenerated checkout is meant to live at `$BATOCERA_SRC`
   (`batocera-build/build/batocera.linux` by default), gitignored and
   fully regenerable.

In practice, this whole session's real work happened in a fourth
location — `/Users/bas/batocera-build-wifi/batocera.linux`, a manually
created, space-free checkout — because `env.sh`'s default `$BATOCERA_SRC`
sits under a path containing a space (`.../Circuit-Sword Batocera/...`),
which silently corrupts `$(realpath $(CURDIR))` in Buildroot's own
Makefile (GNU Make's `realpath` truncates at the first space). Every build
this session explicitly overrode `BR2_EXTERNAL`, `BR_DOCKER_VOLUMES=1`,
`O=`, and `DL_DIR=` by hand, and prepended Homebrew's GNU make/find to
`PATH` (macOS's BSD tools don't work for this Buildroot tree) — none of
this is written down anywhere outside this conversation's history.

Two loose, large leftover build artifacts
(`batocera-bcm2837-43.1-20260530.img`, ~7GB, and its `.gz`, ~2GB) sit at
the top of the main directory, predating the `output/` convention.

CLAUDE.md's Hard Rule #7 (a full image build does not auto-pick-up edited
source in already-built packages) is a real, non-obvious Buildroot trap
that bit this project multiple times this session even with full context
— a fresh contributor would almost certainly ship silently-stale builds
without ever knowing it.

## Behavior

**1. Cleanup**
- Delete `batocera-bcm2837-43.1-20260530.img` and its `.gz` (regenerable
  build output, currently un-gitignored top-level cruft).
- Delete `circuit-sword-external/` (dead code from the abandoned Phase-1
  design — the patch-file mechanism in `batocera-build/` is the real one).
- Delete stray `.DS_Store` files and add `.DS_Store` to `.gitignore`.

**2. Bounded dead-code audit of the build tooling**
Not a whole-codebase audit — scoped to exactly the area this session
touched and learned about:
- `batocera-build/scripts/*.sh` (`build-all.sh`, `build-image.sh`,
  `build-kernel.sh`, `build-wifi.sh`, `setup-disk-image.sh`,
  `setup-build-tree.sh`, `extract-artifacts.sh`) — check each is still
  consistent with the actual workflow used this session (manual
  `BR2_EXTERNAL` override, `BR_DOCKER_VOLUMES=1`, Docker-volume artifact
  extraction) and still references real, current package/target names.
- The loose per-file patches in `batocera-build/patches/` (`0001-...`,
  `0004-...`) — confirm each is still needed against the pinned commit
  (not superseded by an upstream fix since `PINNED_COMMITS.txt` was set).
- `PINNED_COMMITS.txt` — confirm it matches the base commit
  (`155c2d8d304cbb53db52e9479dcf683392821d5c`) every task this session
  actually diffed `batocera-build/patches/batocera-linux.patch` against.
- Findings get fixed inline if trivial (stale comment, dead reference) or
  flagged in `BUILDING.md`/a follow-up note if they need a real decision
  (e.g. a script that's actively wrong, not just stale).

**3. Fix the space-in-path landmine, not just document it**
`env.sh` gains an early check: if `$REPO_ROOT` (or an explicitly-set
`$BATOCERA_SRC`) contains a space, fail immediately with a clear message
explaining the Buildroot `realpath` truncation bug and the workaround
(set `BATOCERA_SRC` to a space-free path). This replaces silent, deep,
hard-to-diagnose corruption with a fast, explicit failure — the actual
project directory does not move.

**4. `BUILDING.md`**
New top-level doc covering what CLAUDE.md doesn't: the actual day-to-day
build workflow. Contents:
- Prerequisites (Docker Desktop, resource allocation, Homebrew GNU
  make/find on macOS).
- First-time setup: `setup-build-tree.sh`, the space-in-path constraint,
  `BATOCERA_SRC` override.
- Building: the exact `make` invocation shape this session used
  (`BR2_EXTERNAL`, `BR_DOCKER_VOLUMES=1`, `O=`, `DL_DIR=`), and
  `extract-artifacts.sh` for pulling the finished image out of the Docker
  volume.
- **CLAUDE.md Hard Rule #7 in workflow terms**: after editing a package's
  source, which `PKG=<name>-rebuild` / `-reinstall` / `-dirclean` variant
  to use and why, plus the direct Docker-volume verification step (never
  trust a clean build log alone) — with the two concrete examples from
  this session (plain-copy Python package vs. a patch-based C++ package
  like `batocera-emulationstation`, which needs `-dirclean` since a plain
  rebuild reuses the already-extracted, unpatched source tree).
- How the patch-capture workflow works (`git diff --submodule=diff
  <pinned-commit> HEAD` against the dev-tree, landing in
  `batocera-build/patches/batocera-linux.patch`) — this is how new
  changes actually get contributed back into the regenerable build tree,
  not by hand-editing `$BATOCERA_SRC` directly.

**5. Git repository**
`git init` in the main project directory, add a first commit with
everything present after cleanup. No remote is created as part of this
work — the user will handle that separately.

**6. Reorganize `docs/superpowers/plans/`**
This directory has become cluttered over the course of the project: 19
plan files alongside 12 stray `*-FINDINGS.md` files (inconsistent
`PHASE0`/`PHASE3`/`PHASE4`/`PHASE6`-prefixed naming, unlike the plan
files' consistent `YYYY-MM-DD-topic.md` scheme), a handful of loose
reference artifacts (`batocera.conf.wifi-template`, `sd-*` files,
`WINDOWS-WSL2-MIGRATION.md`), and 9 `wifi-build*.log` build logs.
- Move all `*-FINDINGS.md` files into a new `docs/superpowers/plans/findings/`
  subdirectory.
- Move the loose non-plan, non-findings reference artifacts
  (`batocera.conf.wifi-template`, `sd-batocera-boot-wifi-snippet.txt`,
  `sd-circuitswordlog`, `sd-config-hdmitest.txt`, `sd-config.txt`,
  `WINDOWS-WSL2-MIGRATION.md`) into a new
  `docs/superpowers/plans/reference/` subdirectory.
- Delete the 9 `wifi-build*.log` files — already `.gitignore`d
  (`docs/superpowers/plans/*.log`), pure disk clutter, and their relevant
  findings are already captured in the FINDINGS docs.
- `docs/superpowers/specs/` is left as-is (17 files, flat, consistently
  named by date+topic — already organized enough, no subfolder needed).
- After moving, grep the moved files' own content and every other doc for
  hardcoded paths to the old locations (e.g. a design doc referencing
  `docs/superpowers/plans/findings/PHASE4-QUICKMENU-FINDINGS.md` directly) and
  update them — a move that breaks cross-references is not actually
  cleaner.

**7. End-to-end validation**
Run `setup-build-tree.sh` for real against a fresh clone location, apply
the current `batocera-build/patches/batocera-linux.patch`, and run a full
image build through to a flashable `.img.gz` — proving the documented,
regenerable path actually works, not just the ad-hoc dev-tree this
session used. Any patch-apply failure or build break found here is a real
bug to fix (in the patch or `PINNED_COMMITS.txt`), not just a doc note.

## Testing

- **Off-device / verifiable now**: the cleanup itself (file deletions,
  `.gitignore` additions), `env.sh`'s new space-check (test by pointing
  `BATOCERA_SRC` at a space-containing path and confirming it fails fast
  with a clear message, and at a space-free path and confirming it still
  works), the dead-code audit's findings, `git init` + first commit
  succeeding, and the full `setup-build-tree.sh` + image build (this *is*
  the off-device-verifiable proof that the documented path works end to
  end — no hardware needed to build an image, only to flash/run it).
- **Needs on-device validation**: none — this whole project is
  build-tooling and documentation, not a runtime feature. The resulting
  image is the same kind of artifact every other plan this session
  already produced; this plan doesn't add new on-device behavior to test.

## Out of scope

- Creating or configuring a git remote (GitHub or otherwise) — explicitly
  deferred to the user.
- Moving the main project directory to a space-free path — the space
  itself isn't being eliminated, only guarded against with a fast,
  explicit failure instead of silent corruption.
- A full dead-code audit of the runtime C/Python/firmware codebase
  (quickmenu, daemon, statusbar, etc.) — out of scope; this plan's audit
  is bounded to the build-tooling layer (`batocera-build/scripts/`,
  `patches/`, `PINNED_COMMITS.txt`) only.
- Building or documenting an actual OTA/auto-update mechanism — this
  project explicitly replaces that Phase 5 item with "give the building
  blocks" per the user's own framing; CLAUDE.md's existing manual-update
  hard rule is unaffected.
- CI/automated builds — this is about a human contributor's local
  workflow, not a hosted pipeline.
