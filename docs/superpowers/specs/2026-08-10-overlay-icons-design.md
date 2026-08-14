# Circuit-Sword Overlay Icons — Design

Replaces the word-labels (`WIFI`, `VOLUME`, `BRIGHTNESS` in
`circuitsword-quickmenu`; `BAT`, `WIFI`, `VOL`, `BRT` in
`circuitsword-statusbar`) with small hand-authored bitmap icons, fully
visual — no percentage numbers anywhere in either overlay afterward.

## Context

Both overlays currently render everything through `qm_font.c`'s pure 5×7
bitmap font (`qm_draw_text`/`qm_draw_char`, no cairo/pango/glib, no image
decoding library — see that file's own header comment). The quickmenu's
volume/brightness rows already pair a text label+percentage with a filled
bar (`qm_draw_bar` in `quickmenu.c`); the status bar is pure text
(`"BAT %d%%"`, `"WIFI ON"`, `"VOL %d%%"`, `"BRT %d%%"` in `sb_render.c`).

Confirmed with the user via brainstorming:
- Icons apply to **both** overlays.
- Icons stay on the **same 5×7 grid** as the existing letters — smaller
  and more constrained than a dedicated larger icon grid, but consistent
  with the project's existing pixel-art scale and needing no new drawing
  primitives beyond what `qm_draw_char` already proves out.
- **Fully visual, no numbers**: the icon itself communicates the level
  (battery fill amount, volume loudness, brightness level) — no
  percentage text next to it anywhere. The quickmenu's existing
  percentage bar (`qm_draw_bar`) stays for volume/brightness (it's
  already a non-numeric, purely visual indicator), but the numeric label
  text (`"VOLUME: 65%"`) is removed.

## Architecture

A new file, `qm_icons.c`, alongside `qm_font.c` in
`circuitsword-quickmenu`'s package directory — a separate concern from
the letter font (icons are hand-drawn shapes, not characters), but built
the exact same way: a static bitmap table plus one draw function,
compiled into both `circuitsword-quickmenu` and `circuitsword-statusbar`
the same way `qm_font.c`/`qm_settings.c` already are today (both
packages' `.mk` files already reference this directory's sources
directly — `qm_icons.c` is added to that same list in both).

```c
typedef enum {
    QM_ICON_BATTERY,      /* levels 0-3: empty/low/half/full */
    QM_ICON_BATTERY_CHARGING,   /* single bolt glyph, no levels -- replaces
                                    QM_ICON_BATTERY entirely while charging,
                                    not drawn as an overlay on top of it (a
                                    5x7 grid has no clean room to combine
                                    both shapes legibly) */
    QM_ICON_WIFI_ON,       /* single glyph, no levels */
    QM_ICON_WIFI_OFF,      /* single glyph, no levels */
    QM_ICON_VOLUME,        /* levels 0-3: mute/low/mid/high */
    QM_ICON_BRIGHTNESS,    /* levels 0-3: low/mid/high/max */
} qm_icon_kind;

void qm_draw_icon(qm_fb *fb, int x, int y, qm_icon_kind kind, int level,
                   int scale, uint32_t color);
int qm_icon_width(void);  /* == QM_GLYPH_W, exposed for layout math the
                              same way qm_text_width() already is */

/* Pure, shared by both callers: maps a 0-100 percent reading to a 0-3
   icon level. 0-24 -> 0, 25-49 -> 1, 50-74 -> 2, 75-100 -> 3. Values
   outside 0-100 clamp first. */
int qm_icon_level_from_percent(int percent);
```

`qm_draw_icon` mirrors `qm_draw_char`'s exact structure (same
row-then-column bit-scan over a `QM_GLYPH_H`-row, `QM_GLYPH_W`-wide
bitmap, same `qm_fill_rect`-per-set-bit approach) — it looks up a
different table (`kind`+`level` instead of a character), nothing else
changes. An unrecognized `kind`/`level` combination no-ops, the same
defensive behavior `qm_draw_char` already has for an unknown character.

## Components

### `qm_icons.c` (new)

Hand-authored 5×7 bitmaps, same encoding as `qm_font.c`'s glyph table
(one `uint8_t` per row, low 5 bits, MSB = leftmost column). Shapes are
loosely inspired by Batocera/EmulationStation's own status-icon
conventions (a striped battery body, arced WiFi signal) for visual
familiarity — not a literal reproduction. Confirmed with the user: no
actual ES/theme icon assets are vendored in this repo to copy from (they
are SVGs pulled from an external theme repo at build time, and this
project's renderer has no image-decoding capability by design), so this
is a from-memory stylistic reference during hand-authoring, not an asset
import:

- **Battery** (4 levels): a rectangle outline with a small nub on the
  right (the terminal), filled from the bottom by 0/1-2/3-4/5-7 rows
  for empty/low/half/full — reuses the same "partial fill by row count"
  idea `qm_draw_bar` already uses for percentages, just baked into a
  fixed bitmap per level instead of computed at runtime.
- **Battery charging**: a bolt/lightning-bolt shape, single glyph.
- **WiFi on/off**: two glyphs — a simple set of concentric arcs over a
  dot for "on"; the same arcs with a diagonal strike-through for "off"
  (mirrors how the quickmenu's `qm_font.c` character set already has a
  few line-drawing glyphs like `/` and `%` to draw from as a style
  reference).
- **Volume** (4 levels): a speaker wedge with 0/1/2/3 sound-wave arcs
  radiating for mute/low/mid/high.
- **Brightness** (4 levels): a sun — a filled center dot with 0/2/4/6
  rays for low/mid/high/max (levels differ by ray count, not size, since
  a 5×7 grid has no room to scale the sun body itself).

### `circuitsword-quickmenu`'s `quickmenu.c` (modified)

In `qm_render()`'s per-row loop:
- `QM_ITEM_WIFI`: replace `snprintf(buf, ..., "WIFI: %s", ...)` +
  `qm_draw_text` with a single `qm_draw_icon(fb, margin, y,
  st->wifi_on ? QM_ICON_WIFI_ON : QM_ICON_WIFI_OFF, 0, scale, fg)` call.
- `QM_ITEM_VOLUME` / `QM_ITEM_BRIGHTNESS`: replace the
  `qm_format_percent` + `qm_draw_text` pair with
  `qm_draw_icon(fb, margin, y, QM_ICON_VOLUME /* or BRIGHTNESS */,
  qm_icon_level_from_percent(st->volume /* or brightness */), scale, fg)`.
  The existing `qm_draw_bar` call directly below stays exactly as-is —
  it is already a non-numeric visual indicator, nothing about it changes.
- `qm_format_percent()` becomes dead code once both its call sites are
  gone — removed as part of this same change (it has no other callers;
  confirmed by grep before removal).
- Row height/layout math (`line_h`, `row_h`) is currently sized around
  `QM_GLYPH_H`-tall text and stays correct unchanged, since icons live on
  the exact same `QM_GLYPH_H`-tall grid the text did.

### `circuitsword-statusbar`'s `sb_render.c` (modified)

Each of the four `snprintf` + `qm_draw_text` pairs becomes one
`qm_draw_icon` call instead — battery uses `QM_ICON_BATTERY_CHARGING`
when `battery_charging` is true, else `QM_ICON_BATTERY` at
`qm_icon_level_from_percent(battery_percent)`; WiFi picks
`QM_ICON_WIFI_ON`/`QM_ICON_WIFI_OFF`; volume and brightness use their
level-mapped icon the same way the quickmenu does.

The file's existing width-budget comment (sized around the worst-case
*text* width, `"BAT 100%+"` etc., at scale 3 on the 640px panel) no
longer applies — four fixed-width icon glyphs at any scale take a small
fraction of that budget, so the comment and its arithmetic are replaced
with the new, much smaller worst case. This will very likely also
resolve the status bar's previously-known, explicitly-deferred 320px/
scale-2 alternate-panel overflow (`PHASE4-STATUSBAR-FINDINGS.md`'s
"Known deferred items") as a side effect of no longer needing wide
percentage text — but that must still be confirmed on real hardware, not
assumed; this design does not claim it fixed.

## Data Flow

No change to data flow — `qm_wifi_get()`/`qm_volume_get()`/
`qm_brightness_get()`/`qm_battery_get()` (already existing) still supply
the same 0-100/boolean values they always did. Only the render step
changes: those values now go through `qm_icon_level_from_percent()` to
pick a bitmap, instead of `snprintf`-ing into visible digits.

## Error Handling

- **Unrecognized icon kind/level** (should not happen given the enum and
  the 0-3 clamp in `qm_icon_level_from_percent`, but matches the existing
  defensive convention): `qm_draw_icon` no-ops, same as `qm_draw_char` on
  an unknown character — nothing crashes, that one glyph position is
  simply blank.
- **Underlying `qm_*_get()` failure** (already-existing -1-on-error
  convention, unchanged by this design): both callers already have their
  own "hold last known good value" or default-value handling upstream of
  render; this design doesn't touch that, only how a valid value is drawn.

## Testing

**Verifiable off-device**: `qm_icon_level_from_percent()`'s boundary
mapping (0, 24, 25, 49, 50, 74, 75, 100, and out-of-range clamping) as a
pure-function unit test, following the same host-`cc`-compiled pattern as
the existing `tests/test_qm_font.c`; `qm_draw_icon()`'s pixel output for
each `kind`/`level` combination (confirms each level actually paints
different pixels, the same "renders visibly different output" style of
assertion `tests/test_sb_render.c` already uses for its charging/WiFi
cases) into a malloc'd `qm_fb`, no Wayland/hardware involved.
`tests/test_sb_render.c`'s existing fixtures/assertions need updating
since `sb_render()`'s pixel output changes shape entirely (icons instead
of text) — expected, not a regression, the test's *purpose* (verify
different states paint different pixels) still holds and is re-verified
against the new icon-based output.

**Needs on-device validation**: legibility of 5×7 icons at actual DPI
panel viewing distance/size (a real risk this design accepts up front —
5×7 is small for pictorial shapes, confirmed as the user's explicit
choice over a larger dedicated icon grid); whether the status bar's
narrower icon-based layout does in fact now fit the 320px alternate
panel (see above — plausible, not confirmed); visual correctness of each
hand-authored bitmap shape once actually seen rendered (bitmap art
authored by eye in this design doc's prose description, real pixel
values finalized during implementation).

## Out of Scope (this phase)

- WiFi signal strength (still just connected/disconnected, matches the
  existing `qm_wifi_get()` boolean — unchanged from the status bar's
  original design).
- Any animation/transition between icon levels — each redraw just picks
  and draws the current level's static bitmap, no interpolation.
- A mute-specific icon distinct from "volume at its lowest level" — the
  existing statusbar design already declined a separate mute icon; volume
  level 0 (lowest quartile) covers the same signal.
- Enlarging the icon grid beyond 5×7 — explicitly declined by the user in
  favor of staying on the existing font-sized grid.
