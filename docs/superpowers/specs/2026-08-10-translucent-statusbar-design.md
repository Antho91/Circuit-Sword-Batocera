# Circuit-Sword Translucent Status Bar — Design

Makes `circuitsword-statusbar`'s background translucent (uniform 80%
opacity) instead of fully opaque black, so the running game stays
partially visible through the bar strip while it's on screen. Icons drawn
on top of the bar (battery/WiFi/volume/brightness) stay fully opaque and
legible.

## Context

`sb_render.c` currently fills the entire bar height with solid opaque
`QM_COLOR_BG` (black) via `qm_fill_rect`. The bar's Wayland surface uses a
`wl_shm` buffer in `WL_SHM_FORMAT_XRGB8888` — a format with no alpha
channel, so labwc (the compositor) always composites it as fully opaque
regardless of what color values are written; `qm_fill_rect` writing a
"transparent-looking" color would have no visible effect. The surface
also explicitly marks itself opaque to the compositor
(`wl_surface_set_opaque_region`, `sb_wl.c`), an optimization hint telling
labwc it can skip blending this surface against whatever's underneath —
which would defeat translucency even if the buffer format supported it.

Confirmed with the user via brainstorming:
- Uniform 80% opacity (not a visual gradient) — simpler, and the natural
  shape for a single value that could become user-configurable later
  (out of scope for this design — noted for a future iteration, not
  built now).
- Icons stay fully opaque; only the plain background fill becomes
  translucent.
- Scoped entirely to `circuitsword-statusbar`. `circuitsword-quickmenu`
  (the interactive full-screen menu) stays fully opaque by original
  design — untouched by this change.

## Architecture

```
sb_wl.c: wl_shm buffer format WL_SHM_FORMAT_XRGB8888 -> WL_SHM_FORMAT_ARGB8888
         wl_surface_set_opaque_region(...) call removed entirely
              |
              v
sb_render.c: background fill QM_COLOR_BG -> QM_ARGB(204, 0, 0, 0)
             (204/255 ~= 80% opacity; new QM_ARGB macro, quickmenu.h)
              |
              v
qm_draw_icon_rgba() (qm_icons.c): every icon pixel it draws now writes
alpha=255 explicitly (QM_ARGB(255, r,g,b) instead of QM_RGB(r,g,b)) --
a universal change to the shared primitive, harmless for
circuitsword-quickmenu's still-XRGB8888 surface (alpha byte is simply
ignored there), and what makes icons stay fully opaque against the
now-translucent background underneath them in the status bar.
```

## Components

### `quickmenu.h` (modified)

New macro alongside the existing `QM_RGB`:

```c
#define QM_ARGB(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
```

`QM_RGB` itself is unchanged (still produces alpha=0 in the top byte) —
`circuitsword-quickmenu` and every other existing caller keeps working
exactly as before, since XRGB8888 ignores that byte entirely.

### `qm_icons.c`'s `qm_draw_icon_rgba()` (modified)

The per-pixel blended color it writes changes from `QM_RGB(...)` to
`QM_ARGB(255, ...)` — every icon pixel this function ever draws becomes
explicitly fully opaque in the alpha channel, in both overlays. This is
a one-line change to the existing blend loop, not a new code path.

### `sb_render.c` (modified)

The whole-bar background fill (`qm_fill_rect(fb, 0, 0, ..., QM_COLOR_BG)`)
changes to a new translucent color, `QM_ARGB(204, 0, 0, 0)` (black at
80% opacity — `204 = round(0.8 * 255)`). Nothing else in this file
changes; icon draw calls already go through `qm_draw_icon_rgba`, which
now independently guarantees full icon opacity regardless of what's
underneath.

### `sb_wl.c` (modified)

- `WL_SHM_FORMAT_XRGB8888` → `WL_SHM_FORMAT_ARGB8888` at buffer creation.
- The `wl_surface_set_opaque_region(...)` call (and its surrounding
  region-creation code) is removed entirely — the bar is no longer
  opaque, so the compositor must actually blend it against RetroArch's
  surface underneath rather than skip blending as an optimization.

### `circuitsword-quickmenu` / `qm_wl.c` (untouched)

Stays `WL_SHM_FORMAT_XRGB8888`, stays fully opaque, no changes — matches
its own original, still-valid design (a full-screen interactive menu
that intentionally occludes everything underneath while open).

## Data Flow

No change to what data feeds the bar — same `qm_wifi_get()`/
`qm_volume_get()`/`qm_brightness_get()`/`qm_battery_get()` calls, same
icon selection logic. Only the pixel format and two color constants
change; the bar's actual content (which icons, which states) is
identical to today.

## Error Handling

No new failure modes. `ARGB8888` buffer allocation follows the exact
same `wl_shm_create_pool`/`wl_shm_pool_create_buffer` path already used
for `XRGB8888` — same size calculation (both are 4 bytes/pixel), just a
different format enum passed to `wl_shm_pool_create_buffer`. If buffer
creation fails, existing error handling (`sb_create_buffer` returning
non-zero, `sb_wl_open` cleaning up and returning `NULL`) is unchanged.

## Testing

**Verifiable off-device**: `sb_render.c`'s background fill pixel value
(alpha byte == 204, matching `tests/test_sb_render.c`'s existing
pixel-inspection style) via a new/extended host test; `qm_draw_icon_rgba`
writing alpha=255 for any icon pixel it touches (extend
`tests/test_qm_icons.c`'s existing pixel checks to also assert on the
top byte, not just the RGB bytes it already checks).

**Needs on-device validation**: whether labwc actually honors
`ARGB8888` translucency for a `wlr-layer-shell` surface the way it does
for regular `xdg-shell` surfaces (this project's existing quickmenu/
statusbar work has already proven layer-shell surfaces composite
correctly on this hardware, but always as fully-opaque surfaces so far
— this is the first translucent one, genuinely new ground); whether 80%
reads as a good balance between "status info stays legible" and "game
stays visible" at actual DPI panel brightness/contrast; any visible
flicker or compositing artifact specific to alpha-blended layer-shell
surfaces that a fully-opaque one wouldn't show.

## Out of Scope (this phase)

- Any visual gradient (fade) — explicitly declined in favor of a
  uniform value.
- User-configurable opacity via an EmulationStation settings screen —
  the user explicitly flagged this as a future direction, not built
  now; this design hardcodes 80% (`204`) as a single constant.
- Any change to `circuitsword-quickmenu`'s own opacity/surface format —
  stays fully opaque by original, still-valid design.
- Icon translucency — icons stay fully opaque per the user's explicit
  choice; only the plain background fill becomes translucent.
