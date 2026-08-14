# Overlay FreeType Text Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace circuitsword-quickmenu's crude 5x7 bitmap font with real FreeType-rendered `Cabin-Regular.ttf` text everywhere text appears, plus main-screen state labels and clearer Joystick submenu row names/hints.

**Architecture:** A new `qm_ttf.c` wraps FreeType (already present on target as a shared library, no new heavyweight dependency), rendering glyphs to alpha-coverage bitmaps blitted via the existing `qm_alpha_blend()` primitive. Falls back to the existing bitmap font (`qm_font.c`, kept unchanged) if FreeType init fails.

**Tech Stack:** FreeType2 (C library, already staged in the Buildroot sysroot), reuses existing `qm_alpha_blend`/`qm_fill_rect` primitives.

## Global Constraints

- FreeType text replaces the bitmap font EVERYWHERE text appears in the overlay (hint line, new state labels, all submenu content). The title wordmark stays the existing baked SVG icon (`QM_ICON_TITLE`) — unchanged, out of scope.
- `qm_font.c` is NOT deleted or modified — it becomes the silent fallback if FreeType is unavailable at runtime.
- Font file: `package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf` — already extracted and committed to the repo as part of this plan's Task 1 (a real, valid TTF file, SIL Open Font License, Copyright 2018 The Cabin Project Authors — same font Batocera's own es-theme-carbon ES theme uses).
- Installed target path: `/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf`.
- Per CLAUDE.md Hard Rule #7: `circuitsword-quickmenu` is a real compiled package — verify every task via `PKG=circuitsword-quickmenu-rebuild` + direct Docker-volume inspection, never trust a clean build log alone.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (detached HEAD, currently at the tip after today's A/B-mapping revert and volume-crash fix commits — read `git log --oneline -5` to confirm the exact current HEAD before starting, since this plan follows several out-of-band fixes made directly in this session, not through SDD). Buildroot commands use the absolute `env.sh` path:
  ```bash
  bash -c '
    export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
    source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
    cd "$BATOCERA_SRC"
    make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
  ' 2>&1 | tail -40
  ```
- Patch capture regeneration, exact command (fixed project-wide base commit):
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```

---

### Task 1: FreeType infrastructure (qm_ttf.c) + Buildroot wiring

**Files:**
- Create: `package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf` (already extracted, see below — verify it's present, don't re-extract)
- Create: `package/batocera/utils/circuitsword-quickmenu/qm_ttf.c`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk`

**Interfaces:**
- Produces: `int qm_ttf_init(void)`, `void qm_draw_text_ttf(qm_fb *fb, int x, int y, const char *text, int px_size, uint32_t color)`, `int qm_ttf_text_width(const char *text, int px_size)` — Task 2 wires these into every existing text-drawing call site.

- [ ] **Step 1: Confirm the font file is present**

`package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf` should already exist (103092 bytes, a real TrueType font, Copyright 2018 The Cabin Project Authors — extracted from the currently-built image's es-theme-carbon installation as part of this plan's setup). Run `file package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf` and confirm it reports "TrueType Font data". If missing, extract it fresh:
```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine cat /bcm2837/target/usr/share/emulationstation/themes/es-theme-carbon/art/fonts/Cabin-Regular.ttf > package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf
```

- [ ] **Step 2: Add declarations to `quickmenu.h`**

Read the file fresh. Add a new section after the `qm_font.c` section (before the `qm_icons.c` section):

```c
/* ------------------------------------------------------------------ */
/* qm_ttf.c -- real FreeType-rendered text, replaces qm_font.c's       */
/* bitmap font everywhere text appears in this overlay. Falls back to  */
/* qm_font.c automatically if FreeType init/font load ever fails --    */
/* callers never need to check availability themselves.                */
/* ------------------------------------------------------------------ */
#define QM_TTF_FONT_PATH "/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf"

/* Call once at startup. Returns 0 on success, -1 on failure (logs the
 * reason to stderr either way). On failure, qm_draw_text_ttf() and
 * qm_ttf_text_width() transparently fall back to qm_font.c's
 * qm_draw_text()/qm_text_width() -- callers don't need to branch. */
int qm_ttf_init(void);

/* Renders `text` at `px_size` pixels tall, alpha-blending each glyph
 * into `fb` via the same qm_alpha_blend() primitive qm_icons.c uses.
 * `color` is the fill color (QM_RGB/QM_ARGB), same convention as
 * qm_draw_icon_rgba(). Falls back to qm_draw_text() (at a scale
 * approximating px_size / QM_GLYPH_H) if FreeType is unavailable. */
void qm_draw_text_ttf(qm_fb *fb, int x, int y, const char *text, int px_size, uint32_t color);

/* Pixel width `text` would occupy at `px_size`, for centering/right-
 * alignment. Falls back to qm_text_width() (scaled) if FreeType is
 * unavailable. */
int qm_ttf_text_width(const char *text, int px_size);
```

- [ ] **Step 3: Write `qm_ttf.c`**

Create `package/batocera/utils/circuitsword-quickmenu/qm_ttf.c`:

```c
/* Real FreeType-rendered text for circuitsword-quickmenu. Falls back to
 * qm_font.c's bitmap font if FreeType init or font loading fails -- the
 * overlay must never crash or go silent just because text rendering
 * degraded. */
#include "quickmenu.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>
#include <string.h>

static FT_Library qm_ft_library;
static FT_Face qm_ft_face;
static int qm_ttf_available = 0;

int qm_ttf_init(void)
{
    if (FT_Init_FreeType(&qm_ft_library) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: FT_Init_FreeType failed, "
                        "falling back to bitmap font\n");
        return -1;
    }
    if (FT_New_Face(qm_ft_library, QM_TTF_FONT_PATH, 0, &qm_ft_face) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: failed to load %s, "
                        "falling back to bitmap font\n", QM_TTF_FONT_PATH);
        FT_Done_FreeType(qm_ft_library);
        return -1;
    }
    qm_ttf_available = 1;
    return 0;
}

/* Fallback scale: qm_font.c's glyphs are QM_GLYPH_H (7px) tall at
 * scale 1. Pick the nearest integer scale so bitmap-font fallback text
 * is roughly the same visual height as the requested TTF px_size would
 * have been. */
static int qm_ttf_fallback_scale(int px_size)
{
    int scale = px_size / QM_GLYPH_H;
    return scale < 1 ? 1 : scale;
}

void qm_draw_text_ttf(qm_fb *fb, int x, int y, const char *text, int px_size, uint32_t color)
{
    if (!qm_ttf_available) {
        qm_draw_text(fb, x, y, text, qm_ttf_fallback_scale(px_size), color);
        return;
    }

    FT_Set_Pixel_Sizes(qm_ft_face, 0, (FT_UInt)px_size);

    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    int pen_x = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (FT_Load_Char(qm_ft_face, *p, FT_LOAD_RENDER) != 0)
            continue;

        FT_GlyphSlot slot = qm_ft_face->glyph;
        FT_Bitmap *bmp = &slot->bitmap;

        int glyph_x = pen_x + slot->bitmap_left;
        int glyph_y = y + (px_size - slot->bitmap_top);

        for (unsigned int row = 0; row < bmp->rows; row++) {
            for (unsigned int col = 0; col < bmp->width; col++) {
                uint8_t a = bmp->buffer[row * (unsigned int)bmp->pitch + col];
                if (a == 0) continue;
                int px = glyph_x + (int)col;
                int py = glyph_y + (int)row;
                uint32_t bg = qm_get_pixel(fb, px, py);
                uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
                uint8_t bgc = (uint8_t)((bg >> 8) & 0xFF);
                uint8_t bb = (uint8_t)(bg & 0xFF);
                uint32_t blended = QM_ARGB(255,
                    qm_alpha_blend(br, cr, a),
                    qm_alpha_blend(bgc, cg, a),
                    qm_alpha_blend(bb, cb, a));
                qm_fill_rect(fb, px, py, 1, 1, blended);
            }
        }

        pen_x += (int)(slot->advance.x >> 6);
    }
}

int qm_ttf_text_width(const char *text, int px_size)
{
    if (!qm_ttf_available)
        return qm_text_width(text, qm_ttf_fallback_scale(px_size));

    FT_Set_Pixel_Sizes(qm_ft_face, 0, (FT_UInt)px_size);

    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (FT_Load_Char(qm_ft_face, *p, FT_LOAD_DEFAULT) != 0)
            continue;
        width += (int)(qm_ft_face->glyph->advance.x >> 6);
    }
    return width;
}
```

Note for the implementer: `qm_alpha_blend` is declared in `quickmenu.h` (from `qm_icons.c`) and is safe to call here since both files link into the same binary. `qm_get_pixel`/`qm_fill_rect` are from `qm_font.c`'s section of `quickmenu.h`.

- [ ] **Step 4: Wire FreeType into the Buildroot package**

In `circuitsword-quickmenu.mk`, read the file fresh. Add `freetype` to `CIRCUITSWORD_QUICKMENU_DEPENDENCIES` (currently `host-wayland wayland wayland-protocols`):

```
CIRCUITSWORD_QUICKMENU_DEPENDENCIES = host-wayland wayland wayland-protocols freetype
```

Add `qm_ttf.c` to `CIRCUITSWORD_QUICKMENU_SRCS` (alphabetically, between `qm_settings.c` and `qm_wl.c`):

```
	$(CIRCUITSWORD_QUICKMENU_PKGDIR)/qm_ttf.c \
```

In the `BUILD_CMDS` section, both `$(TARGET_CC)` invocations need `-I$(STAGING_DIR)/usr/include/freetype2` added to their include flags and `-lfreetype` added to their link flags (only the FIRST invocation, building `circuitsword-quickmenu` itself, actually needs `qm_ttf.c`/FreeType — the second invocation builds `qm-wl-selftest`, a Wayland-only self-test binary that doesn't need FreeType, leave it unchanged).

In `INSTALL_TARGET_CMDS`, add installing the font file:

```
	$(INSTALL) -m 0644 -D $(CIRCUITSWORD_QUICKMENU_PKGDIR)/fonts/Cabin-Regular.ttf \
		$(TARGET_DIR)/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf
```

- [ ] **Step 5: Call `qm_ttf_init()` from `main()`**

In `quickmenu.c`'s `main()`, read the file fresh, add a call to `qm_ttf_init()` early (e.g. right after the signal handlers are set up, before the Wayland/input setup) — its return value doesn't need to gate anything else (both success and failure leave the program able to continue, per the fallback design):

```c
    qm_ttf_init();
```

- [ ] **Step 6: Host-side test for the fallback path**

Create/extend a test in `/Users/bas/Circuit-Sword Batocera/tests/` (NOT part of this git repo — read `tests/run-c-tests.sh` fresh first to match its existing compile-line conventions) — e.g. `tests/test_qm_ttf.c`:

```c
/* Host-side test for qm_ttf.c's fallback behavior. Does NOT call
 * qm_ttf_init() (which would try to load the real font and succeed if
 * libfreetype+the font file are available on the host, which is fine --
 * but this test specifically exercises the FALLBACK path, which is only
 * reachable when qm_ttf_init() was never called or failed, i.e. the
 * module-level availability flag stays at its 0 default). Run via
 * tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
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

int main(void)
{
    printf("qm_draw_text_ttf without qm_ttf_init (fallback path)\n");
    {
        qm_fb *fb = make_fb(64, 32);
        qm_draw_text_ttf(fb, 0, 0, "AB", 14, QM_RGB(0xFF, 0xFF, 0xFF));
        int nonzero = 0;
        for (uint32_t y = 0; y < fb->height; y++)
            for (uint32_t x = 0; x < fb->width; x++)
                if (qm_get_pixel(fb, (int)x, (int)y) != 0) nonzero++;
        check(nonzero > 0, "fallback path still draws something (bitmap font)");
        free_fb(fb);
    }

    printf("qm_ttf_text_width without qm_ttf_init (fallback path)\n");
    {
        int w = qm_ttf_text_width("AB", 14);
        check(w > 0, "fallback text width is positive");
        free(NULL); /* no-op, keeps stdlib.h include meaningful if unused elsewhere */
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
```

Add a compile+run block for this test to `tests/run-c-tests.sh`, matching the existing per-test pattern — this test needs `qm_ttf.c`, `qm_font.c`, `qm_icons.c`, `qm_icon_data.c` linked in (for the fallback path's dependencies and the real FreeType path's `qm_alpha_blend`/`qm_get_pixel`/`qm_fill_rect` calls). It also needs to link against `-lfreetype` and include FreeType's headers — if `libfreetype`/`freetype2` headers are NOT available on the host machine running these tests, this specific test file should be skipped gracefully (e.g. `run-c-tests.sh` checks for `pkg-config --exists freetype2` or similar before attempting to compile this one test, printing a skip notice rather than failing the whole suite) — read `run-c-tests.sh`'s existing structure fresh to decide the cleanest way to add this conditional, matching its current style.

- [ ] **Step 7: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass (or the new FreeType test skips cleanly if freetype2 isn't available on the host, with every other test still passing).

- [ ] **Step 8: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -60
```

Expected: builds clean. If FreeType linking fails (e.g. `-lfreetype` not found), check that `freetype` was actually added to `CIRCUITSWORD_QUICKMENU_DEPENDENCIES` (Step 4) — Buildroot only stages a package's headers/libs for a consumer that declares the dependency.

- [ ] **Step 9: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c '
  ls -la /bcm2837/target/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf
  ldd /bcm2837/target/usr/bin/circuitsword-quickmenu | grep freetype
'
```

Expected: font file present at the target path; `circuitsword-quickmenu` binary shows a `libfreetype.so.6` link dependency.

- [ ] **Step 10: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf \
        package/batocera/utils/circuitsword-quickmenu/qm_ttf.c \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.c \
        package/batocera/utils/circuitsword-quickmenu/circuitsword-quickmenu.mk
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add FreeType text rendering (qm_ttf.c)

Renders real TTF text via FreeType + a bundled copy of Cabin-Regular.ttf
(the same font es-theme-carbon uses for Batocera's own ES menus),
replacing the crude 5x7 bitmap font. Falls back to the existing
qm_font.c bitmap font automatically if FreeType init or font loading
ever fails -- the overlay never goes silent or crashes over this.
Not yet wired into any call site (next task).
EOF
)"
```

- [ ] **Step 11: Regenerate patch capture** (command in Global Constraints)

---

### Task 2: Wire FreeType text into every existing text call site

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c`

**Interfaces:**
- Consumes: `qm_draw_text_ttf()`, `qm_ttf_text_width()` (Task 1).

- [ ] **Step 1: Read `quickmenu.c` fresh**

Find every existing call to `qm_draw_text()` and `qm_text_width()` (the OLD bitmap-font functions) outside of `qm_font.c` itself — these are the hint line (SELECT/BACK/ADJUST labels) and the Joystick/Daemon Settings submenu row rendering, all added in earlier sub-projects today.

- [ ] **Step 2: Replace each call**

For each `qm_draw_text(fb, x, y, str, scale, color)` call, replace with `qm_draw_text_ttf(fb, x, y, str, px_size, color)` where `px_size` is chosen to look visually similar in height to what `scale` currently produces (`scale * QM_GLYPH_H` is the old pixel height — use that as the starting `px_size` value, e.g. a hint-line label at `scale=1` becomes `px_size=QM_GLYPH_H` i.e. `7`, which will look tiny for real TTF text — the implementer should pick sensible ACTUAL sizes for legibility rather than mechanically porting the old scale math; a reasonable starting point is `px_size = 14` for hint-line/submenu-row text at the 640x480 primary panel (`scale == 4` today) and `px_size = 10` at the 320x240 alt panel (`scale == 2` today) — i.e. derive `px_size` from the existing `scale` variable already computed at the top of `qm_render()`, e.g. `int px_size = (scale >= 4) ? 14 : 10;`, and use this one derived value everywhere text is drawn in a given render call for visual consistency within that frame).

Similarly replace every `qm_text_width(str, scale)` call used for centering/alignment math with `qm_ttf_text_width(str, px_size)` using the same derived `px_size`.

- [ ] **Step 3: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass — the existing `qm_render()` tests (main screen, Joystick submenu, Daemon Settings submenu, at both panel sizes) should still pass since they only check "renders something"/"stays in bounds"/"background painted", not exact pixel content, so switching the text renderer shouldn't break their assertions.

- [ ] **Step 4: Buildroot rebuild + Docker-volume verification** (same commands as Task 1 Steps 8-9, verification target: confirm the binary still builds and the font file dependency is intact — a `strings`-based content check isn't meaningful here since visual font rendering isn't a string literal).

- [ ] **Step 5: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: switch hint line + submenus to FreeType text

All qm_draw_text()/qm_text_width() call sites outside qm_font.c itself
now go through qm_draw_text_ttf()/qm_ttf_text_width() instead -- the
hint line and both submenus now render real TTF text.
EOF
)"
```

- [ ] **Step 6: Regenerate patch capture**

---

### Task 3: Main-screen state labels

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c`

- [ ] **Step 1: Read `qm_render()`'s main-screen item loop fresh**

Find the current per-item icon-drawing loop (WiFi/Volume/Brightness/Joystick/Daemon Settings).

- [ ] **Step 2: Add a state label next to each icon**

After each item's `qm_draw_icon_rgba()` call (and any existing bar draw for Volume/Brightness), add a `qm_draw_text_ttf()` call drawing a short label to the right of the icon (before the bar, if the row has one — or after the bar if that reads better; use your judgment on exact horizontal layout, but keep it readable and non-overlapping at both panel scales):

- WiFi row: `"WiFi: ON"` or `"WiFi: OFF"` depending on `st->wifi_on`.
- Volume row: `"Volume: NN%"` using `st->volume`.
- Brightness row: `"Brightness: NN%"` using `st->brightness`.
- Joystick row: `"Joystick"` (static — it's an action/submenu-opener, not a toggle, so no ON/OFF state to show).
- Daemon Settings row: `"Settings"` (same reasoning).

Use `snprintf` into a small local buffer for the dynamic labels (WiFi/Volume/Brightness), matching the existing pattern already used in the Daemon Settings submenu's label formatting (`labels[QM_DS_FAN_ON_TEMP]` etc. from earlier today's work) — read that code for the exact style to match.

- [ ] **Step 3: Extend host test coverage**

Read `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` fresh (despite the filename, this is where `qm_render()` coverage lives). The existing main-screen tests already check "renders something"/"stays in bounds" across various `wifi_on`/`volume`/`brightness` values — confirm these still pass with the new labels (they should, since the assertions are content-agnostic) rather than adding new assertions, unless a specific new failure mode (e.g. label text overflowing the panel width) seems worth a dedicated check — use your judgment.

- [ ] **Step 4: Run the host test suite, Buildroot rebuild, Docker-volume verification** (same pattern as prior tasks — for this task, verify via `docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c "WiFi:\|Volume:\|Brightness:"`, expect nonzero).

- [ ] **Step 5: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add state labels to main-screen rows

WiFi/Volume/Brightness rows now show their current state/value as text
next to the icon (e.g. "WiFi: ON"), so it's clear what pressing A/Right
will change before you press it.
EOF
)"
```

- [ ] **Step 6: Regenerate patch capture**

---

### Task 4: Joystick submenu row clarity

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c`

- [ ] **Step 1: Read the Joystick submenu's render/label code fresh**

Find the `labels[QM_JOY_COUNT]` array (or equivalent) that currently holds `"Calibrate"`, `"Invert J1 X"`, `"Invert J1 Y"`, `"Invert J2 X"`, `"Invert J2 Y"`, `"Joy 1 Enabled"`, `"Joy 2 Enabled"`.

- [ ] **Step 2: Rename the invert/enable rows for clarity**

Replace the label strings with clearer versions, e.g.:

```c
static const char *labels[QM_JOY_COUNT] = {
    "Calibrate", "J1 X-axis: invert", "J1 Y-axis: invert",
    "J2 X-axis: invert", "J2 Y-axis: invert",
    "J1: enabled", "J2: enabled",
};
```

(Exact wording is the implementer's judgment as long as it's clearly more self-explanatory than the original "Invert J1 X" style — the `[X]`/`[ ]` state indicator logic already present for these rows stays unchanged, it just now sits next to a clearer label.)

- [ ] **Step 3: Add a selection-dependent hint-line message**

Find where the hint line is drawn for `QM_SCREEN_JOYSTICK` (it may currently just reuse the same SELECT/BACK/ADJUST badges+labels as the main screen, or have its own variant from earlier today's work — read fresh to confirm). Add a short explanatory line that changes based on `st->joy_selected`:

```c
static const char *qm_joy_hint(int selected)
{
    switch (selected) {
    case QM_JOY_CALIBRATE:
        return "Rotate joysticks to calibrate";
    case QM_JOY_INVERT_J1X:
    case QM_JOY_INVERT_J1Y:
    case QM_JOY_INVERT_J2X:
    case QM_JOY_INVERT_J2Y:
        return "Reverses this axis's direction";
    case QM_JOY_TOGGLE_J1:
    case QM_JOY_TOGGLE_J2:
        return "Enable/disable this joystick";
    default:
        return "";
    }
}
```

Draw this string via `qm_draw_text_ttf()` somewhere in the hint-line area for `QM_SCREEN_JOYSTICK` specifically (below or alongside the existing badge row — use your judgment on layout, keep it readable at both panel scales, don't overlap the badges).

- [ ] **Step 4: Run the host test suite, Buildroot rebuild, Docker-volume verification.**

Verify via `docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c "axis: invert\|Reverses this axis"`, expect nonzero.

- [ ] **Step 5: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: clarify Joystick submenu row names + add hints

Renamed the invert/enable rows to be more self-explanatory and added a
selection-dependent hint-line message explaining what each row does --
addresses real on-device feedback that "Invert J1 X" etc. gave no clue
what pressing A would actually do.
EOF
)"
```

- [ ] **Step 6: Regenerate patch capture**

- [ ] **Step 7: Self-review**

Read the combined 4-task diff. Confirm: every text-drawing call site in `quickmenu.c` now goes through `qm_draw_text_ttf`/`qm_ttf_text_width`, none were missed (grep for any remaining bare `qm_draw_text(` / `qm_text_width(` calls outside `qm_font.c` and `qm_ttf.c`'s own fallback branches — there should be none). Confirm the font file path constant (`QM_TTF_FONT_PATH`) matches exactly between its declaration and the `.mk` file's install destination.
