# Overlay Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the word-labels and percentage numbers in both overlays (`circuitsword-quickmenu`'s WIFI/VOLUME/BRIGHTNESS rows, `circuitsword-statusbar`'s BAT/WIFI/VOL/BRT groups) with small hand-authored 5×7 bitmap icons — fully visual, no text/numbers left in either.

**Architecture:** A new shared source file, `qm_icons.c`, added alongside the existing `qm_font.c` in `circuitsword-quickmenu`'s package directory, following the exact same "static bitmap table + one draw function" shape `qm_font.c` already uses for letters. It is compiled into both `circuitsword-quickmenu` and `circuitsword-statusbar` the same way `qm_font.c`/`qm_settings.c` already are (both `.mk` files already reference this directory's sources). A pure helper, `qm_icon_level_from_percent()`, maps a 0-100 reading to a 0-3 icon level; `qm_draw_icon()` looks up and draws the right bitmap for a `(kind, level)` pair.

**Tech Stack:** C (gnu99, no toolkit — same as the rest of both overlay packages), host `cc` for tests (`tests/run-c-tests.sh`), Buildroot `generic-package`.

## Global Constraints

- Design doc: `docs/superpowers/specs/2026-08-10-overlay-icons-design.md` — this plan implements it exactly; do not deviate from its architecture (shared `qm_icons.c`, 5×7 grid, fully visual/no numbers, `QM_ICON_BATTERY_CHARGING` replaces rather than overlays the battery icon) or scope.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, detached HEAD at pinned commit `155c2d8d304cbb53db52e9479dcf683392821d5c`, tag `batocera-43.1`). Every task's file changes happen here and get real git commits in this repo.
- **`batocera-build/scripts/*.sh` (`build-image.sh`, `env.sh`) default `BATOCERA_SRC` to a stale, unpatched checkout** at `batocera-build/build/batocera.linux` inside the main project directory — NOT where this project's development happens. Any command that sources `env.sh` or invokes `make` in the build tree MUST explicitly `export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux` first. `env.sh` itself requires `bash`, not `zsh` — wrap in `bash -c '...'` if the shell is zsh. Do not use `batocera-build/scripts/build-image.sh` for this plan's verification steps — it triggers a multi-hour full image build, which this plan does not need.
- Per Hard Rule #7 in the project's root `CLAUDE.md` (Buildroot incremental-build staleness gotcha): `circuitsword-quickmenu` and `circuitsword-statusbar` are both real compiled packages, so a verification step must force `PKG=circuitsword-quickmenu-rebuild` / `PKG=circuitsword-statusbar-rebuild` (not `-reinstall`, which only applies to plain-copy packages) — a bare full build will not pick up edited-but-already-stamped source in an already-built Docker output volume.
- The main project directory (`/Users/bas/Circuit-Sword Batocera`) has **no git repo**, by deliberate choice. Never run `git init` there. Files under `tests/` and `docs/` in that directory are saved directly, not committed.
- After all build-tree changes are committed (end of Task 2), regenerate the project's reproducible patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- No hardware in CI. Testing is off-device only: host `cc`-compiled C tests (`tests/run-c-tests.sh`) and incremental Buildroot single-package compiles (`bcm2837-pkg PKG=<name>-rebuild`, minutes not hours). On-device validation (icon legibility, whether the 320px alt panel now fits, visual correctness of the hand-drawn shapes) is tracked in the findings log, never claimed as done by this plan.
- Findings log: `docs/superpowers/plans/findings/PHASE4-OVERLAY-ICONS-FINDINGS.md` (created in Task 2).
- Icon bit encoding: identical to `qm_font.c`'s `qm_font_glyphs` table — one `uint8_t` per row, low 5 bits significant, MSB (bit 4) = leftmost column, `QM_GLYPH_W` (5) columns × `QM_GLYPH_H` (7) rows.

---

## Task 1: `qm_icons.c` — icon bitmaps, `qm_draw_icon()`, `qm_icon_level_from_percent()`, host tests

**Files:**
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icons.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Create: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`

**Interfaces:**
- Consumes: `qm_fill_rect(qm_fb *fb, int x, int y, int w, int h, uint32_t color)` — already `extern` in `quickmenu.h`, defined in `qm_font.c`, reused as-is. `QM_GLYPH_W`, `QM_GLYPH_H` — already defined in `quickmenu.h`.
- Produces (all declared in `quickmenu.h`, defined in `qm_icons.c`):
  ```c
  typedef enum {
      QM_ICON_BATTERY,
      QM_ICON_BATTERY_CHARGING,
      QM_ICON_WIFI_ON,
      QM_ICON_WIFI_OFF,
      QM_ICON_VOLUME,
      QM_ICON_BRIGHTNESS,
  } qm_icon_kind;

  void qm_draw_icon(qm_fb *fb, int x, int y, qm_icon_kind kind, int level, int scale, uint32_t color);
  int  qm_icon_width(void);
  int  qm_icon_level_from_percent(int percent);
  ```
  `level` is only meaningful for `QM_ICON_BATTERY`/`QM_ICON_VOLUME`/`QM_ICON_BRIGHTNESS` (0-3, out-of-range values clamp) — ignored for `QM_ICON_WIFI_ON`/`QM_ICON_WIFI_OFF`/`QM_ICON_BATTERY_CHARGING`, which have exactly one bitmap each. Task 2's callers pass `0` for the levelless kinds by convention.

### Step 1: Add the declarations to `quickmenu.h`

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.h`. Immediately after the existing `qm_font.c` block (after the line `uint32_t qm_get_pixel(const qm_fb *fb, int x, int y);`, before the `qm_input.c` comment block), insert:

```c
/* ------------------------------------------------------------------ */
/* qm_icons.c — hand-authored 5x7 bitmap icons, same grid and encoding */
/* as qm_font.c's letters. Pure drawing, no syscalls, host-testable.   */
/* ------------------------------------------------------------------ */
typedef enum {
    QM_ICON_BATTERY,            /* level 0-3: empty/low/half/full */
    QM_ICON_BATTERY_CHARGING,   /* single glyph, level ignored */
    QM_ICON_WIFI_ON,            /* single glyph, level ignored */
    QM_ICON_WIFI_OFF,           /* single glyph, level ignored */
    QM_ICON_VOLUME,             /* level 0-3: mute/low/mid/high */
    QM_ICON_BRIGHTNESS,         /* level 0-3: low/mid/high/max */
} qm_icon_kind;

void qm_draw_icon(qm_fb *fb, int x, int y, qm_icon_kind kind, int level, int scale, uint32_t color);
int  qm_icon_width(void);
/* Pure, no I/O. Clamps percent to 0..100 first, then maps: 0-24 -> 0,
 * 25-49 -> 1, 50-74 -> 2, 75-100 -> 3. */
int  qm_icon_level_from_percent(int percent);
```

### Step 2: Write the failing tests

- [ ] Create `/Users/bas/Circuit-Sword Batocera/tests/test_qm_icons.c`:

```c
/* Host-side unit tests for circuitsword-quickmenu's icon bitmaps
 * (qm_icons.c). Compiled with the host cc against the real source --
 * no Wayland, no evdev, no device needed. Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("qm_icon_level_from_percent boundaries\n");
    {
        check(qm_icon_level_from_percent(0) == 0, "0 -> level 0");
        check(qm_icon_level_from_percent(24) == 0, "24 -> level 0");
        check(qm_icon_level_from_percent(25) == 1, "25 -> level 1");
        check(qm_icon_level_from_percent(49) == 1, "49 -> level 1");
        check(qm_icon_level_from_percent(50) == 2, "50 -> level 2");
        check(qm_icon_level_from_percent(74) == 2, "74 -> level 2");
        check(qm_icon_level_from_percent(75) == 3, "75 -> level 3");
        check(qm_icon_level_from_percent(100) == 3, "100 -> level 3");
        check(qm_icon_level_from_percent(-5) == 0, "negative clamps to 0 -> level 0");
        check(qm_icon_level_from_percent(150) == 3, "over 100 clamps to 100 -> level 3");
    }

    printf("qm_icon_width\n");
    {
        check(qm_icon_width() == QM_GLYPH_W, "icon width matches the font grid width");
    }

    printf("qm_draw_icon draws something for every kind/level combination\n");
    {
        struct { qm_icon_kind kind; int level; const char *name; } cases[] = {
            { QM_ICON_BATTERY, 0, "battery level 0" },
            { QM_ICON_BATTERY, 1, "battery level 1" },
            { QM_ICON_BATTERY, 2, "battery level 2" },
            { QM_ICON_BATTERY, 3, "battery level 3" },
            { QM_ICON_BATTERY_CHARGING, 0, "battery charging" },
            { QM_ICON_WIFI_ON, 0, "wifi on" },
            { QM_ICON_WIFI_OFF, 0, "wifi off" },
            { QM_ICON_VOLUME, 0, "volume level 0" },
            { QM_ICON_VOLUME, 1, "volume level 1" },
            { QM_ICON_VOLUME, 2, "volume level 2" },
            { QM_ICON_VOLUME, 3, "volume level 3" },
            { QM_ICON_BRIGHTNESS, 0, "brightness level 0" },
            { QM_ICON_BRIGHTNESS, 1, "brightness level 1" },
            { QM_ICON_BRIGHTNESS, 2, "brightness level 2" },
            { QM_ICON_BRIGHTNESS, 3, "brightness level 3" },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            qm_fb *fb = make_fb(16, 16);
            qm_draw_icon(fb, 0, 0, cases[i].kind, cases[i].level, 1, QM_RGB(0xFF, 0xFF, 0xFF));
            char msg[64];
            snprintf(msg, sizeof(msg), "%s draws at least one pixel", cases[i].name);
            check(count_nonzero(fb) > 0, msg);
            free_fb(fb);
        }
    }

    printf("qm_draw_icon battery levels are visibly different\n");
    {
        qm_fb *a = make_fb(16, 16), *b = make_fb(16, 16);
        qm_draw_icon(a, 0, 0, QM_ICON_BATTERY, 0, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon(b, 0, 0, QM_ICON_BATTERY, 3, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "empty battery differs from full battery");
        free_fb(a); free_fb(b);
    }

    printf("qm_draw_icon wifi on differs from wifi off\n");
    {
        qm_fb *a = make_fb(16, 16), *b = make_fb(16, 16);
        qm_draw_icon(a, 0, 0, QM_ICON_WIFI_ON, 0, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon(b, 0, 0, QM_ICON_WIFI_OFF, 0, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "wifi-on differs from wifi-off");
        free_fb(a); free_fb(b);
    }

    printf("qm_draw_icon battery differs from battery-charging\n");
    {
        qm_fb *a = make_fb(16, 16), *b = make_fb(16, 16);
        qm_draw_icon(a, 0, 0, QM_ICON_BATTERY, 3, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon(b, 0, 0, QM_ICON_BATTERY_CHARGING, 0, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "battery-full differs from battery-charging");
        free_fb(a); free_fb(b);
    }

    printf("qm_draw_icon scaling\n");
    {
        qm_fb *a = make_fb(32, 32), *b = make_fb(32, 32);
        qm_draw_icon(a, 0, 0, QM_ICON_BATTERY, 3, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon(b, 0, 0, QM_ICON_BATTERY, 3, 2, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(b) == 4 * count_nonzero(a),
              "scale 2 lights exactly 4x the pixels of scale 1");
        free_fb(a); free_fb(b);
    }

    printf("qm_draw_icon unrecognized level clamps instead of crashing\n");
    {
        qm_fb *fb = make_fb(16, 16);
        qm_draw_icon(fb, 0, 0, QM_ICON_VOLUME, -1, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) > 0, "negative level clamps to level 0, draws something, no crash");
        free_fb(fb);
        qm_fb *fb2 = make_fb(16, 16);
        qm_draw_icon(fb2, 0, 0, QM_ICON_VOLUME, 99, 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb2) > 0, "huge level clamps to level 3, draws something, no crash");
        free_fb(fb2);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

### Step 3: Wire the new test into `run-c-tests.sh`

- [ ] Open `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`. After the existing `test_qm_font` block (the first `cc ... test_qm_font.c ...` through its `"$OUT/test_qm_font"` invocation, lines 14-20) and before the `test_qm_settings` block, insert:

```bash
cc -std=gnu99 -O1 -Wall -Wextra -Werror \
   -DQM_NO_MAIN \
   -I"$QM_SRC" \
   "$HERE/test_qm_icons.c" "$QM_SRC/qm_icons.c" "$QM_SRC/qm_font.c" "$QM_SRC/quickmenu.c" \
   -o "$OUT/test_qm_icons"

"$OUT/test_qm_icons"
```

  (`qm_icons.c` only calls `qm_fill_rect` from `qm_font.c`, but `quickmenu.c` is included in the compile line the same way `test_qm_font.c`'s own block already does, for consistency and because `-DQM_NO_MAIN` makes `quickmenu.c` contribute only its pure `qm_render()` half, matching the established pattern — this avoids a second, subtly different compile-line shape in the same script.)

### Step 4: Run the tests to verify they fail

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: compile error (`qm_icons.c` doesn't exist yet / `qm_draw_icon`, `qm_icon_width`, `qm_icon_level_from_percent`, `qm_icon_kind` undeclared).

### Step 5: Implement `qm_icons.c`

- [ ] Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/qm_icons.c`:

```c
/* Pure drawing into a 32bpp XRGB8888 buffer. No syscalls -- this file is
 * compiled by the host compiler in tests/run-c-tests.sh as well as by the
 * Buildroot cross toolchain.
 *
 * Icons: hand-authored 5x7 bitmaps, same grid and bit encoding as
 * qm_font.c's letters (one uint8_t per row, low 5 bits significant, MSB
 * = leftmost column). Shapes are loosely inspired by Batocera/
 * EmulationStation's own status-icon conventions (striped battery body,
 * arced WiFi signal) for visual familiarity -- not a literal
 * reproduction; no ES/theme icon assets are vendored in this repo to
 * copy from. See docs/superpowers/specs/2026-08-10-overlay-icons-design.md. */
#include "quickmenu.h"

static const uint8_t qm_icon_battery[4][QM_GLYPH_H] = {
    /* level 0: empty -- outline only */
    {0x1E,0x12,0x13,0x12,0x12,0x12,0x1E},
    /* level 1: low -- bottom cavity row filled */
    {0x1E,0x12,0x13,0x12,0x12,0x1E,0x1E},
    /* level 2: half -- bottom 3 cavity rows filled */
    {0x1E,0x12,0x13,0x1E,0x1E,0x1E,0x1E},
    /* level 3: full -- all cavity rows filled */
    {0x1E,0x1E,0x1F,0x1E,0x1E,0x1E,0x1E},
};

static const uint8_t qm_icon_battery_charging[QM_GLYPH_H] = {
    0x06,0x04,0x0C,0x1F,0x06,0x04,0x08
};

static const uint8_t qm_icon_wifi_on[QM_GLYPH_H] = {
    0x00,0x0E,0x11,0x04,0x0A,0x00,0x04
};

static const uint8_t qm_icon_wifi_off[QM_GLYPH_H] = {
    0x10,0x0E,0x19,0x04,0x0E,0x02,0x05
};

static const uint8_t qm_icon_volume[4][QM_GLYPH_H] = {
    /* level 0: mute -- speaker cone, no waves */
    {0x00,0x04,0x0C,0x1C,0x0C,0x04,0x00},
    /* level 1: low -- one wave arc */
    {0x00,0x04,0x0C,0x1E,0x0C,0x04,0x00},
    /* level 2: mid -- two wave arcs */
    {0x00,0x04,0x0E,0x1F,0x0E,0x04,0x00},
    /* level 3: high -- three wave arcs */
    {0x00,0x04,0x0F,0x1F,0x0F,0x04,0x00},
};

static const uint8_t qm_icon_brightness[4][QM_GLYPH_H] = {
    /* level 0: low -- center dot only */
    {0x00,0x00,0x00,0x04,0x00,0x00,0x00},
    /* level 1: mid -- + horizontal rays */
    {0x00,0x00,0x00,0x15,0x00,0x00,0x00},
    /* level 2: high -- + vertical rays */
    {0x04,0x00,0x00,0x15,0x00,0x00,0x04},
    /* level 3: max -- + diagonal rays */
    {0x04,0x0A,0x00,0x15,0x00,0x0A,0x04},
};

static int qm_clamp_level(int level)
{
    if (level < 0) return 0;
    if (level > 3) return 3;
    return level;
}

int qm_icon_width(void)
{
    return QM_GLYPH_W;
}

int qm_icon_level_from_percent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent < 25) return 0;
    if (percent < 50) return 1;
    if (percent < 75) return 2;
    return 3;
}

void qm_draw_icon(qm_fb *fb, int x, int y, qm_icon_kind kind, int level, int scale, uint32_t color)
{
    if (scale < 1)
        return;

    const uint8_t *bits;
    switch (kind) {
        case QM_ICON_BATTERY:
            bits = qm_icon_battery[qm_clamp_level(level)];
            break;
        case QM_ICON_BATTERY_CHARGING:
            bits = qm_icon_battery_charging;
            break;
        case QM_ICON_WIFI_ON:
            bits = qm_icon_wifi_on;
            break;
        case QM_ICON_WIFI_OFF:
            bits = qm_icon_wifi_off;
            break;
        case QM_ICON_VOLUME:
            bits = qm_icon_volume[qm_clamp_level(level)];
            break;
        case QM_ICON_BRIGHTNESS:
            bits = qm_icon_brightness[qm_clamp_level(level)];
            break;
        default:
            return;
    }

    for (int row = 0; row < QM_GLYPH_H; row++) {
        uint8_t rowbits = bits[row];
        for (int col = 0; col < QM_GLYPH_W; col++) {
            if (!(rowbits & (1u << (QM_GLYPH_W - 1 - col))))
                continue;
            qm_fill_rect(fb, x + col * scale, y + row * scale,
                         scale, scale, color);
        }
    }
}
```

### Step 6: Run the tests to verify they pass

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: all three binaries (`test_qm_font`, `test_qm_icons`, `test_qm_settings`, `test_sb_render` — `test_qm_icons` is the new one) compile and print `PASSED (0 failures)`.

### Step 7: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/circuitsword-quickmenu/qm_icons.c \
          package/batocera/utils/circuitsword-quickmenu/quickmenu.h
  git commit -m "circuitsword-quickmenu: add qm_icons.c, hand-authored overlay icon bitmaps"
  ```

---

## Task 2: Wire icons into both overlays, update `sb_render.c`'s width comment, Buildroot verification

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh`
- Create: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-OVERLAY-ICONS-FINDINGS.md`

**Interfaces:**
- Consumes (from Task 1, already committed): `qm_icon_kind` enum, `qm_draw_icon()`, `qm_icon_width()`, `qm_icon_level_from_percent()` — all declared in `quickmenu.h`, defined in `qm_icons.c`.

### Step 1: Update `quickmenu.c`'s `qm_render()`

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/quickmenu.c`. Find the per-row loop (currently lines 55-78):

```c
    for (int item = 0; item < QM_ITEM_COUNT; item++) {
        int row_h = line_h + bar_h + 2 * scale;
        if (item == st->selected)
            qm_fill_rect(fb, margin / 2, y - scale,
                         (int)fb->width - margin, row_h, QM_COLOR_SEL_BG);

        uint32_t fg = (item == st->selected) ? QM_COLOR_FG : QM_COLOR_DIM;

        if (item == QM_ITEM_WIFI) {
            snprintf(buf, sizeof(buf), "WIFI: %s", st->wifi_on ? "ON" : "OFF");
            qm_draw_text(fb, margin, y, buf, scale, fg);
        } else if (item == QM_ITEM_VOLUME) {
            qm_format_percent(buf, sizeof(buf), "VOLUME", st->volume);
            qm_draw_text(fb, margin, y, buf, scale, fg);
            qm_draw_bar(fb, margin, y + line_h - 2 * scale, bar_w, bar_h,
                        st->volume);
        } else {
            qm_format_percent(buf, sizeof(buf), "BRIGHTNESS", st->brightness);
            qm_draw_text(fb, margin, y, buf, scale, fg);
            qm_draw_bar(fb, margin, y + line_h - 2 * scale, bar_w, bar_h,
                        st->brightness);
        }
        y += row_h + line_h / 2;
    }
```

  Replace it with:

```c
    for (int item = 0; item < QM_ITEM_COUNT; item++) {
        int row_h = line_h + bar_h + 2 * scale;
        if (item == st->selected)
            qm_fill_rect(fb, margin / 2, y - scale,
                         (int)fb->width - margin, row_h, QM_COLOR_SEL_BG);

        uint32_t fg = (item == st->selected) ? QM_COLOR_FG : QM_COLOR_DIM;

        if (item == QM_ITEM_WIFI) {
            qm_draw_icon(fb, margin, y,
                         st->wifi_on ? QM_ICON_WIFI_ON : QM_ICON_WIFI_OFF,
                         0, scale, fg);
        } else if (item == QM_ITEM_VOLUME) {
            qm_draw_icon(fb, margin, y, QM_ICON_VOLUME,
                         qm_icon_level_from_percent(st->volume), scale, fg);
            qm_draw_bar(fb, margin, y + line_h - 2 * scale, bar_w, bar_h,
                        st->volume);
        } else {
            qm_draw_icon(fb, margin, y, QM_ICON_BRIGHTNESS,
                         qm_icon_level_from_percent(st->brightness), scale, fg);
            qm_draw_bar(fb, margin, y + line_h - 2 * scale, bar_w, bar_h,
                        st->brightness);
        }
        y += row_h + line_h / 2;
    }
```

- [ ] `char buf[48];` (declared earlier in `qm_render()`, currently line 53) is no longer used anywhere in the function once the above edit lands — confirm with `grep -n "buf" quickmenu.c` that its only remaining use, if any, is unrelated, and remove the now-unused declaration if it truly has none left. (The title/hint text draws use `qm_draw_text` directly with string literals, not `buf` — they are untouched by this task and don't need it either.)

- [ ] Find `qm_format_percent()` (currently lines 25-28):
  ```c
  static void qm_format_percent(char *out, size_t n, const char *label, int value)
  {
      snprintf(out, n, "%s: %d%%", label, value);
  }
  ```
  Confirm via `grep -n "qm_format_percent" quickmenu.c` that its only remaining references are its own definition (both call sites were removed in the edit above). Delete the function entirely.

### Step 2: Run the quickmenu-side tests

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: `test_qm_font` (which compiles and exercises `quickmenu.c`'s `qm_render()`) still passes — its existing assertions only check `count_nonzero(fb) > 0` and background-color painting, not text content, so they remain valid against icon-based output. If `test_qm_font.c`'s compile line doesn't already include `qm_icons.c`, add it now (find the line `"$HERE/test_qm_font.c" "$QM_SRC/qm_font.c" "$QM_SRC/quickmenu.c" \` in `run-c-tests.sh` and insert `"$QM_SRC/qm_icons.c" \` before it — `quickmenu.c`'s `qm_render()` now calls `qm_draw_icon`, so this compile line needs the new symbol).

### Step 3: Update `sb_render.c`

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/sb_render.c`. Replace the entire file with:

```c
/* Pure drawing for circuitsword-statusbar: battery, WiFi, volume,
 * brightness laid out left-to-right across a thin top bar. No I/O, no
 * Wayland -- reuses circuitsword-quickmenu's qm_font.c/qm_icons.c
 * primitives and QM_COLOR_* palette so the bar matches the same look as
 * the quickmenu overlay. Host-unit-tested in tests/test_sb_render.c. */
#include "statusbar.h"

void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness,
               int battery_percent, int battery_charging)
{
    const int scale = (fb->width >= 640) ? 3 : 2;
    /* Four fixed-width icons (QM_GLYPH_W px each) replace what used to be
     * variable-width percentage text -- the worst case is now just
     * 4 icons * icon_width * scale plus margin/gaps, far under any panel
     * width this project targets (640px primary, 320px alternate). */
    const int margin = 3 * scale;
    const int gap = 4 * scale;
    const int icon_w = qm_icon_width() * scale;

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

    int y = ((int)fb->height - QM_GLYPH_H * scale) / 2;
    int x = margin;

    qm_draw_icon(fb, x, y,
                 battery_charging ? QM_ICON_BATTERY_CHARGING : QM_ICON_BATTERY,
                 qm_icon_level_from_percent(battery_percent), scale, QM_COLOR_FG);
    x += icon_w + gap;

    qm_draw_icon(fb, x, y, wifi_on ? QM_ICON_WIFI_ON : QM_ICON_WIFI_OFF,
                 0, scale, QM_COLOR_FG);
    x += icon_w + gap;

    qm_draw_icon(fb, x, y, QM_ICON_VOLUME,
                 qm_icon_level_from_percent(volume), scale, QM_COLOR_FG);
    x += icon_w + gap;

    qm_draw_icon(fb, x, y, QM_ICON_BRIGHTNESS,
                 qm_icon_level_from_percent(brightness), scale, QM_COLOR_FG);
}
```

  (`#include <stdio.h>` is dropped — `snprintf` is no longer used anywhere in this file.)

### Step 4: Update `test_sb_render.c`

- [ ] Open `/Users/bas/Circuit-Sword Batocera/tests/test_sb_render.c`. The existing tests already assert behavior, not literal text content (`memcmp` on full pixel buffers, `count_nonzero`, background-color checks) — they remain valid as written **except** the last test block (`"sb_render worst-case values stay within screen width at scale 3"`, currently lines 97-111), whose comment references the old text-width overflow bug that no longer applies with icon-based rendering. Replace that entire block:

```c
    printf("sb_render worst-case values stay within screen width at scale 3\n");
    {
        /* 640-wide panel -> scale 3 (see sb_render.c). Worst-case value
         * combination per the layout-overflow regression: battery at
         * 100% and charging, volume 100%, brightness 100%, wifi on. This
         * previously ran the "BRIGHT 100%" group entirely off the right
         * edge of a 640px-wide framebuffer. */
        qm_fb *fb = make_fb(640, 40);
        sb_render(fb, 1, 100, 100, 100, 1);
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "worst-case render draws something");
        check(rightmost < (int)fb->width,
              "worst-case render's rightmost pixel stays within screen width");
        free_fb(fb);
    }
```

  with:

```c
    printf("sb_render icon groups stay within screen width\n");
    {
        /* Fixed-width icons (unlike the old variable-width percentage
         * text) have no "worst case" value combination -- every reading
         * takes the same on-screen width. Still worth a regression test
         * on the narrower 320px alt panel (scale 2), which the old
         * text-based layout was known not to fit. */
        qm_fb *fb = make_fb(320, 40);
        sb_render(fb, 1, 100, 100, 100, 1);
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "icon render draws something");
        check(rightmost < (int)fb->width,
              "icon render's rightmost pixel stays within the 320px alt panel width");
        free_fb(fb);
    }
```

### Step 5: Update `run-c-tests.sh`'s `test_sb_render` compile line

- [ ] The `test_sb_render` block (currently lines 29-34) compiles `sb_render.c` + `qm_font.c`. `sb_render.c` now also calls `qm_draw_icon`/`qm_icon_width`/`qm_icon_level_from_percent`, defined in `qm_icons.c`. Find:
  ```bash
  cc -std=gnu99 -O1 -Wall -Wextra -Werror \
     -I"$SB_SRC" -I"$QM_SRC" \
     "$HERE/test_sb_render.c" "$SB_SRC/sb_render.c" "$QM_SRC/qm_font.c" \
     -o "$OUT/test_sb_render"
  ```
  and add `qm_icons.c` to the compile line:
  ```bash
  cc -std=gnu99 -O1 -Wall -Wextra -Werror \
     -I"$SB_SRC" -I"$QM_SRC" \
     "$HERE/test_sb_render.c" "$SB_SRC/sb_render.c" "$QM_SRC/qm_font.c" "$QM_SRC/qm_icons.c" \
     -o "$OUT/test_sb_render"
  ```

### Step 6: Run the full host test suite

- [ ] Run:
  ```bash
  bash "/Users/bas/Circuit-Sword Batocera/tests/run-c-tests.sh"
  ```
  Expected: all four binaries (`test_qm_font`, `test_qm_icons`, `test_qm_settings`, `test_sb_render`) compile and print `PASSED (0 failures)`.

### Step 7: Wire `qm_icons.c` into both `.mk` files

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`. Find `CIRCUITSWORD_QUICKMENU_SRCS` (currently lines 19-24):
  ```makefile
  CIRCUITSWORD_QUICKMENU_SRCS = \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/quickmenu.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_font.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_input.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_settings.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_wl.c
  ```
  Add `qm_icons.c` (alphabetically, after `qm_font.c`):
  ```makefile
  CIRCUITSWORD_QUICKMENU_SRCS = \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/quickmenu.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_font.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_icons.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_input.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_settings.c \
  	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_wl.c
  ```

- [ ] Also update the `qm-wl-selftest` build command in the same file (currently lines 58-69), which separately lists `qm_font.c` for its own standalone binary and would fail to link if it ever calls into icon code in the future — check first whether `qm_wl_selftest.c` actually needs icon symbols (`grep -n "qm_draw_icon\|qm_icon_" qm_wl_selftest.c`); if it does not (expected — it's a Wayland-connection self-test, unrelated to rendering content), leave that second build command untouched. Do not add `qm_icons.c` there speculatively.

- [ ] Open `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk`. Find `CIRCUITSWORD_STATUSBAR_SRCS` (currently lines 26-31):
  ```makefile
  CIRCUITSWORD_STATUSBAR_SRCS = \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/statusbar.c \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_render.c \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_wl.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_font.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_settings.c
  ```
  Add `qm_icons.c` from the same shared source directory (`sb_render.c` now calls into it):
  ```makefile
  CIRCUITSWORD_STATUSBAR_SRCS = \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/statusbar.c \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_render.c \
  	$(CIRCUITSWORD_STATUSBAR_PKGDIR)/sb_wl.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_font.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_icons.c \
  	$(CIRCUITSWORD_STATUSBAR_QUICKMENU_SRCDIR)/qm_settings.c
  ```

### Step 8: Incremental Buildroot compile-check for both packages

- [ ] Run (using this project's established incremental single-package pattern — `env.sh` sets `OUTPUT_DIR`/`DL_DIR`; it requires `bash`, not `zsh`):
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild 2>&1 | tail -40
  '
  ```
  Expected: both `wayland-scanner` invocations, then the `cc` invocation compiling `quickmenu.c`, `qm_font.c`, `qm_icons.c`, `qm_input.c`, `qm_settings.c`, `qm_wl.c` together, with no errors, ending in the generated `circuitsword-quickmenu` binary.

- [ ] Run the same pattern for the status bar package:
  ```bash
  bash -c '
  cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-statusbar-rebuild 2>&1 | tail -40
  '
  ```
  Expected: no errors, ending in the generated `circuitsword-statusbar` binary.

### Step 9: Commit

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c \
          package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk \
          package/batocera/utils/circuitsword-statusbar/sb_render.c \
          package/batocera/utils/circuitsword-statusbar/circuitsword-statusbar.mk
  git commit -m "circuitsword-quickmenu, circuitsword-statusbar: render icons instead of text labels"
  ```

### Step 10: Regenerate the patch capture

- [ ] Run:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
- [ ] Verify the new code is present in the regenerated patch:
  ```bash
  grep -c "qm_draw_icon" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  grep -c "QM_ICON_BATTERY_CHARGING" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```
  Expected: both counts greater than 0.

### Step 11: Write the findings log

- [ ] Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE4-OVERLAY-ICONS-FINDINGS.md`:

```markdown
# Overlay Icons: Findings

Implements `docs/superpowers/specs/2026-08-10-overlay-icons-design.md`.
Replaces word-labels and percentage numbers in both overlays
(`circuitsword-quickmenu`'s menu rows, `circuitsword-statusbar`'s bar)
with hand-authored 5x7 bitmap icons -- fully visual, no text/numbers left.

## What was built

- `qm_icons.c` (new, in `circuitsword-quickmenu`'s package directory,
  shared with `circuitsword-statusbar` the same way `qm_font.c`/
  `qm_settings.c` already are): 15 hand-authored 5x7 bitmaps across 6
  icon kinds (battery x4 levels, battery-charging, wifi-on, wifi-off,
  volume x4 levels, brightness x4 levels), `qm_draw_icon()`,
  `qm_icon_width()`, and the pure `qm_icon_level_from_percent()` mapper.
- `quickmenu.c`: WIFI/VOLUME/BRIGHTNESS rows now draw an icon instead of
  a text label; the volume/brightness percentage bar (`qm_draw_bar`) is
  unchanged. Removed the now-dead `qm_format_percent()`.
- `sb_render.c`: all four status-bar groups (battery, wifi, volume,
  brightness) now draw only an icon, no text, no percentage numbers.
  Recomputed the width-budget comment for the new, much narrower
  fixed-width-icon worst case.

## Verified off-device

- `tests/test_qm_icons.c`: level-from-percent boundary mapping (0, 24,
  25, 49, 50, 74, 75, 100, and out-of-range clamping), every one of the
  15 kind/level combinations draws at least one non-background pixel
  (catches an accidentally-all-zero bitmap), battery/wifi/charging
  states are pixel-distinguishable from each other, scaling behaves the
  same way `qm_draw_char`'s already does (4x pixels at scale 2).
- `tests/test_qm_font.c`'s existing `qm_render` tests still pass against
  icon-based output (they only assert "draws something" and background
  color, never text content).
- `tests/test_sb_render.c`'s existing tests still pass; its worst-case-
  width regression test now targets the 320px alt panel (previously the
  known-overflowing case) instead of the old text-based 640px worst
  case, since fixed-width icons have no "worst case" value combination.
- Incremental Buildroot compiles (`PKG=circuitsword-quickmenu-rebuild`
  and `PKG=circuitsword-statusbar-rebuild`) both succeed.
- Patch capture confirmed to contain the new icon code via targeted `grep`.

## Needs on-device validation (not yet done)

- **Legibility**: 5x7 pixel icons are small for pictorial shapes (a real
  risk accepted up front per the design doc, in favor of staying on the
  existing font-sized grid rather than a larger dedicated icon grid) --
  must be seen on the actual DPI panel to judge whether battery/volume/
  brightness levels and wifi on/off are actually distinguishable at a
  normal viewing distance.
- **320px alt panel fit**: plausible that the icon-based status bar now
  fits the alternate 320px panel (previously a known, explicitly-deferred
  overflow with the old text-based layout) -- the new host test confirms
  it fits in a synthetic 320px framebuffer, but this must still be
  confirmed on the real alternate panel hardware, not assumed.
- **Visual correctness of the hand-drawn shapes**: the bitmap art
  (battery outline+fill, lightning bolt, wifi arcs+dot with strike-
  through for off, speaker+waves, sun+rays) was authored by reasoning
  about bit patterns, not by looking at rendered pixels -- confirm each
  shape actually reads as intended once seen on real hardware, and adjust
  the bitmap tables in `qm_icons.c` if any shape is unclear or ambiguous.
```

---
