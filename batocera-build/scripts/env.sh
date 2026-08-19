#!/bin/bash
# Shared environment for all batocera-build/scripts/*.sh.
# Source this, don't run it directly: `source "$(dirname "$0")/env.sh"`

set -euo pipefail

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

# BR_DOCKER_VOLUMES=1 (default): the actual multi-hundred-GB Buildroot
# output/downloads/ccache live inside Docker Desktop's own Linux VM as
# named volumes (batocera-output-$BOARD, batocera-dl, batocera-ccache),
# not bind-mounted from the host. Measured ~3x faster than a host bind
# mount for this workload (many small files, heavy chmod/rename traffic
# -- see WIFI-BUILD-FINDINGS.md "Performance" section) and sidesteps
# macOS's case-insensitive-APFS problem entirely (glibc's build has
# case-colliding stamp files that broke the old host-bind-mount setup) --
# no case-sensitive disk image needed. Trade-off: named volumes aren't
# Finder-browsable -- use extract-artifacts.sh to copy the built image
# out to the host, and expect an empty/cold cache the first time you
# switch (nothing carries over from a prior host-bind-mount build).
#
# Set BR_DOCKER_VOLUMES=0 to fall back to the old host-bind-mount path
# (needs setup-disk-image.sh on macOS first -- see its header comment).
: "${BR_DOCKER_VOLUMES:=1}"
export BR_DOCKER_VOLUMES

: "${BATOCERA_BUILD_ROOT:=$REPO_ROOT/output}"
: "${OUTPUT_DIR:=$BATOCERA_BUILD_ROOT/output}"
: "${DL_DIR:=$BATOCERA_BUILD_ROOT/dl}"
: "${CCACHE_DIR:=$BATOCERA_BUILD_ROOT/buildroot-ccache}"

: "${BOARD:=bcm2837}"
: "${LOG_FILE:=$REPO_ROOT/docs/superpowers/plans/findings/wifi-build.log}"

export OUTPUT_DIR DL_DIR CCACHE_DIR

DOCKER_REPO="${DOCKER_REPO:-batoceralinux}"
DOCKER_IMAGE_NAME="${DOCKER_IMAGE_NAME:-batocera.linux-build}"
DOCKER_IMAGE="$DOCKER_REPO/$DOCKER_IMAGE_NAME"

# --- Build tuning --------------------------------------------------------
# GCC 15 default-behavior changes need the two -Wno-error flags below or
# several host packages fail to build (host-yasm's use of `false`/`true`
# as enum members, for example, breaks under GCC 15's C23-by-default mode
# without -std=gnu17). See BUILDING.md's "A known rough edge" section for
# how this actually gets applied to a build invocation -- it's not enough
# to export this var, the build-*.sh scripts have to pass it through
# explicitly as a `make` command-line variable.
#
# BR2_JLEVEL is intentionally NOT part of MAKE_OPTS -- it doesn't work
# there. See BUILDING.md's "Controlling build parallelism" section for
# why (short version: it only takes effect via a generated batocera.mk,
# not via MAKE_OPTS/env vars) and the $BR2_JLEVEL default below.
export MAKE_OPTS="HOST_CFLAGS='-O2 -std=gnu17 -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration' HOST_CXXFLAGS='-O2'"

# BR2_JLEVEL=2 is a safe default verified end-to-end on an 8-core M3 with
# ~9.7GB allocated to Docker Desktop. Raising this trades RAM headroom
# for speed; it's safe to raise on a host with more Docker memory to
# spare, since this project never sets PARALLEL_BUILD=1 (which would also
# force on BR2_PER_PACKAGE_DIRECTORIES and break incremental resumes --
# see BUILDING.md). webkitgtk ignores this value either way -- see
# BUILDING.md's webkitgtk njobs section.
: "${BR2_JLEVEL:=2}"
export BR2_JLEVEL

# GNU make/find on macOS (BSD make/find don't work for this build)
if [[ "$(uname)" == "Darwin" ]]; then
    export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
fi

require_src() {
    if [ ! -d "$BATOCERA_SRC/.git" ]; then
        echo "ERROR: $BATOCERA_SRC is not a batocera.linux checkout." >&2
        echo "Run batocera-build/scripts/setup-build-tree.sh first, or set BATOCERA_SRC." >&2
        exit 1
    fi
}

require_disk() {
    if [ "$BR_DOCKER_VOLUMES" = "1" ]; then
        # Only small stamp/config files land on the host in this mode --
        # the heavy build lives in Docker named volumes instead. A plain
        # directory is fine on any filesystem, no disk image needed.
        mkdir -p "$OUTPUT_DIR" "$DL_DIR" "$CCACHE_DIR"
        return 0
    fi
    if [ ! -d "$BATOCERA_BUILD_ROOT" ]; then
        echo "ERROR: $BATOCERA_BUILD_ROOT not mounted." >&2
        echo "Run batocera-build/scripts/setup-disk-image.sh first, or set BATOCERA_BUILD_ROOT." >&2
        exit 1
    fi
}

require_docker() {
    if ! docker info >/dev/null 2>&1; then
        echo "ERROR: Docker is not running." >&2
        exit 1
    fi
}

# Regenerates $BATOCERA_SRC/batocera.mk with the current $BR2_JLEVEL on
# every call. This file is how BR2_JLEVEL actually reaches Buildroot's
# .config -- see BUILDING.md's "Controlling build parallelism" section.
# It must be regenerated every run because setup-build-tree.sh's
# `git clean -fdx` wipes it on each re-run of that script.
sync_batocera_mk() {
    echo '$(call add-defconfig,BR2_JLEVEL='"$BR2_JLEVEL"')' > "$BATOCERA_SRC/batocera.mk"
}

# Ensures the BR_DOCKER_VOLUMES=1 named volumes exist and are writable by
# the non-root build container from their very first use. A freshly
# Docker-created named volume is owned by root, and Docker's own
# populate-on-first-use behavior re-populates (and re-roots) it from the
# build image's contents at that mount path for as long as it's "empty" --
# so both a chown AND a real file are needed, in that order, or the
# container's non-root user (docker.mk's `-u $(UID):$(GID)`) still can't
# write to it. Volume names must match docker.mk's own DOCKER_VOL_DL/
# DOCKER_VOL_CCACHE/DOCKER_VOL_OUTPUT (which default to batocera-dl,
# batocera-ccache, batocera-output-$BOARD but are override-able) -- if
# you've overridden those for docker.mk, override them identically here.
seed_docker_volumes() {
    [ "$BR_DOCKER_VOLUMES" = "1" ] || return 0
    local vol
    for vol in "${DOCKER_VOL_DL:-batocera-dl}" "${DOCKER_VOL_CCACHE:-batocera-ccache}" "${DOCKER_VOL_OUTPUT:-batocera-output-$BOARD}"; do
        docker volume create "$vol" >/dev/null
        docker run --rm -v "$vol":/v alpine sh -c "chown $(id -u):$(id -g) /v && touch /v/.keep"
    done
}
