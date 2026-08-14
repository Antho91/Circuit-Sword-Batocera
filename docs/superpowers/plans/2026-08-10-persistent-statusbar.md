# Persistent In-Game Status Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `circuitsword-statusbar`, a new, permanent, passive Wayland overlay (battery, WiFi, volume, brightness) that stays visible across the top of the screen for the entire time a game is running — never pausing the game, never grabbing input.

**Architecture:** A new standalone C binary (`circuitsword-statusbar`), sharing `circuitsword-quickmenu`'s pure primitives (`qm_font.c`'s bitmap-font renderer, `qm_settings.c`'s WiFi/volume/brightness readers, `quickmenu.h`'s `qm_fb`/color constants) by compiling them directly into the new binary from the neighboring package directory — no shared library, no code duplication. A new `qm_battery_get()` primitive is added to the shared `qm_settings.c` for the one piece of data nothing existing reads yet. The daemon (`rpi-circuitsword.py`) gets a new `statusbar_thread` that launches/kills the binary as `retroarch_running()` (already defined, currently unused since the quick-menu-everywhere change) goes true/false.

**Tech Stack:** C (gnu99, raw `libwayland-client`, `wlr-layer-shell-unstable-v1`), Python 3 (daemon thread), Buildroot package (`generic-package`).

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md` — this plan implements it exactly; do not deviate from its architecture (separate binary, top-anchored, no input grab, no pause) or scope.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, detached HEAD at pinned commit `155c2d8d304cbb53db52e9479dcf683392821d5c`, tag `batocera-43.1`). Every task's file changes happen here and get real git commits in this repo.
- **`batocera-build/scripts/*.sh` (`build-image.sh`, `env.sh`) default `BATOCERA_SRC` to a stale, unpatched checkout** at `batocera-build/build/batocera.linux` inside the main project directory — NOT where this project's development happens. Any command in this plan that sources `env.sh` or invokes `make` in the build tree MUST explicitly `export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux` first, exactly as shown in each task's commands below. Do not use `batocera-build/scripts/build-image.sh` for this plan's verification steps — it triggers a multi-hour full image build, which this plan does not need; use the incremental `bcm2837-pkg PKG=<name>-rebuild` pattern shown in each task instead (same pattern used throughout the prior quickmenu phase).
- The main project directory (`/Users/bas/Circuit-Sword Batocera`) has **no git repo**, by deliberate choice. Never run `git init` there. Files under `tests/` and `docs/` in that directory are saved directly, not committed.
- After all build-tree changes are committed (end of Task 5), regenerate the project's reproducible patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- No hardware in CI. Every task's testing steps are off-device: Python `unittest`, host `cc` compile via `tests/run-c-tests.sh`, and incremental Buildroot single-package compiles (`bcm2837-pkg PKG=<name>-rebuild`, which run inside the Docker build container but take minutes, not the hours a full image build takes). On-device validation is tracked in the findings log (Task 5), never claimed as done by this plan.
- Findings log: `docs/superpowers/plans/findings/PHASE4-STATUSBAR-FINDINGS.md` (created in Task 5).
- Shared-primitive reuse: `circuitsword-statusbar`'s Buildroot package compiles `circuitsword-quickmenu`'s `qm_font.c` and `qm_settings.c` directly from their existing location (`$(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-quickmenu/`) via an absolute path reference in its own `.mk`, the same way `circuitsword-quickmenu.mk` already references its own sources via `CIRCUITSWORD_QUICKMENU_PKGDIR`. No shared library, no new abstraction — this project's established minimal-plumbing convention.

---

## Task 1: Shared primitive — `qm_battery_get()` in `circuitsword-quickmenu`'s `qm_settings.c`

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_settings.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_settings.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`

**Interfaces:**
- Consumes: `qm_clamp(int, int, int)` and `qm_read_int_file(const char *)` — both already `static` in `qm_settings.c`, reused as-is.
- Produces: `int qm_battery_parse(int raw_capacity, const char *status, int *percent, int *charging)` — pure logic, no I/O, 0 on success (writes both out-params) or -1 if `raw_capacity < 0` or `status == NULL`. `int qm_battery_get(int *percent, int *charging)` — the real I/O wrapper, 0 ok / -1 on error, used by Task 3's `statusbar.c`.

- [ ] **Step 1: Write the failing host C test**

Create `/Users/bas/Circuit-Sword Batocera/tests/test_qm_settings.c`:

```c
/* Host-side unit tests for circuitsword-quickmenu's qm_settings.c pure
 * parsing logic. The file-I/O wrappers themselves read real system paths
 * and are not host-testable; qm_battery_parse() is the pure core split
 * out for exactly that reason -- see qm_battery_get(). Run via
 * tests/run-c-tests.sh. */
#include <stdio.h>
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

int main(void)
{
    printf("qm_battery_parse\n");
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(87, "Discharging", &percent, &charging);
        check(rc == 0, "returns 0 on valid input");
        check(percent == 87, "percent passed through");
        check(charging == 0, "Discharging -> charging=0");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(42, "Charging", &percent, &charging);
        check(rc == 0, "returns 0 on valid input");
        check(percent == 42, "percent passed through");
        check(charging == 1, "Charging -> charging=1");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(150, "Discharging", &percent, &charging);
        check(rc == 0, "returns 0 even for an out-of-range raw value");
        check(percent == 100, "clamped to 100");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(-5, "Discharging", &percent, &charging);
        check(rc == 0, "returns 0 even for a negative-but-not-sentinel raw value");
        check(percent == 0, "clamped to 0");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(-1, "Discharging", &percent, &charging);
        check(rc == -1, "raw_capacity == -1 (unreadable capacity file) -> error");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(50, NULL, &percent, &charging);
        check(rc == -1, "NULL status (unreadable status file) -> error");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

Note: `qm_battery_parse` treats `raw_capacity == -1` as the error sentinel (matching `qm_read_int_file`'s own -1-on-failure convention), not "any negative value" — a raw value of exactly -1 means "the file read failed", while other negative values are treated as just an out-of-range reading to clamp. This mirrors how `voltage_to_percent()` in `rpi-circuitsword.py` clamps rather than errors on out-of-range input.

- [ ] **Step 2: Wire the new test into the runner and confirm it fails**

Open `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`. Replace its entire contents with:

```bash
#!/bin/bash
# Host-side unit tests for circuitsword-quickmenu's pure (non-Wayland,
# non-evdev) code. Compiles straight out of the real build tree with the
# host compiler -- no cross toolchain, no Wayland, no device.
set -euo pipefail

SRC="${BATOCERA_SRC:-/Users/bas/batocera-build-wifi/batocera.linux}/package/batocera/utils/circuitsword-quickmenu"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -DQM_NO_MAIN \
   -I"$SRC" \
   "$HERE/test_qm_font.c" "$SRC/qm_font.c" "$SRC/quickmenu.c" \
   -o "$OUT/test_qm_font"

"$OUT/test_qm_font"

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$SRC" \
   "$HERE/test_qm_settings.c" "$SRC/qm_settings.c" \
   -o "$OUT/test_qm_settings"

"$OUT/test_qm_settings"
```

Run: `cd "/Users/bas/Circuit-Sword Batocera" && bash tests/run-c-tests.sh`
Expected: FAILS to compile — `qm_battery_parse` is not yet declared/defined (`error: implicit declaration of function 'qm_battery_parse'` or a linker error).

- [ ] **Step 3: Declare the new functions in `quickmenu.h`**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`, find this block (the existing `qm_settings.c` declarations, currently lines 68-73):

```c
int qm_wifi_get(void);                    /* 0, 1, or -1 on error */
int qm_wifi_set(int enabled);             /* 0 ok, -1 error */
int qm_volume_get(void);                  /* 0..100, or -1 on error */
int qm_volume_set(int percent);           /* 0 ok, -1 error */
int qm_brightness_get(void);              /* 0..100, or -1 on error */
int qm_brightness_set(int percent);       /* 0 ok, -1 error */
```

Replace it with:

```c
int qm_wifi_get(void);                    /* 0, 1, or -1 on error */
int qm_wifi_set(int enabled);             /* 0 ok, -1 error */
int qm_volume_get(void);                  /* 0..100, or -1 on error */
int qm_volume_set(int percent);           /* 0 ok, -1 error */
int qm_brightness_get(void);              /* 0..100, or -1 on error */
int qm_brightness_set(int percent);       /* 0 ok, -1 error */
/* Pure logic (no I/O), host-unit-testable in isolation -- see
 * tests/test_qm_settings.c. Clamps raw_capacity to 0..100; -1 means the
 * error sentinel (capacity file unreadable), not just "out of range". */
int qm_battery_parse(int raw_capacity, const char *status, int *percent, int *charging);
int qm_battery_get(int *percent, int *charging);   /* 0 ok, -1 on error */
```

- [ ] **Step 4: Implement both functions in `qm_settings.c`**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_settings.c`, find the small helper section near the top (after `qm_write_int_file`, before `qm_clamp`) and add a string-reading helper. Replace:

```c
static int qm_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
```

with:

```c
static int qm_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int qm_read_str_file(const char *path, char *buf, size_t n)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    char *got = fgets(buf, (int)n, fp);
    fclose(fp);
    if (got == NULL)
        return -1;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return 0;
}
```

Then, at the end of the file (after the existing `/* ---------------- Brightness ---------------- */` section), append:

```c

/* ---------------- Battery ---------------- */

#define QM_BATTERY_DIR "/sys/class/power_supply/battery"

int qm_battery_parse(int raw_capacity, const char *status, int *percent, int *charging)
{
    if (raw_capacity < 0 || status == NULL)
        return -1;
    *percent = qm_clamp(raw_capacity, 0, 100);
    *charging = (strcmp(status, "Charging") == 0) ? 1 : 0;
    return 0;
}

int qm_battery_get(int *percent, int *charging)
{
    int cap = qm_read_int_file(QM_BATTERY_DIR "/capacity");
    char status[32];
    if (qm_read_str_file(QM_BATTERY_DIR "/status", status, sizeof(status)) != 0)
        return -1;
    return qm_battery_parse(cap, status, percent, charging);
}
```

- [ ] **Step 5: Run the tests to confirm they pass**

Run: `cd "/Users/bas/Circuit-Sword Batocera" && bash tests/run-c-tests.sh`
Expected: both `test_qm_font` and `test_qm_settings` print `PASSED (0 failures)`.

- [ ] **Step 6: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_settings.c \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h
git commit -m "circuitsword-quickmenu: add qm_battery_get for the status bar"
```

(`tests/test_qm_settings.c` and `tests/run-c-tests.sh` live under `/Users/bas/Circuit-Sword Batocera`, which has no git repo — already saved to disk from Steps 1-2, nothing further to commit there.)

---

## Task 2: `circuitsword-statusbar` Buildroot package + top-anchored Wayland surface

**Files:**
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/Config.in`
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk`
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/statusbar.h`
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_wl.c`

**Interfaces:**
- Consumes: `qm_fb`, `QM_RGB`, `QM_COLOR_*` (from `circuitsword-quickmenu/quickmenu.h`, found via an `-I` path into that package's directory).
- Produces: `typedef struct sb_wl sb_wl;`, `sb_wl *sb_wl_open(void)`, `qm_fb *sb_wl_fb(sb_wl *w)`, `int sb_wl_present(sb_wl *w)`, `int sb_wl_pump(sb_wl *w, int timeout_ms)` (no input fd — unlike `qm_wl_pump`, this surface never has input to poll), `void sb_wl_close(sb_wl *w)` — all consumed by Task 3's `statusbar.c`. Also produces `void sb_render(...)`'s *declaration* (defined in Task 3) in the same header, so this task's header is complete for Task 3 to consume without editing it again.

This task has no host-testable step (like `circuitsword-quickmenu`'s own `qm_wl.c`, Wayland connection code has no host unit test in this project — it's verified by successful cross-compilation and, later, on-device). Its testing step is an incremental Buildroot compile-check at the end.

- [ ] **Step 1: Package metadata**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/Config.in`:

```
config BR2_PACKAGE_CIRCUITSWORD_STATUSBAR
	bool "circuitsword-statusbar"
	depends on BR2_PACKAGE_WAYLAND
	select BR2_PACKAGE_WAYLAND_PROTOCOLS
	help
	  Permanent, passive in-game status bar (battery, WiFi, volume,
	  brightness) for the Circuit-Sword. Draws a top-anchored
	  overlay-layer Wayland surface (wlr-layer-shell-unstable-v1) on
	  top of the running emulator via labwc, for as long as a game is
	  running. Never grabs input and never pauses the game -- purely
	  passive. Launched and killed by the rpi-circuitsword.py daemon's
	  statusbar_thread as games start/stop.

comment "circuitsword-statusbar needs wayland"
	depends on !BR2_PACKAGE_WAYLAND
```

- [ ] **Step 2: Shared header**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/statusbar.h`:

```c
#ifndef STATUSBAR_H
#define STATUSBAR_H

/* qm_fb, QM_RGB, QM_COLOR_*, qm_fill_rect/qm_draw_text/qm_text_width, and
 * qm_wifi_get/qm_volume_get/qm_brightness_get/qm_battery_get are all owned
 * by circuitsword-quickmenu and reused here as-is -- found via the -I path
 * into that package's directory set in circuitsword-statusbar.mk. See
 * docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md. */
#include "quickmenu.h"

/* ---------------- sb_wl.c -- top-anchored overlay-layer surface -------- */
typedef struct sb_wl sb_wl;

sb_wl   *sb_wl_open(void);
qm_fb   *sb_wl_fb(sb_wl *w);
int      sb_wl_present(sb_wl *w);
/* No input_fd parameter, unlike qm_wl_pump: this surface never grabs
 * input, so there is nothing to poll besides the Wayland connection
 * itself. Returns 0 on timeout, -1 on connection error or the compositor
 * closing our surface. */
int      sb_wl_pump(sb_wl *w, int timeout_ms);
void     sb_wl_close(sb_wl *w);

/* ---------------- sb_render.c -- pure drawing, host-unit-testable ------ */
void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness,
               int battery_percent, int battery_charging);

#endif /* STATUSBAR_H */
```

- [ ] **Step 3: Top-anchored Wayland surface**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_wl.c`:

```c
/* Wayland overlay-layer output for circuitsword-statusbar.
 *
 * Adapted from circuitsword-quickmenu's qm_wl.c, which proved this
 * mechanism works on real hardware: labwc keeps DRM master the whole
 * time, RetroArch is just another Wayland client, and a
 * zwlr_layer_surface_v1 on the OVERLAY layer draws above even fullscreen
 * clients. The difference here: quickmenu anchors all four edges
 * (full-screen); this anchors only TOP+LEFT+RIGHT (a thin bar stretched
 * to the screen width, fixed height) and requests exclusive_zone 0 (does
 * not reserve screen space or push other surfaces around -- the game
 * underneath is unaffected). No evdev, no keyboard interactivity: this
 * surface is purely passive.
 *
 * Boilerplate sequence follows labwc's own clients/labnag.c and
 * clients/pool-buffer.c, with all cairo/pango/glib/wlroots use removed:
 * this file links only libwayland-client. */
#include "statusbar.h"

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

/* Bar height is our own fixed choice (not inferred from the compositor,
 * since only the TOP edge is anchored -- LEFT+RIGHT stretch width
 * automatically, but height has no opposing anchor to infer from). Chosen
 * to comfortably fit one line of QM_GLYPH_H-tall text at scale 3, the
 * scale sb_render.c uses on this board's 640-wide DPI panel. */
#define SB_BAR_HEIGHT 40
/* Used only if the compositor sends a 0 width despite LEFT+RIGHT anchor,
 * which it should not do. Matches this board's DPI panel (640x480). */
#define SB_FALLBACK_W 640

#define SB_LAYER_NAMESPACE "circuitsword-statusbar"
#define SB_CONFIGURE_ROUNDTRIPS 20

struct sb_wl {
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

static void sb_registry_global(void *data, struct wl_registry *reg,
                               uint32_t name, const char *iface,
                               uint32_t version)
{
    struct sb_wl *w = data;
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

static void sb_registry_global_remove(void *data, struct wl_registry *reg,
                                      uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener sb_registry_listener = {
    .global = sb_registry_global,
    .global_remove = sb_registry_global_remove,
};

/* ---------------- layer surface ---------------- */

static void sb_ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t width, uint32_t height)
{
    struct sb_wl *w = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w->configured)
        return;
    w->width = (width == 0) ? SB_FALLBACK_W : width;
    w->height = (height == 0) ? SB_BAR_HEIGHT : height;
    w->configured = 1;
}

static void sb_ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    ((struct sb_wl *)data)->closed = 1;
}

static const struct zwlr_layer_surface_v1_listener sb_ls_listener = {
    .configure = sb_ls_configure,
    .closed = sb_ls_closed,
};

/* ---------------- shm buffer ---------------- */

static int sb_anon_shm(void)
{
    for (int tries = 0; tries < 100; tries++) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        char name[64];
        snprintf(name, sizeof(name), "/circuitsword-statusbar-%x-%x",
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

static int sb_create_buffer(struct sb_wl *w)
{
    uint32_t stride = w->width * 4;
    size_t size = (size_t)stride * w->height;

    int fd = sb_anon_shm();
    if (fd < 0) {
        fprintf(stderr, "circuitsword-statusbar: shm_open failed (%s)\n",
                strerror(errno));
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        fprintf(stderr, "circuitsword-statusbar: ftruncate failed (%s)\n",
                strerror(errno));
        close(fd);
        return -1;
    }
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "circuitsword-statusbar: mmap failed (%s)\n",
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

sb_wl *sb_wl_open(void)
{
    struct sb_wl *w = calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;

    if (getenv("XDG_RUNTIME_DIR") == NULL)
        setenv("XDG_RUNTIME_DIR", "/var/run", 1);
    const char *disp = getenv("WAYLAND_DISPLAY");
    w->display = wl_display_connect((disp != NULL) ? disp : "wayland-0");
    if (w->display == NULL) {
        fprintf(stderr, "circuitsword-statusbar: cannot connect to the "
                        "compositor (WAYLAND_DISPLAY=%s)\n",
                (disp != NULL) ? disp : "wayland-0");
        free(w);
        return NULL;
    }

    w->registry = wl_display_get_registry(w->display);
    wl_registry_add_listener(w->registry, &sb_registry_listener, w);
    if (wl_display_roundtrip(w->display) < 0) {
        fprintf(stderr, "circuitsword-statusbar: registry roundtrip failed\n");
        sb_wl_close(w);
        return NULL;
    }
    if (w->compositor == NULL || w->shm == NULL || w->layer_shell == NULL) {
        fprintf(stderr, "circuitsword-statusbar: compositor lacks %s%s%s\n",
                (w->compositor == NULL) ? "wl_compositor " : "",
                (w->shm == NULL) ? "wl_shm " : "",
                (w->layer_shell == NULL) ? "zwlr_layer_shell_v1 " : "");
        sb_wl_close(w);
        return NULL;
    }

    w->surface = wl_compositor_create_surface(w->compositor);
    if (w->surface == NULL) {
        sb_wl_close(w);
        return NULL;
    }

    /* Empty input region: this surface takes no pointer or touch input at
     * all (it has no input path whatsoever), so nothing should ever be
     * routed to it and stolen from the game underneath. */
    struct wl_region *empty = wl_compositor_create_region(w->compositor);
    if (empty != NULL) {
        wl_surface_set_input_region(w->surface, empty);
        wl_region_destroy(empty);
    }

    w->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        w->layer_shell, w->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, SB_LAYER_NAMESPACE);
    if (w->layer_surface == NULL) {
        fprintf(stderr, "circuitsword-statusbar: get_layer_surface failed\n");
        sb_wl_close(w);
        return NULL;
    }
    zwlr_layer_surface_v1_add_listener(w->layer_surface, &sb_ls_listener, w);
    /* TOP+LEFT+RIGHT only (no BOTTOM): a bar stretched to the full screen
     * width, anchored to the top edge, fixed height. Width 0 in set_size
     * is valid because LEFT+RIGHT are both anchored (stretches
     * automatically); height must be explicit since only TOP is anchored
     * on that axis. exclusive_zone 0 (unlike quickmenu's -1): this bar
     * does not reserve screen space or push other surfaces around -- it
     * just draws on the overlay layer, on top of whatever is there. */
    zwlr_layer_surface_v1_set_anchor(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(w->layer_surface, 0, SB_BAR_HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(w->layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    wl_surface_commit(w->surface);

    for (int i = 0; i < SB_CONFIGURE_ROUNDTRIPS && !w->configured && !w->closed; i++) {
        if (wl_display_roundtrip(w->display) < 0) {
            fprintf(stderr, "circuitsword-statusbar: roundtrip failed while "
                            "waiting for configure\n");
            sb_wl_close(w);
            return NULL;
        }
    }
    if (!w->configured || w->closed) {
        fprintf(stderr, "circuitsword-statusbar: no layer-surface configure "
                        "(configured=%d closed=%d)\n", w->configured, w->closed);
        sb_wl_close(w);
        return NULL;
    }

    if (sb_create_buffer(w) != 0) {
        sb_wl_close(w);
        return NULL;
    }

    /* The bar strip is opaque -- tell the compositor so it can skip
     * blending what is underneath it. */
    struct wl_region *opaque = wl_compositor_create_region(w->compositor);
    if (opaque != NULL) {
        wl_region_add(opaque, 0, 0, (int32_t)w->width, (int32_t)w->height);
        wl_surface_set_opaque_region(w->surface, opaque);
        wl_region_destroy(opaque);
    }

    return w;
}

qm_fb *sb_wl_fb(sb_wl *w)
{
    return (w == NULL) ? NULL : &w->fb;
}

int sb_wl_present(sb_wl *w)
{
    if (w == NULL || w->buffer == NULL)
        return -1;
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    wl_surface_damage(w->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(w->surface);
    if (wl_display_flush(w->display) < 0 && errno != EAGAIN) {
        fprintf(stderr, "circuitsword-statusbar: display flush failed (%s)\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

int sb_wl_pump(sb_wl *w, int timeout_ms)
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

    struct pollfd pfd;
    pfd.fd = wl_display_get_fd(w->display);
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);

    if (pr > 0 && (pfd.revents & POLLIN)) {
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
    return 0;
}

void sb_wl_close(sb_wl *w)
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

- [ ] **Step 4: Buildroot package definition**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk`:

```makefile
################################################################################
#
# circuitsword-statusbar
#
################################################################################

CIRCUITSWORD_STATUSBAR_VERSION = 1.0
CIRCUITSWORD_STATUSBAR_SOURCE =
CIRCUITSWORD_STATUSBAR_LICENSE = GPL-2.0+
CIRCUITSWORD_STATUSBAR_DEPENDENCIES = host-wayland wayland wayland-protocols

# See circuitsword-quickmenu.mk's own NOTE: deliberately not named
# *_SRCDIR, which Buildroot's package infra reserves for itself.
CIRCUITSWORD_STATUSBAR_PKGDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-statusbar
# qm_font.c, qm_settings.c and quickmenu.h are owned by circuitsword-quickmenu
# and reused here as-is (shared pure primitives, no toolkit, no shared
# library) -- see docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md.
CIRCUITSWORD_QUICKMENU_PKGDIR = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-quickmenu

CIRCUITSWORD_STATUSBAR_SRCS = \
	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/statusbar.c \
	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_render.c \
	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_wl.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_font.c \
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_settings.c

# Same vendored/staging protocol XML sources circuitsword-quickmenu uses --
# the layer-shell XML is vendored once, in circuitsword-quickmenu's own
# package directory, and referenced from here rather than duplicated.
CIRCUITSWORD_STATUSBAR_LAYER_SHELL_XML = $(CIRCUITSWORD_QUICKMENU_PKGDIR)/protocols/wlr-layer-shell-unstable-v1.xml
CIRCUITSWORD_STATUSBAR_XDG_SHELL_XML = $(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml

define CIRCUITSWORD_STATUSBAR_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_STATUSBAR_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_STATUSBAR_LAYER_SHELL_XML) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(CIRCUITSWORD_STATUSBAR_XDG_SHELL_XML) \
		$(@D)/xdg-shell-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(CIRCUITSWORD_STATUSBAR_XDG_SHELL_XML) \
		$(@D)/xdg-shell-protocol.c
	$(TARGET_CONFIGURE_OPTS) $(TARGET_CC) \
		-std=gnu99 -O2 -Wall -Wextra \
		-I$(CIRCUITSWORD_STATUSBAR_PKGDIR) \
		-I$(CIRCUITSWORD_QUICKMENU_PKGDIR) \
		-I$(@D) \
		-I$(STAGING_DIR)/usr/include \
		$(CIRCUITSWORD_STATUSBAR_SRCS) \
		$(@D)/wlr-layer-shell-unstable-v1-protocol.c \
		$(@D)/xdg-shell-protocol.c \
		-o $(@D)/circuitsword-statusbar \
		-L$(STAGING_DIR)/usr/lib -lwayland-client -lrt
endef

define CIRCUITSWORD_STATUSBAR_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 -D $(@D)/circuitsword-statusbar \
		$(TARGET_DIR)/usr/bin/circuitsword-statusbar
endef

$(eval $(generic-package))
```

Note: `statusbar.c` and `sb_render.c` (referenced in `CIRCUITSWORD_STATUSBAR_SRCS` above) don't exist yet — they're created in Task 3. This task's package will not successfully compile until Task 3 lands; that is expected and covered by Task 3's own build-verification step, not this one. This task has no standalone compile-check step for that reason.

- [ ] **Step 5: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-statusbar/Config.in \
        package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk \
        package/batocera/utils/circuitsword-statusbar/statusbar.h \
        package/batocera/utils/circuitsword-statusbar/sb_wl.c
git commit -m "circuitsword-statusbar: new package skeleton + top-anchored Wayland surface"
```

---

## Task 3: Status bar rendering + binary entry point

**Files:**
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/statusbar.c`
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`

**Interfaces:**
- Consumes: everything from Task 2's `statusbar.h` (`sb_wl_open`/`sb_wl_fb`/`sb_wl_present`/`sb_wl_pump`/`sb_wl_close`), and from `circuitsword-quickmenu/quickmenu.h` (`qm_fb`, `qm_fill_rect`, `qm_draw_text`, `qm_text_width`, `QM_COLOR_BG`, `QM_COLOR_FG`, `QM_GLYPH_H`, `qm_wifi_get`, `qm_volume_get`, `qm_brightness_get`, `qm_battery_get` — the last added in Task 1).
- Produces: `void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness, int battery_percent, int battery_charging)` (declared in Task 2's `statusbar.h`, defined here) and the final `circuitsword-statusbar` executable (`main()` in `statusbar.c`).

- [ ] **Step 1: Write the failing host C test for the render function**

Create `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`:

```c
/* Host-side unit tests for circuitsword-statusbar's pure drawing code
 * (sb_render.c). Compiled with the host cc against the real source files
 * -- no Wayland, no device. Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "statusbar.h"

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
    printf("sb_render basic\n");
    {
        qm_fb *fb = make_fb(640, 40);
        sb_render(fb, 1, 50, 70, 87, 0);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");
        free_fb(fb);
    }

    printf("sb_render charging vs not\n");
    {
        qm_fb *a = make_fb(640, 40), *b = make_fb(640, 40);
        sb_render(a, 1, 50, 70, 87, 0);
        sb_render(b, 1, 50, 70, 87, 1);
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "charging indicator changes pixel output");
        free_fb(a); free_fb(b);
    }

    printf("sb_render wifi on vs off\n");
    {
        qm_fb *a = make_fb(640, 40), *b = make_fb(640, 40);
        sb_render(a, 1, 50, 70, 87, 0);
        sb_render(b, 0, 50, 70, 87, 0);
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "wifi state changes pixel output");
        free_fb(a); free_fb(b);
    }

    printf("sb_render extreme values do not crash\n");
    {
        qm_fb *fb = make_fb(320, 40);
        sb_render(fb, 0, 0, 0, 0, 0);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "zeroed state still paints bg");
        sb_render(fb, 1, 100, 100, 100, 1);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "maxed state still paints bg");
        free_fb(fb);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Wire it into the test runner and confirm it fails**

Replace `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh` with:

```bash
#!/bin/bash
# Host-side unit tests for circuitsword-quickmenu and circuitsword-statusbar's
# pure (non-Wayland, non-evdev) code. Compiles straight out of the real build
# tree with the host compiler -- no cross toolchain, no Wayland, no device.
set -euo pipefail

BASE="${BATOCERA_SRC:-/Users/bas/batocera-build-wifi/batocera.linux}/package/batocera/utils"
QM_SRC="$BASE/circuitsword-quickmenu"
SB_SRC="$BASE/circuitsword-statusbar"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -DQM_NO_MAIN \
   -I"$QM_SRC" \
   "$HERE/test_qm_font.c" "$QM_SRC/qm_font.c" "$QM_SRC/quickmenu.c" \
   -o "$OUT/test_qm_font"

"$OUT/test_qm_font"

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$QM_SRC" \
   "$HERE/test_qm_settings.c" "$QM_SRC/qm_settings.c" \
   -o "$OUT/test_qm_settings"

"$OUT/test_qm_settings"

cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -I"$SB_SRC" -I"$QM_SRC" \
   "$HERE/test_sb_render.c" "$SB_SRC/sb_render.c" "$QM_SRC/qm_font.c" \
   -o "$OUT/test_sb_render"

"$OUT/test_sb_render"
```

Run: `cd "/Users/bas/Circuit-Sword Batocera" && bash tests/run-c-tests.sh`
Expected: FAILS — `sb_render.c` does not exist yet.

- [ ] **Step 3: Implement `sb_render.c`**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`:

```c
/* Pure drawing for circuitsword-statusbar: battery, WiFi, volume,
 * brightness laid out left-to-right across a thin top bar. No I/O, no
 * Wayland -- reuses circuitsword-quickmenu's qm_font.c primitives and
 * QM_COLOR_* palette so the bar matches the same look as the quickmenu
 * overlay. Host-unit-tested in tests/test_sb_render.c. */
#include "statusbar.h"

#include <stdio.h>

void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness,
               int battery_percent, int battery_charging)
{
    const int scale = (fb->width >= 640) ? 3 : 2;
    const int margin = 4 * scale;
    const int gap = margin * 3;

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

    int y = ((int)fb->height - QM_GLYPH_H * scale) / 2;
    int x = margin;
    char buf[32];

    snprintf(buf, sizeof(buf), "BAT %d%%%s", battery_percent,
             battery_charging ? "+" : "");
    qm_draw_text(fb, x, y, buf, scale, QM_COLOR_FG);
    x += qm_text_width(buf, scale) + gap;

    snprintf(buf, sizeof(buf), "WIFI %s", wifi_on ? "ON" : "OFF");
    qm_draw_text(fb, x, y, buf, scale, QM_COLOR_FG);
    x += qm_text_width(buf, scale) + gap;

    snprintf(buf, sizeof(buf), "VOL %d%%", volume);
    qm_draw_text(fb, x, y, buf, scale, QM_COLOR_FG);
    x += qm_text_width(buf, scale) + gap;

    snprintf(buf, sizeof(buf), "BRIGHT %d%%", brightness);
    qm_draw_text(fb, x, y, buf, scale, QM_COLOR_FG);
}
```

- [ ] **Step 4: Run the tests to confirm they pass**

Run: `cd "/Users/bas/Circuit-Sword Batocera" && bash tests/run-c-tests.sh`
Expected: `test_qm_font`, `test_qm_settings`, and `test_sb_render` all print `PASSED (0 failures)`.

- [ ] **Step 5: Implement the binary entry point**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/statusbar.c`:

```c
/* circuitsword-statusbar: permanent, passive top bar for the Circuit-Sword.
 *
 * Launched by rpi-circuitsword.py's statusbar_thread whenever a game is
 * running, killed when it exits (see statusbar_tick() in that file).
 * Draws battery/WiFi/volume/brightness on a top-anchored Wayland
 * overlay-layer surface. Never grabs input, never pauses anything -- the
 * game underneath stays fully playable the entire time this runs.
 *
 * Exit codes: 0 = normal (SIGTERM/SIGINT handled, or the compositor
 * closed our surface). 3 = could not open the Wayland surface at all
 * (nothing was ever drawn, no visible glitch). 4 = first present failed.
 *
 * See docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md. */
#include "statusbar.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* Poll/redraw-on-change cadence. None of battery/WiFi/volume/brightness
 * change fast enough to need anything tighter -- unlike quickmenu's
 * input-driven 100ms pump, there is no interactivity to be responsive to. */
#define SB_POLL_INTERVAL_MS 2000

static volatile sig_atomic_t sb_quit = 0;

static void sb_on_signal(int signum)
{
    (void)signum;
    sb_quit = 1;
}

int main(void)
{
    struct sigaction sa;
    sa.sa_handler = sb_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sb_wl *wl = sb_wl_open();
    if (wl == NULL) {
        fprintf(stderr, "circuitsword-statusbar: no overlay surface, aborting\n");
        return 3;
    }

    int wifi_on = qm_wifi_get();
    if (wifi_on < 0) wifi_on = 0;
    int volume = qm_volume_get();
    if (volume < 0) volume = 0;
    int brightness = qm_brightness_get();
    if (brightness < 0) brightness = 0;
    int battery_percent = 50, battery_charging = 0;
    qm_battery_get(&battery_percent, &battery_charging);

    qm_fb *fb = sb_wl_fb(wl);
    sb_render(fb, wifi_on, volume, brightness, battery_percent, battery_charging);
    if (sb_wl_present(wl) != 0) {
        sb_wl_close(wl);
        return 4;
    }

    while (!sb_quit) {
        int pr = sb_wl_pump(wl, SB_POLL_INTERVAL_MS);
        if (pr < 0)
            break;   /* compositor closed us or connection lost */

        /* Hold last-known-good on any read failure, same convention as
         * rpi-circuitsword.py's own read_battery_percent(). */
        int new_wifi = qm_wifi_get();
        if (new_wifi < 0) new_wifi = wifi_on;
        int new_volume = qm_volume_get();
        if (new_volume < 0) new_volume = volume;
        int new_brightness = qm_brightness_get();
        if (new_brightness < 0) new_brightness = brightness;
        int new_battery_percent = battery_percent, new_battery_charging = battery_charging;
        qm_battery_get(&new_battery_percent, &new_battery_charging);

        int dirty = (new_wifi != wifi_on) || (new_volume != volume) ||
                    (new_brightness != brightness) ||
                    (new_battery_percent != battery_percent) ||
                    (new_battery_charging != battery_charging);

        wifi_on = new_wifi;
        volume = new_volume;
        brightness = new_brightness;
        battery_percent = new_battery_percent;
        battery_charging = new_battery_charging;

        if (dirty && !sb_quit) {
            sb_render(fb, wifi_on, volume, brightness, battery_percent, battery_charging);
            sb_wl_present(wl);
        }
    }

    sb_wl_close(wl);
    return 0;
}
```

- [ ] **Step 6: Incremental Buildroot compile-check (off-device, minutes not hours)**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-statusbar-rebuild 2>&1 | tail -40
```

Expected: the package's `BUILD_CMDS` run to completion (both `wayland-scanner` invocations, then the single `cc` invocation compiling `statusbar.c`, `sb_render.c`, `sb_wl.c`, `qm_font.c`, and `qm_settings.c` together) with no errors, ending in the generated `circuitsword-statusbar` binary. If `PKG=circuitsword-statusbar-rebuild` reports the package as unknown, the board's cached `.config` predates Task 5's Config.in wiring — that is expected at this point in the plan; re-run this exact check as part of Task 5 Step 3 instead, after the package is wired into the Config.in tree, and treat this step as informational only if it can't yet resolve the package name.

- [ ] **Step 7: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-statusbar/sb_render.c \
        package/batocera/utils/circuitsword-statusbar/statusbar.c
git commit -m "circuitsword-statusbar: render function + binary entry point"
```

---

## Task 4: Daemon integration — `statusbar_thread`

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`

**Interfaces:**
- Consumes: `retroarch_running() -> bool` (already defined, currently unused since the quickmenu-everywhere-restyle change), `QUICKMENU_ENV` (existing `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` dict, reused as-is — the status bar is a Wayland client with the same environment needs).
- Produces: `STATUSBAR_BIN = "/usr/bin/circuitsword-statusbar"`, `STATUSBAR_POLL_INTERVAL_S = 1`, `statusbar_tick(proc)` (one decision cycle, pure side-effect function taking/returning a `subprocess.Popen`-or-`None`, unit-tested directly), `statusbar_thread(stop_event)` (the thread entry point registered in `main()`).

- [ ] **Step 1: Write the failing tests**

Open `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`. Add this new test class after `TestSessionPausesAndResumesWhenReachable` (before the `if __name__ == "__main__":` line at the end):

```python
class TestStatusbarTick(unittest.TestCase):
    """statusbar_tick() is the single-decision core statusbar_thread loops
    on: start the bar if a game just started and nothing is tracked, stop
    it if the game just exited, self-heal (relaunch) if the tracked
    process is found dead while a game is still running, and otherwise
    leave an already-running bar untouched."""

    def setUp(self):
        self.real_running = cs.retroarch_running
        self.real_popen = cs.subprocess.Popen
        self.addCleanup(setattr, cs, "retroarch_running", self.real_running)
        self.addCleanup(setattr, cs.subprocess, "Popen", self.real_popen)

    def test_launches_when_game_starts_and_nothing_tracked(self):
        cs.retroarch_running = lambda: True
        launched = []

        class FakeProc:
            def poll(self):
                return None   # still running

        def fake_popen(args, env=None):
            launched.append(args)
            return FakeProc()

        cs.subprocess.Popen = fake_popen
        proc = cs.statusbar_tick(None)
        self.assertEqual(launched, [[cs.STATUSBAR_BIN]])
        self.assertIsNotNone(proc)

    def test_keeps_running_process_untouched_while_game_still_running(self):
        cs.retroarch_running = lambda: True

        class FakeProc:
            def __init__(self):
                self.terminated = False
            def poll(self):
                return None   # still alive
            def terminate(self):
                self.terminated = True

        def must_not_launch(*a, **kw):
            raise AssertionError("must not relaunch an already-running process")

        cs.subprocess.Popen = must_not_launch
        existing = FakeProc()
        proc = cs.statusbar_tick(existing)
        self.assertIs(proc, existing)
        self.assertFalse(existing.terminated)

    def test_stops_when_game_exits(self):
        cs.retroarch_running = lambda: False

        class FakeProc:
            def __init__(self):
                self.terminated = False
                self.killed = False
            def poll(self):
                return None   # still alive until terminated
            def terminate(self):
                self.terminated = True
            def wait(self, timeout=None):
                pass
            def kill(self):
                self.killed = True

        existing = FakeProc()
        proc = cs.statusbar_tick(existing)
        self.assertIsNone(proc)
        self.assertTrue(existing.terminated)
        self.assertFalse(existing.killed, "graceful terminate should be enough here")

    def test_relaunches_if_process_found_dead_while_game_still_running(self):
        cs.retroarch_running = lambda: True

        class DeadProc:
            def poll(self):
                return 1   # exited with an error

        launched = []

        class FakeProc:
            def poll(self):
                return None

        def fake_popen(args, env=None):
            launched.append(args)
            return FakeProc()

        cs.subprocess.Popen = fake_popen
        proc = cs.statusbar_tick(DeadProc())
        self.assertEqual(launched, [[cs.STATUSBAR_BIN]],
                          "a dead process while the game is still running must self-heal")
        self.assertIsNotNone(proc)

    def test_stays_none_when_no_game_and_nothing_tracked(self):
        cs.retroarch_running = lambda: False

        def must_not_launch(*a, **kw):
            raise AssertionError("must not launch when no game is running")

        cs.subprocess.Popen = must_not_launch
        proc = cs.statusbar_tick(None)
        self.assertIsNone(proc)
```

- [ ] **Step 2: Run the tests to confirm they fail**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py
```

Expected: `AttributeError: module 'rpi_circuitsword' has no attribute 'statusbar_tick'` (or `STATUSBAR_BIN`).

- [ ] **Step 3: Implement `statusbar_tick` and `statusbar_thread`**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`, add this new section immediately after the existing `quickmenu_thread` function (i.e. right before the `# ============================================================` / `# Entry point.` section header that currently follows it):

```python
# ============================================================
# Persistent in-game status bar (Phase 4 follow-up).
#
# Unlike the quick menu, this is passive: it never pauses RetroArch and
# never grabs input. statusbar_tick() is the single decision this thread
# makes on every poll -- start/stop/keep circuitsword-statusbar based on
# whether a game is currently running -- factored out as its own function
# (mirroring how run_quickmenu_session() is the testable unit for the
# quickmenu thread) so it is directly unit-testable without a real
# process or a real Wayland compositor. See
# docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md.
# ============================================================
STATUSBAR_BIN = "/usr/bin/circuitsword-statusbar"
STATUSBAR_POLL_INTERVAL_S = 1


def statusbar_tick(proc):
    """One decision cycle. `proc` is the currently tracked Popen object, or
    None. Returns the (possibly new) tracked Popen object, or None if
    nothing should be tracked anymore."""
    running = retroarch_running()
    alive = proc is not None and proc.poll() is None

    if running and not alive:
        try:
            env = dict(os.environ)
            env.update(QUICKMENU_ENV)
            proc = subprocess.Popen([STATUSBAR_BIN], env=env)
            print("[rpi-circuitsword] statusbar: started", file=sys.stderr)
        except OSError as e:
            print(f"[rpi-circuitsword] statusbar: launch failed: {e}",
                  file=sys.stderr)
            proc = None
    elif not running and alive:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=1)
        print("[rpi-circuitsword] statusbar: stopped", file=sys.stderr)
        proc = None

    return proc


def statusbar_thread(stop_event: threading.Event):
    proc = None
    while not stop_event.is_set():
        proc = statusbar_tick(proc)
        stop_event.wait(STATUSBAR_POLL_INTERVAL_S)

    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()

```

- [ ] **Step 4: Register the new thread in `main()`**

In the same file, in `main()`, replace:

```python
    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
    ]
```

with:

```python
    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
        threading.Thread(target=statusbar_thread, args=(stop_event,), name="statusbar", daemon=True),
    ]
```

- [ ] **Step 5: Run the tests to confirm they pass**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py
```

Expected: all tests PASS, including the five new `TestStatusbarTick` cases, and everything pre-existing unaffected.

- [ ] **Step 6: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: add statusbar_thread to launch/kill circuitsword-statusbar"
```

---

## Task 5: Wire the package into the build, verify, capture patch, write findings

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/Config.in`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/core/batocera-system/Config.in`
- Modify: `/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch`
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-STATUSBAR-FINDINGS.md`

**Interfaces:**
- Consumes: the four commits from Tasks 1-4 (already in `batocera.linux`'s git history at this point).
- Produces: nothing consumed by later tasks — this is the last task in the plan.

- [ ] **Step 1: Register the package's `Config.in` in the root menu**

In `/Users/bas/batocera-build-wifi/batocera.linux/Config.in`, find this line (currently line 138):

```
    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-quickmenu/Config.in"
```

Add immediately after it:

```
    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-statusbar/Config.in"
```

- [ ] **Step 2: Select the package for the bcm2837 target**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/core/batocera-system/Config.in`, find this line (currently line 349):

```
	select BR2_PACKAGE_CIRCUITSWORD_QUICKMENU	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837
```

Add immediately after it:

```
	select BR2_PACKAGE_CIRCUITSWORD_STATUSBAR	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837
```

- [ ] **Step 3: Incremental Buildroot compile-check — both packages**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
source ./env.sh
cd "$BATOCERA_SRC"
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-statusbar-rebuild 2>&1 | tail -40
make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -40
```

Expected: both commands complete with no errors. The second command re-verifies `circuitsword-quickmenu` itself still compiles cleanly after Task 1's `qm_settings.c`/`quickmenu.h` changes (it shares those files). If the first command still reports the package as unknown, the board's cached Buildroot `.config` needs regenerating — re-run the full `$(BOARD)-config` step (`make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-config`) once, then retry the `-rebuild` targets above.

- [ ] **Step 4: Regenerate the patch capture**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

- [ ] **Step 5: Verify all four changes landed in the regenerated patch**

```bash
grep -c "qm_battery_parse" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "circuitsword-statusbar" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "statusbar_tick" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
grep -c "BR2_PACKAGE_CIRCUITSWORD_STATUSBAR" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Expected: all four print a number `>= 1`. If any prints `0`, the corresponding task's commit is missing from history or from the diff range — stop and investigate before proceeding.

- [ ] **Step 6: Run the full off-device test suite one more time, end to end**

```bash
cd "/Users/bas/Circuit-Sword Batocera"
python3 tests/test_quickmenu_logic.py
bash tests/run-c-tests.sh
```

Expected: both PASS with zero failures.

- [ ] **Step 7: Write the findings log**

Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-STATUSBAR-FINDINGS.md`:

```markdown
# Persistent In-Game Status Bar: Findings

Implements `docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md`,
a follow-up to the Phase 4 quick-menu work. Restores a capability the
original manufacturer firmware (`github.com/kiteretro/Circuit-Sword`,
DispmanX-based) had and this project's own RetroPie-based rewrite lost
when it moved to 64-bit KMS.

## What was built

- `circuitsword-quickmenu`'s `qm_settings.c`/`quickmenu.h`: new
  `qm_battery_get()` (and its pure core, `qm_battery_parse()`), reading
  `/sys/class/power_supply/battery/{capacity,status}` -- the same sysfs
  path confirmed working live via SSH during this session's earlier
  charge-icon debugging.
- New `circuitsword-statusbar` Buildroot package: a second, independent
  Wayland client sharing `circuitsword-quickmenu`'s `qm_font.c` and
  `qm_settings.c` directly (compiled from the neighboring package
  directory, no shared library). Draws a top-anchored (not full-screen)
  `wlr-layer-shell-unstable-v1` overlay-layer surface -- battery, WiFi,
  volume, brightness, left to right -- with `exclusive_zone 0` and no
  input handling at all: purely passive, never affects the running game.
- `rpi-circuitsword.py`: new `statusbar_thread`/`statusbar_tick()`, using
  the already-existing `retroarch_running()` (unused since the
  quickmenu-everywhere-restyle change) to launch `circuitsword-statusbar`
  when a game starts, kill it when the game exits, and self-heal
  (relaunch) if the tracked process is found dead mid-session.

## Verified off-device

- `tests/test_qm_settings.c`: `qm_battery_parse()`'s clamping and
  error-sentinel logic.
- `tests/test_sb_render.c`: `sb_render()` paints a background and visibly
  changes output for charging-state and WiFi-state differences; extreme
  (0 and 100) values don't crash.
- `tests/test_quickmenu_logic.py`'s new `TestStatusbarTick`: launches on
  game-start, stays untouched while already running, stops on game-exit,
  self-heals if found dead mid-session, and never launches when nothing
  is running -- five cases, all passing.
- Incremental Buildroot compiles (`bcm2837-pkg PKG=circuitsword-statusbar-rebuild`
  and `PKG=circuitsword-quickmenu-rebuild`) both succeed: the new package
  compiles cleanly against real cross-toolchain headers and links, and
  `circuitsword-quickmenu` still compiles cleanly after sharing its files
  with the new package.
- Patch capture confirmed to contain all four changes via targeted `grep`.

## Needs on-device validation (not yet done)

- The top-anchored overlay-layer surface actually composites above a
  running fullscreen RetroArch client -- related to, but not identical
  to, the full-screen-anchored case `circuitsword-quickmenu` already
  proved; must be confirmed separately.
- Visual layout/sizing/legibility of the bar on the real DPI panel (the
  `SB_BAR_HEIGHT`/scale choices in `sb_wl.c`/`sb_render.c` are reasonable
  estimates, not yet seen on hardware).
- Z-ordering when `circuitsword-quickmenu` opens on top of an
  already-visible status bar -- both are overlay-layer clients; expected
  to just work via quickmenu's full-screen occlusion, but unverified.
- WiFi connected/disconnected icon accuracy against real network state
  changes.
- Any visible flicker on the 2-second redraw-on-change cadence during
  actual gameplay.
- `statusbar_thread`'s 1-second poll latency for start/stop feels
  imperceptible in design but hasn't been felt on real hardware.
```

- [ ] **Step 8: Commit the Config.in wiring**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add Config.in package/batocera/core/batocera-system/Config.in
git commit -m "circuitsword-statusbar: wire into the bcm2837 target"
```

(The patch file and findings log both live under `/Users/bas/Circuit-Sword Batocera`, which has no git repo by deliberate project choice — already saved to disk from Steps 4 and 7, nothing further to commit there.)

---

## Out of scope (carried over from the design doc)

- WiFi signal-strength bars — only connected/disconnected shown.
- A mute icon.
- Any auto-hide/fade behavior — the bar is unconditionally visible for the entire duration a game is running.
- Showing the bar inside EmulationStation itself, outside a running game.
- Any change to `circuitsword-quickmenu`'s own menu items, navigation, or interaction model.
- Building and flashing a full image / on-device testing — tracked as open items in the findings log for a future session, the same way the quickmenu-everywhere-restyle plan handled it.
