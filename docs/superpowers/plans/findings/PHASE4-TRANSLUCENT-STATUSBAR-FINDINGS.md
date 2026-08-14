# Translucent Status Bar: Findings

Implements `docs/superpowers/specs/2026-08-10-translucent-statusbar-design.md`.
Makes `circuitsword-statusbar`'s background uniformly 80% opaque instead
of fully opaque black, so the running game stays partially visible
through the bar strip. Icons (battery/WiFi/volume/brightness) stay fully
opaque and legible on top of it.

## What was built

- `quickmenu.h`: new `QM_ARGB(a, r, g, b)` macro alongside the existing
  alpha-less `QM_RGB`.
- `qm_icons.c`'s `qm_draw_icon_rgba()`: every pixel it draws now
  explicitly carries alpha=255 (`QM_ARGB(255, ...)` instead of
  `QM_RGB(...)`) -- a shared-primitive change, harmless for
  `circuitsword-quickmenu` (still `XRGB8888`, alpha byte ignored), and
  what keeps icons fully opaque against the status bar's now-translucent
  background.
- `sb_render.c`: background fill changed from opaque `QM_COLOR_BG` to
  `QM_ARGB(204, 0, 0, 0)` (204/255 ~= 80% opacity).
- `sb_wl.c`: `wl_shm` buffer format changed from `WL_SHM_FORMAT_XRGB8888`
  to `WL_SHM_FORMAT_ARGB8888`; the `wl_surface_set_opaque_region(...)`
  hint (which told labwc it could skip blending this surface) removed
  entirely, since the surface is no longer opaque.
- `circuitsword-quickmenu`'s own `qm_wl.c` is untouched -- stays
  `XRGB8888`, stays fully opaque, by original design.

## Verified off-device

- `tests/test_qm_icons.c`: new check confirming every pixel
  `qm_draw_icon_rgba()` draws carries alpha=255, regardless of the input
  draw color's own (irrelevant, always-0) alpha bits.
- `tests/test_sb_render.c`: updated background-equality checks (and the
  `rightmost_nonbg_x()` helper) to compare against the actual new
  translucent background value instead of the old opaque `QM_COLOR_BG`;
  new check confirming the background's alpha byte is exactly 204.
- Incremental Buildroot compiles (`PKG=circuitsword-quickmenu-rebuild`
  and `PKG=circuitsword-statusbar-rebuild`) both succeed.
- Patch capture confirmed to contain the new code via targeted `grep`.

## Needs on-device validation (not yet done)

- **Whether labwc actually honors `ARGB8888` translucency for a
  `wlr-layer-shell` surface.** Every prior overlay surface on this
  hardware (`circuitsword-quickmenu`'s full-screen menu, and the status
  bar's own previous opaque incarnation) has been fully opaque -- this is
  the first translucent layer-shell surface attempted on this hardware/
  compositor combination. The mechanism (`ARGB8888` buffer + no opaque
  region hint) is the standard Wayland approach and should work, but this
  is genuinely new ground for this project and must be confirmed on the
  real device, not assumed.
- **Whether 80% is a good balance** between "status info stays legible"
  and "game stays visible" at actual DPI panel brightness/contrast --
  chosen as a reasonable starting value, not tuned against real hardware.
- **Any compositing artifacts** specific to alpha-blended layer-shell
  surfaces (flicker, incorrect blend order, edge artifacts) that a
  fully-opaque surface wouldn't have shown.
