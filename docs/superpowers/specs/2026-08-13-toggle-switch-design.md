# Visual Toggle Switches — Design

Replaces text-based on/off indicators in `circuitsword-quickmenu`
("WiFi: ON/OFF", the Joystick submenu's `[X]`/`[ ]` marks, and the Daemon
Settings status row's "Fan: ON/OFF") with a real pill-shaped toggle-switch
graphic, so state is readable at a glance instead of requiring the user to
read text.

## Context

On-device feedback after the FreeType text pass: on/off state is currently
communicated purely through text, which reads less immediately than a
proper switch graphic. The user asked whether Batocera's own switch look
could be reused rather than inventing a new visual style.

It can: `batocera-emulationstation` (MIT-licensed, this project's ES fork)
ships exactly this as its `SwitchComponent`'s built-in resources,
`resources/on.svg` and `resources/off.svg` — a pill-shaped track with a
circular knob, filled solid with the knob on the right when ON, drawn as
an outline with the knob on the left when OFF. Both are single flat-color
paths (`fill:#ffffff`), tinted at runtime by the theme's text color, not a
separate accent color — the shape alone communicates state. This matches
`circuitsword-quickmenu`'s existing icon convention exactly (baked
monochrome bitmap, one runtime color argument via `qm_draw_icon_rgba`),
so no changes to the color/tinting system are needed.

## Scope confirmed with the user

- Applies everywhere an on/off state is currently shown as text or
  bracket marks: the main screen's WiFi row, all 6 toggle rows in the
  Joystick submenu (4 invert rows + 2 enable rows), and the Daemon
  Settings status row's fan-on/off portion.
- The Daemon Settings status row usage is purely visual — that row is
  read-only display today (the fan state isn't directly toggled by the
  user there) and stays that way; only its *rendering* changes.
- Color stays the existing single-tint convention
  (`QM_COLOR_FG`/`QM_COLOR_DIM` for selected/unselected), matching every
  other icon in the overlay and matching how ES itself renders its own
  switch (no new accent color).
- The title wordmark and every other existing icon are unaffected.

## Assets

- Vendor `on.svg` and `off.svg` directly from
  `batocera-emulationstation` at the pinned commit
  (`2c29a330e487210a7d51ad2650bb7b280ea44c86`, per
  `batocera-emulationstation.mk`'s `BATOCERA_EMULATIONSTATION_VERSION`),
  into `package/batocera/utils/circuitsword-quickmenu/icons/src/` as
  `toggle-on.svg` / `toggle-off.svg` — same vendoring approach already
  used for `fonts/Cabin-Regular.ttf` (pull the real Batocera asset once,
  commit a local copy, no runtime or build-time dependency on the
  upstream source).
- Baked via the existing `tools/convert-icons.py` pipeline at 32×16px
  (preserves the source's ~2:1 aspect ratio; `qm_draw_icon_rgba`'s
  drawing loop already reads width/height per-asset from
  `qm_icon_asset`, so a non-square icon needs no changes to the drawing
  primitive itself — only a new `ICONS` list entry with `(32, 16)`
  instead of the usual `(ICON_SIZE, ICON_SIZE)`).
- New `qm_icon_kind` enum members: `QM_ICON_TOGGLE_ON`,
  `QM_ICON_TOGGLE_OFF`, inserted in `quickmenu.h` after the existing
  `QM_ICON_SETTINGS` entry (before `QM_ICON_TITLE`, matching how
  `QM_ICON_JOYSTICK`/`QM_ICON_SETTINGS` were added earlier).

## Rendering changes

### Main screen (`QM_SCREEN_MAIN`), WiFi row

Currently: icon + `"WiFi: ON"` / `"WiFi: OFF"` text, right-aligned via
`qm_ttf_text_width()`. Becomes: icon + the toggle-switch icon
(`QM_ICON_TOGGLE_ON`/`_OFF` based on `st->wifi_on`), right-aligned at a
fixed 32px width — simpler than the current variable-width text
right-alignment math, since the icon width is constant regardless of
state.

### Joystick submenu (`QM_SCREEN_JOYSTICK`)

Currently: each of the 6 toggle rows (all but Calibrate) draws `"[X]"` or
`"[ ]"` via `qm_draw_text_ttf()`, right-aligned via
`qm_ttf_text_width()`, using the existing `status_bit[]` array to look up
the row's bit in `st->joy_status[6]`. Becomes: same `status_bit[]`
lookup, but draws `QM_ICON_TOGGLE_ON`/`_OFF` right-aligned at the fixed
32px width instead of text.

### Daemon Settings (`QM_SCREEN_DAEMON_SETTINGS`), status row

Currently: `"CPU: %.1fC, Fan: %s"` or `"Fan is currently: %s"` as one
text string (or a transient `ds_message` when a save/reload just failed,
which takes priority and stays text-only — that's an error message, not
a state indicator). Becomes: the CPU-temp/message portion stays text;
the fan-state portion renders as the switch icon instead of the literal
word "ON"/"OFF", right-aligned on the row. No change to input handling —
this row was never interactive.

## Testing

- `tests/test_qm_icons.c`'s existing "every icon draws at least one
  non-background pixel" loop picks up both new icons automatically
  (it iterates `QM_ICON_COUNT`).
- `tests/test_qm_font.c`'s per-screen, per-panel-size render tests
  (rightmost-pixel-stays-in-bounds checks for `QM_SCREEN_MAIN`,
  `QM_SCREEN_JOYSTICK`, `QM_SCREEN_DAEMON_SETTINGS` at 320×240 and
  640×480) get updated to reflect the new fixed-32px right edge instead
  of the old variable-width text right edge.
- A new icon-asset sanity check (matching the pattern used when the
  Joystick/Settings icons were added): confirm the baked
  `QM_ICON_TOGGLE_ON`/`_OFF` alpha arrays are non-empty/non-all-zero,
  guarding against a silent CairoSVG rasterization failure on the new
  32×16 non-square size.
- **Needs on-device validation**: legibility of a 32×16px switch at the
  320×240 alt panel's scale, and whether the ON/OFF shape distinction
  (filled-pill-with-right-knob vs outline-pill-with-left-knob) reads
  clearly at that size on the real display — not verifiable off-device.

## Out of scope (this pass)

- Any animation/transition when a switch changes state — stays an
  instant redraw, matching every other stateful icon in this overlay
  (e.g. the WiFi icon's dim/bright swap).
- Reusing the *theme's* switch icon (a theme could override `pathOn`/
  `pathOff`) instead of the base ES resource — out of scope; vendoring
  the base asset avoids a build-order dependency on `es-theme-carbon`,
  matching the reasoning already used for `Cabin-Regular.ttf`.
