/* Pure drawing into a 32bpp XRGB8888 buffer. No syscalls -- this file is
 * compiled by the host compiler in tests/run-c-tests.sh as well as by the
 * Buildroot cross toolchain.
 *
 * Icons: real vector-sourced art (Batocera/EmulationStation's own battery
 * and WiFi icons, plus newly hand-drawn icons for volume/brightness, the
 * title wordmark, and the four button badges, all in the same flat-
 * silhouette style), each baked to its own fixed width/height alpha mask
 * by the developer-run tools/convert-icons.py -- see icons/src/svg files and
 * the generated qm_icon_data.c. This file only does the alpha-blend blit
 * and the battery-state mapping; it has no bitmap data of its own. See
 * docs/superpowers/specs/2026-08-10-real-overlay-icons-design.md
 * (including its title/badge addendum). */
#include "quickmenu.h"

uint8_t qm_alpha_blend(uint8_t bg, uint8_t fg, uint8_t alpha)
{
    return (uint8_t)(bg + ((int)fg - (int)bg) * alpha / 255);
}

void qm_draw_icon_rgba(qm_fb *fb, int x, int y, qm_icon_kind kind, uint32_t color)
{
    if (kind < 0 || kind >= QM_ICON_COUNT)
        return;

    const qm_icon_asset *asset = &qm_icon_assets[kind];
    const uint8_t *alpha = asset->alpha;
    uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
    uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
    uint8_t cb = (uint8_t)(color & 0xFF);

    for (int row = 0; row < asset->height; row++) {
        for (int col = 0; col < asset->width; col++) {
            uint8_t a = alpha[row * asset->width + col];
            if (a == 0)
                continue;

            uint32_t bg = qm_get_pixel(fb, x + col, y + row);
            uint8_t br = (uint8_t)((bg >> 16) & 0xFF);
            uint8_t bgc = (uint8_t)((bg >> 8) & 0xFF);
            uint8_t bb = (uint8_t)(bg & 0xFF);

            uint32_t blended = QM_ARGB(255,
                qm_alpha_blend(br, cr, a),
                qm_alpha_blend(bgc, cg, a),
                qm_alpha_blend(bb, cb, a));
            qm_fill_rect(fb, x + col, y + row, 1, 1, blended);
        }
    }
}

qm_icon_kind qm_battery_icon_kind(int percent, int charging)
{
    if (charging)
        return QM_ICON_BATTERY_CHARGING;
    if (percent < 10)
        return QM_ICON_BATTERY_EMPTY;
    if (percent < 35)
        return QM_ICON_BATTERY_25;
    if (percent < 60)
        return QM_ICON_BATTERY_50;
    if (percent < 85)
        return QM_ICON_BATTERY_75;
    return QM_ICON_BATTERY_FULL;
}

qm_icon_kind qm_volume_icon_kind(int percent)
{
    if (percent <= 0)
        return QM_ICON_VOLUME_MUTE;
    if (percent <= 50)
        return QM_ICON_VOLUME_LOW;
    return QM_ICON_VOLUME;
}
