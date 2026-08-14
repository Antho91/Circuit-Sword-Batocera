# Overlay FreeType Text — Design

Replaces `circuitsword-quickmenu`'s crude 5×7-pixel bitmap font
(`qm_font.c`) with real FreeType-rendered TTF text, used for every piece
of text in the overlay, and adds missing clarity to the UI based on
real on-device feedback: main-screen rows gain a state label, and the
Joystick submenu's invert/enable rows get clearer names plus an
explanatory hint on selection.

## Context

Three pieces of feedback surfaced after flashing today's build:

1. Main-screen rows (WiFi/Volume/Brightness/Joystick/Daemon Settings) are
   icon-only — nothing on screen shows what pressing A/Right will actually
   do (e.g. does WiFi being ON mean A turns it off, or on?).
2. The Joystick and Daemon Settings submenus (built earlier today) use
   `qm_font.c`'s existing 5×7-glyph bitmap font for all their content —
   looks crude next to the real vector icons used everywhere else in the
   same overlay.
3. The Joystick submenu's invert/enable row names ("Invert J1 X", "Joy 1
   Enabled") give no indication of what the setting actually does for
   someone unfamiliar with joystick-calibration terminology.

Confirmed by checking the currently-built image: `libfreetype.so.6` is
already present on target (`/usr/lib/libfreetype.so.6`, pulled in as a
dependency of other packages — VLC, SDL2-using cores, etc.) and
FreeType's headers are already staged (`/usr/include/freetype2/ft2build.h`)
— using FreeType here adds no new heavyweight dependency to the image,
just a new consumer of an already-present library. The actual font
Batocera's own ES menu uses (`es-theme-carbon`, this project's
unconditionally-selected theme) is `Cabin-Regular.ttf`
(`/usr/share/emulationstation/themes/es-theme-carbon/art/fonts/Cabin-Regular.ttf`
on target) — SIL Open Font License, safe to redistribute a copy of.

## Scope confirmed with the user

- FreeType text replaces the bitmap font EVERYWHERE text appears in the
  overlay: the hint line, the new per-row state labels, and all submenu
  content (Joystick + Daemon Settings). Not a partial replacement.
- The title wordmark ("CIRCUIT-SWORD") is UNCHANGED — it's already a
  baked SVG vector icon (`QM_ICON_TITLE`), not text rendered through
  `qm_font.c`, so it's out of scope for this change entirely.
- `qm_font.c` itself is NOT deleted — it becomes a silent fallback if
  FreeType initialization or font loading ever fails at runtime, so the
  overlay degrades to blocky text instead of breaking entirely.

## Architecture

```
circuitsword-quickmenu startup:
    qm_ttf_init() -- FT_Init_FreeType(), FT_New_Face() from the bundled
                     font file path, FT_Set_Pixel_Sizes() per draw call
    |
    | success                              | failure
    v                                      v
qm_draw_text_ttf(fb, x, y, text,      qm_draw_text() (existing
                  px_size, color)      qm_font.c bitmap font, unchanged)
    |
    | per character: FT_Load_Char + FT_Render_Glyph -> 8-bit alpha
    | coverage bitmap, blitted via the SAME qm_alpha_blend() primitive
    | qm_icons.c already uses for icon compositing
    v
Framebuffer
```

Every existing call site that currently calls `qm_draw_text()` (hint
line, Joystick submenu rows, Daemon Settings submenu rows) switches to
`qm_draw_text_ttf()`; `qm_draw_text()` itself and `qm_font.c` stay in the
codebase, called only from the one fallback path in `qm_ttf_init()`'s
failure branch (routed through a small dispatch: a module-level
`qm_ttf_available` flag that every draw call checks).

## Components

### `qm_ttf.c` / `qm_ttf.h` (new)

- `int qm_ttf_init(void)` — one-time FreeType + face load at startup.
  Returns 0 on success, -1 on failure (missing library, missing/corrupt
  font file, out of memory). Sets the module-level availability flag.
- `void qm_draw_text_ttf(qm_fb *fb, int x, int y, const char *text, int px_size, uint32_t color)` —
  renders `text` at the given pixel size, alpha-blending each glyph's
  FreeType-rendered coverage bitmap into the framebuffer via the shared
  `qm_alpha_blend()` primitive. If `qm_ttf_init()` never succeeded, falls
  back to `qm_draw_text()` at an equivalent scale — callers never need to
  branch on availability themselves, this function does it internally.
- `int qm_ttf_text_width(const char *text, int px_size)` — for centering
  (hint line) and right-alignment (state labels), mirrors
  `qm_text_width()`'s role for the bitmap font, falls back to
  `qm_text_width()` if FreeType is unavailable.
- Font file bundled at `package/batocera/utils/circuitsword-quickmenu/fonts/Cabin-Regular.ttf`
  (vendored copy, extracted once from the already-built image's
  es-theme-carbon installation — not fetched from the theme package at
  Buildroot build time, avoiding a cross-package build-order dependency),
  installed to a fixed target path (e.g.
  `/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf`) by
  `circuitsword-quickmenu.mk`'s install step.

### `quickmenu.c` (modified)

- Main screen: each row gains a state label drawn via
  `qm_draw_text_ttf()` next to its icon — `"WiFi: ON"`/`"WiFi: OFF"`,
  `"Volume: NN%"`, `"Brightness: NN%"` (Volume/Brightness already show a
  bar, the label is in addition, not a replacement), and static labels
  for the two menu-opening rows (`"Joystick"`, `"Settings"`) since those
  don't have an on/off state, just an action.
- Hint line: `qm_draw_text()` calls become `qm_draw_text_ttf()` calls,
  same badge+label layout, just the new font/renderer.
- Joystick submenu: row labels renamed for clarity — `"Invert J1 X"` →
  `"J1 X-axis: invert"` (exact final strings decided at implementation,
  following this pattern), plus a new hint-line message that changes
  based on which row is selected (e.g. selecting an invert row shows
  `"Reverses this axis's direction"` in the hint area instead of the
  static SELECT/BACK/ADJUST text).
- Daemon Settings submenu: same font swap, no renaming needed (its rows
  are already self-explanatory numeric settings).

## Error Handling

- `qm_ttf_init()` failure (library present but font file missing/corrupt,
  or `FT_Init_FreeType` itself failing) is logged to stderr once at
  startup and the whole overlay continues in bitmap-font-only mode —
  never a hard failure, matching this project's established "never let a
  cosmetic subsystem take down the whole overlay" posture (same spirit as
  `charging_thread`'s edge-detection-unavailable fallback).
- Per-glyph rendering failures (a codepoint FreeType can't find in the
  font) are skipped silently, same as `qm_font.c`'s existing "unmapped
  char draws nothing" behavior.

## Testing

**Off-device**: `qm_ttf_text_width()`'s fallback-when-unavailable path
and `qm_draw_text_ttf()`'s fallback path are host-testable without a real
font (force `qm_ttf_available = 0` and confirm it correctly delegates to
the bitmap-font functions). Full FreeType glyph rendering needs the real
bundled font file present, which IS available to host tests (it's a
committed file in the repo), so a host test can also exercise the real
FreeType path if `libfreetype` is available on the build/test machine —
if not, the test suite should skip that specific check gracefully rather
than fail the whole suite.

**Needs on-device validation (not yet done)**: actual visual legibility
at both panel sizes/scales; FreeType's memory footprint on this 1GB-RAM
board (a new consideration — the bitmap font used zero extra memory
beyond its static array, FreeType allocates a face + glyph cache);
whether per-frame glyph rendering (re-rasterizing on every redraw, no
glyph bitmap cache in this design) causes any visible input-lag on the
Daemon Settings screen's live-updating status row.

## Out of Scope (this pass)

- Any glyph bitmap caching/pre-rendering — every `qm_draw_text_ttf()`
  call re-rasterizes from scratch; if on-device testing shows this is too
  slow or memory-hungry, a cache is a natural follow-up, not built now.
- Changing the title wordmark's rendering — stays the existing baked SVG
  icon.
- Any change to `circuitsword-statusbar` — this design is scoped to
  `circuitsword-quickmenu` only; the status bar has no submenu-style text
  content today and isn't part of this feedback.
