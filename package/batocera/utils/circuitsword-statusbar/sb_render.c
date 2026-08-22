/* Pure drawing for circuitsword-statusbar: volume, WiFi, battery laid out
 * left-to-right across a thin top bar, anchored to the right edge (per
 * on-device feedback: the icons sat on the left, unlike the rest of this
 * project's overlays; brightness was dropped since it duplicates the
 * MODE+Left/Right hardware combo's own on-screen feedback and just
 * crowded the row). No I/O, no Wayland -- reuses circuitsword-quickmenu's
 * qm_icons.c/qm_icon_data.c primitives and QM_COLOR_* palette so the bar
 * matches the same look as the quickmenu overlay. Host-unit-tested in
 * tests/test_sb_render.c. */
#include "statusbar.h"

void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness,
               int battery_percent, int battery_charging)
{
    /* brightness has no icon on this bar (duplicates the MODE+Left/Right
     * hardware combo's own on-screen feedback and just crowded the row) --
     * kept as a parameter to preserve statusbar.h's existing call contract
     * with statusbar.c, unused here. Volume DOES select an icon level
     * again (see qm_volume_icon_kind()) -- on-device feedback flagged
     * that a fixed volume glyph gave no indication of the actual level. */
    (void)brightness;

    /* Three fixed-size (QM_ICON_SIZE px) icons, same on both panels (no
     * runtime scaling). margin(6) + 3*QM_ICON_SIZE(72) + 2*gap(24) =
     * 6+72+24 = 102px, comfortably under both the 640px primary and 320px
     * alt panel widths. */
    const int margin = 6;
    const int gap = 12;
    const int row_w = 3 * QM_ICON_SIZE + 2 * gap;

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_ARGB(204, 0, 0, 0));

    int y = ((int)fb->height - QM_ICON_SIZE) / 2;
    int x = (int)fb->width - margin - row_w;

    qm_draw_icon_rgba(fb, x, y, qm_volume_icon_kind(volume), QM_COLOR_FG);
    x += QM_ICON_SIZE + gap;

    qm_draw_icon_rgba(fb, x, y, QM_ICON_WIFI, wifi_on ? QM_COLOR_FG : QM_COLOR_DIM);
    x += QM_ICON_SIZE + gap;

    qm_draw_icon_rgba(fb, x, y, qm_battery_icon_kind(battery_percent, battery_charging),
                       QM_COLOR_FG);
}
