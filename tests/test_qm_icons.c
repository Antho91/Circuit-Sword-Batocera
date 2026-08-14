/* Host-side unit tests for circuitsword-quickmenu's real-icon drawing
 * code (qm_icons.c) and the generated icon data (qm_icon_data.c).
 * Compiled with the host cc against the real source -- no Wayland, no
 * evdev, no device needed. Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int count_nonzero(const qm_fb *fb)
{
    int n = 0;
    for (uint32_t y = 0; y < fb->height; y++)
        for (uint32_t x = 0; x < fb->width; x++)
            if (qm_get_pixel(fb, (int)x, (int)y) != 0) n++;
    return n;
}

int main(void)
{
    printf("qm_alpha_blend\n");
    {
        check(qm_alpha_blend(10, 200, 0) == 10, "alpha 0 leaves background untouched");
        check(qm_alpha_blend(10, 200, 255) == 200, "alpha 255 becomes exactly fg");
        uint8_t half = qm_alpha_blend(0, 254, 128);
        check(half >= 120 && half <= 132, "alpha 128 lands roughly halfway (0->254)");
    }

    printf("qm_battery_icon_kind boundaries\n");
    {
        check(qm_battery_icon_kind(0, 0) == QM_ICON_BATTERY_EMPTY, "0%% -> EMPTY");
        check(qm_battery_icon_kind(9, 0) == QM_ICON_BATTERY_EMPTY, "9%% -> EMPTY");
        check(qm_battery_icon_kind(10, 0) == QM_ICON_BATTERY_25, "10%% -> 25");
        check(qm_battery_icon_kind(34, 0) == QM_ICON_BATTERY_25, "34%% -> 25");
        check(qm_battery_icon_kind(35, 0) == QM_ICON_BATTERY_50, "35%% -> 50");
        check(qm_battery_icon_kind(59, 0) == QM_ICON_BATTERY_50, "59%% -> 50");
        check(qm_battery_icon_kind(60, 0) == QM_ICON_BATTERY_75, "60%% -> 75");
        check(qm_battery_icon_kind(84, 0) == QM_ICON_BATTERY_75, "84%% -> 75");
        check(qm_battery_icon_kind(85, 0) == QM_ICON_BATTERY_FULL, "85%% -> FULL");
        check(qm_battery_icon_kind(100, 0) == QM_ICON_BATTERY_FULL, "100%% -> FULL");
        check(qm_battery_icon_kind(50, 1) == QM_ICON_BATTERY_CHARGING,
              "charging always wins regardless of percent");
        check(qm_battery_icon_kind(0, 1) == QM_ICON_BATTERY_CHARGING,
              "charging wins even at 0%%");
    }

    printf("qm_draw_icon_rgba draws something for every real icon\n");
    {
        qm_icon_kind cases[] = {
            QM_ICON_BATTERY_EMPTY, QM_ICON_BATTERY_25, QM_ICON_BATTERY_50,
            QM_ICON_BATTERY_75, QM_ICON_BATTERY_FULL, QM_ICON_BATTERY_CHARGING,
            QM_ICON_WIFI, QM_ICON_VOLUME, QM_ICON_VOLUME_MUTE, QM_ICON_VOLUME_LOW,
            QM_ICON_BRIGHTNESS, QM_ICON_TITLE,
            QM_ICON_BADGE_A, QM_ICON_BADGE_B, QM_ICON_BADGE_LEFT, QM_ICON_BADGE_RIGHT,
            QM_ICON_TOGGLE_ON, QM_ICON_TOGGLE_OFF,
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            const qm_icon_asset *asset = &qm_icon_assets[cases[i]];
            qm_fb *fb = make_fb((uint32_t)asset->width, (uint32_t)asset->height);
            qm_draw_icon_rgba(fb, 0, 0, cases[i], QM_RGB(0xFF, 0xFF, 0xFF));
            check(count_nonzero(fb) > 0, "icon draws at least one pixel");
            free_fb(fb);
        }
    }

    printf("qm_draw_icon_rgba respects each asset's own width/height\n");
    {
        check(qm_icon_assets[QM_ICON_TITLE].width == QM_TITLE_W &&
              qm_icon_assets[QM_ICON_TITLE].height == QM_TITLE_H,
              "title asset is QM_TITLE_W x QM_TITLE_H, not QM_ICON_SIZE");
        check(qm_icon_assets[QM_ICON_BADGE_A].width == QM_ICON_SIZE &&
              qm_icon_assets[QM_ICON_BADGE_A].height == QM_ICON_SIZE,
              "badge asset is QM_ICON_SIZE square");
    }

    printf("qm_draw_icon_rgba distinguishes states\n");
    {
        qm_fb *a = make_fb(QM_ICON_SIZE, QM_ICON_SIZE), *b = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(a, 0, 0, QM_ICON_BATTERY_EMPTY, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon_rgba(b, 0, 0, QM_ICON_BATTERY_FULL, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "empty battery differs from full battery");
        free_fb(a); free_fb(b);

        qm_fb *c = make_fb(QM_ICON_SIZE, QM_ICON_SIZE), *d = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(c, 0, 0, QM_ICON_BADGE_A, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_icon_rgba(d, 0, 0, QM_ICON_BADGE_B, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(c->pixels, d->pixels, (size_t)c->pitch * c->height) != 0,
              "badge A differs from badge B");
        free_fb(c); free_fb(d);
    }

    printf("qm_draw_icon_rgba out-of-range kind no-ops instead of crashing\n");
    {
        qm_fb *fb = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(fb, 0, 0, (qm_icon_kind)-1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "negative kind draws nothing, no crash");
        qm_draw_icon_rgba(fb, 0, 0, QM_ICON_COUNT, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "QM_ICON_COUNT (one past last) draws nothing, no crash");
        free_fb(fb);
    }

    printf("qm_draw_icon_rgba writes full opacity for every pixel it touches\n");
    {
        qm_fb *fb = make_fb(QM_ICON_SIZE, QM_ICON_SIZE);
        qm_draw_icon_rgba(fb, 0, 0, QM_ICON_BATTERY_FULL, QM_RGB(0xFF, 0xFF, 0xFF));
        int bad_alpha = 0;
        for (uint32_t y = 0; y < fb->height; y++)
            for (uint32_t x = 0; x < fb->width; x++) {
                uint32_t p = qm_get_pixel(fb, (int)x, (int)y);
                if (p != 0 && ((p >> 24) & 0xFF) != 255)
                    bad_alpha++;
            }
        check(bad_alpha == 0,
              "every drawn pixel has alpha=255, none partially transparent");
        free_fb(fb);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
