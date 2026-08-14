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
