# Real Overlay Icons: Findings

Implements `docs/superpowers/specs/2026-08-10-real-overlay-icons-design.md`
(including its title/badge addendum). Replaces this morning's hand-
authored 5x7 pixel-art icons AND the quickmenu's bitmap-font title/hint
line with real vector-sourced art (battery/WiFi from Batocera/
EmulationStation's own icons; volume/brightness/title/button-badges newly
hand-drawn to match), each baked to its own fixed pixel size by a
developer-run conversion script. Also moves the quickmenu's icon and
percentage bar onto one row instead of two.

## What was built

- `tools/convert-icons.py` (new, developer-run, NOT part of the Buildroot
  build): rasterizes 14 source SVGs (9 status icons, 1 title wordmark, 4
  button badges) to fixed-size alpha-only bitmaps via CairoSVG, emitting
  the generated-but-committed `qm_icon_data.c`.
- `qm_icons.c` (rewritten): `qm_alpha_blend()` (pure per-channel blend
  math), `qm_draw_icon_rgba()` (alpha-over blit, reads each asset's own
  width/height -- no fixed size assumption, no runtime scaling by panel),
  `qm_battery_icon_kind()` (percent+charging -> one of 6 battery states).
- `quickmenu.h`: `qm_icon_kind` grew to 14 members; `qm_icon_asset` gained
  `width`/`height` fields (previously would have assumed one fixed size).
- `quickmenu.c`: title now draws via the baked wordmark instead of the
  bitmap font; hint line now shows colored button badges (A/B/LEFT/RIGHT)
  instead of plain "A:"/"B:"/"LEFT/RIGHT:" text prefixes; volume/
  brightness icon and their percentage bar now sit on the same row,
  vertically centered, instead of icon-above/bar-below.
- `sb_render.c`: all four status-bar icons now draw via the new real-icon
  API; battery uses the new 6-state mapping instead of a generic 0-3
  level; icons render at a fixed size on both panels (no `* scale`).

## Verified off-device

- `tests/test_qm_icons.c`: `qm_alpha_blend()`'s boundary math,
  `qm_battery_icon_kind()`'s full boundary set, all 14 real icons draw at
  least one non-background pixel, the title asset is confirmed to use
  `QM_TITLE_W`x`QM_TITLE_H` rather than the square `QM_ICON_SIZE`,
  distinguishable pixel output between states, out-of-range kind no-ops.
- `tests/test_sb_render.c`'s existing tests still pass against the new
  draw calls.
- `tests/test_qm_font.c`'s existing `qm_render()` tests still pass
  against the new icon+bar-same-row layout and the new title/hint-line
  drawing (they only assert "draws something" and background color).
- Incremental Buildroot compiles (`PKG=circuitsword-quickmenu-rebuild`
  and `PKG=circuitsword-statusbar-rebuild`) both succeed.
- Patch capture confirmed to contain the new code, including the
  generated baked-icon/title data, via targeted `grep`.

## Needs on-device validation (not yet done)

- **Visual quality**: the whole point of this change -- real vector-
  sourced icons, a sharp title wordmark, and colored button badges should
  read far more clearly than the previous pixel art/plain text, but this
  has only been confirmed via a rendered HTML preview (shown to and
  approved by the user before implementation), not on the actual DPI
  panel.
- **Layout proportions**: the quickmenu's new icon+bar-same-row layout
  and hint-line badge spacing, plus the status bar's fixed-size (no
  per-panel scaling) icon layout, need confirming on the real panels.
- **Title/badge font dependency**: the title and badge SVGs use `<text>`
  elements rendered by whatever font was installed on the machine that
  ran `tools/convert-icons.py` -- confirm the baked result looks correct;
  if it doesn't, the fix is re-running the script with a different font
  available, not a code change.
