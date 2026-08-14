/* Host-side unit tests for circuitsword-quickmenu's pure drawing code.
 * Compiled with the host cc against the real qm_font.c -- no Wayland,
 * no evdev, no device needed. Run via tests/run-c-tests.sh. */
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

/* Rightmost x-coordinate with a non-background pixel, or -1 if the whole
 * framebuffer is background. Same technique tests/test_sb_render.c uses to
 * prove drawn content (badges/labels on the hint line here) stays on
 * screen instead of silently clipping off the right edge. */
static int rightmost_nonbg_x(const qm_fb *fb)
{
    int rightmost = -1;
    for (uint32_t y = 0; y < fb->height; y++)
        for (uint32_t x = 0; x < fb->width; x++)
            if (qm_get_pixel(fb, (int)x, (int)y) != QM_COLOR_BG)
                if ((int)x > rightmost) rightmost = (int)x;
    return rightmost;
}

int main(void)
{
    printf("qm_fill_rect\n");
    {
        qm_fb *fb = make_fb(20, 10);
        qm_fill_rect(fb, 2, 3, 4, 5, QM_RGB(0xFF, 0x00, 0x00));
        check(qm_get_pixel(fb, 2, 3) == QM_RGB(0xFF, 0x00, 0x00), "top-left set");
        check(qm_get_pixel(fb, 5, 7) == QM_RGB(0xFF, 0x00, 0x00), "bottom-right set");
        check(qm_get_pixel(fb, 1, 3) == 0, "left of rect untouched");
        check(qm_get_pixel(fb, 6, 3) == 0, "right of rect untouched");
        check(count_nonzero(fb) == 20, "exactly w*h pixels set");
        free_fb(fb);
    }

    printf("qm_fill_rect clipping\n");
    {
        qm_fb *fb = make_fb(8, 8);
        qm_fill_rect(fb, -4, -4, 100, 100, QM_RGB(0x01, 0x02, 0x03));
        check(count_nonzero(fb) == 64, "oversized rect clipped to fb, no crash");
        qm_fill_rect(fb, 100, 100, 5, 5, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 64, "fully offscreen rect draws nothing");
        free_fb(fb);
    }

    printf("qm_text_width\n");
    {
        check(qm_text_width("", 1) == 0, "empty string is 0 wide");
        check(qm_text_width("A", 1) == QM_GLYPH_W, "one glyph at scale 1");
        check(qm_text_width("AB", 1) == QM_GLYPH_ADVANCE + QM_GLYPH_W,
              "two glyphs include inter-glyph spacing");
        check(qm_text_width("AB", 2) == 2 * (QM_GLYPH_ADVANCE + QM_GLYPH_W),
              "scale 2 doubles the width");
    }

    printf("qm_draw_char\n");
    {
        qm_fb *fb = make_fb(16, 16);
        qm_draw_char(fb, 0, 0, ' ', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "space draws nothing");
        qm_draw_char(fb, 0, 0, 'A', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) > 0, "'A' draws something");
        check(qm_get_pixel(fb, 1, 0) != 0, "'A' has a lit pixel at its apex row");
        free_fb(fb);
    }

    printf("qm_draw_char case folding + unknown chars\n");
    {
        qm_fb *up = make_fb(16, 16), *lo = make_fb(16, 16);
        qm_draw_char(up, 0, 0, 'W', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_char(lo, 0, 0, 'w', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(memcmp(up->pixels, lo->pixels, (size_t)up->pitch * up->height) == 0,
              "lowercase renders as uppercase");
        free_fb(up); free_fb(lo);

        qm_fb *fb = make_fb(16, 16);
        qm_draw_char(fb, 0, 0, '~', 1, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(fb) == 0, "unmapped char draws nothing, no crash");
        free_fb(fb);
    }

    printf("qm_draw_text scaling\n");
    {
        qm_fb *a = make_fb(64, 32), *b = make_fb(64, 32);
        qm_draw_text(a, 0, 0, "WIFI", 1, QM_RGB(0xFF, 0xFF, 0xFF));
        qm_draw_text(b, 0, 0, "WIFI", 2, QM_RGB(0xFF, 0xFF, 0xFF));
        check(count_nonzero(b) == 4 * count_nonzero(a),
              "scale 2 lights exactly 4x the pixels of scale 1");
        free_fb(a); free_fb(b);
    }

    printf("qm_render\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .selected = QM_ITEM_WIFI, .wifi_on = 1,
                        .volume = 50, .brightness = 70 };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");

        st.selected = QM_ITEM_BRIGHTNESS;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG,
              "re-render with a different selection still paints bg");

        st.volume = 0; st.brightness = 0; st.wifi_on = 0;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "zeroed state still paints bg");

        st.volume = 100; st.brightness = 100; st.wifi_on = 1;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "maxed state still paints bg");

        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "320x240 render draws something");
        check(rightmost < (int)fb->width,
              "320x240 render's rightmost pixel stays within the alt panel width");
        free_fb(fb);
    }

    printf("qm_render at 640x480 (primary panel, scale 4)\n");
    {
        qm_fb *fb = make_fb(640, 480);
        qm_state st = { .selected = QM_ITEM_WIFI, .wifi_on = 1,
                        .volume = 50, .brightness = 70 };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");

        st.selected = QM_ITEM_BRIGHTNESS;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG,
              "re-render with a different selection still paints bg");

        st.volume = 0; st.brightness = 0; st.wifi_on = 0;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "zeroed state still paints bg");

        st.volume = 100; st.brightness = 100; st.wifi_on = 1;
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "maxed state still paints bg");

        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "640x480 render draws something");
        check(rightmost < (int)fb->width,
              "640x480 render's rightmost pixel (hint line badges/labels) "
              "stays within the primary panel width");
        free_fb(fb);
    }

    printf("qm_render joystick submenu\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .screen = QM_SCREEN_JOYSTICK, .joy_selected = 0 };
        memcpy(st.joy_status, "101010", 6);
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");

        st.joy_selected = QM_JOY_COUNT - 1;
        memcpy(st.joy_status, "000000", 6);
        qm_render(fb, &st);
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG,
              "last row selected, zeroed status still paints bg");

        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "joystick submenu draws something");
        check(rightmost < (int)fb->width,
              "joystick submenu's rightmost pixel ([X]/[ ] indicators) "
              "stays within panel width");
        free_fb(fb);
    }

    printf("qm_render joystick submenu at 640x480\n");
    {
        qm_fb *fb = make_fb(640, 480);
        qm_state st = { .screen = QM_SCREEN_JOYSTICK, .joy_selected = 3 };
        memcpy(st.joy_status, "110011", 6);
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "640x480 joystick submenu stays within panel width");
        free_fb(fb);
    }

    printf("qm_render daemon settings screen\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS, .ds_selected = 0,
                         .ds_loaded = 1, .ds_fan_enabled = 1,
                         .ds_fan_on_temp = 58.0, .ds_fan_off_temp = 50.0,
                         .ds_fan_poll_interval_s = 3, .ds_switch_debounce_ms = 800,
                         .ds_fan_on = 1 };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "daemon settings screen stays within panel width");
        free_fb(fb);
    }

    printf("qm_render daemon settings screen at 640x480\n");
    {
        qm_fb *fb = make_fb(640, 480);
        qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS, .ds_selected = QM_DS_DEBOUNCE_MS,
                         .ds_loaded = 1, .ds_fan_enabled = 1,
                         .ds_fan_on_temp = 60.5, .ds_fan_off_temp = 45.0,
                         .ds_fan_poll_interval_s = 5, .ds_switch_debounce_ms = 1200,
                         .ds_fan_on = 0 };
        qm_render(fb, &st);
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "640x480 daemon settings screen stays within panel width");
        free_fb(fb);
    }

    printf("qm_render daemon settings screen with CPU temp and a message\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS, .ds_selected = QM_DS_FAN_ON_TEMP,
                         .ds_loaded = 1, .ds_fan_enabled = 1,
                         .ds_fan_on_temp = 58.0, .ds_fan_off_temp = 50.0,
                         .ds_fan_poll_interval_s = 3, .ds_switch_debounce_ms = 800,
                         .ds_fan_on = 1, .ds_cpu_temp = 59.2, .ds_cpu_temp_ok = 1 };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something with CPU temp in the status row");
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "CPU temp status row stays within panel width");

        strncpy(st.ds_message, "Save failed", sizeof(st.ds_message) - 1);
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something with a pending message");
        rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "message status row stays within panel width");
        free_fb(fb);
    }

    printf("qm_render daemon settings screen, daemon unreachable (ds_loaded == 0)\n");
    {
        qm_fb *fb = make_fb(320, 240);
        /* Zero-initialized qm_state -- matches how main() starts before
         * the first successful GET_CONFIG. Must NOT show plausible-looking
         * zero values (0.0C thresholds, 0s poll interval, 0ms debounce)
         * here -- that was the critical bug this test guards against. */
        qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "daemon-unreachable message stays within panel width");
        free_fb(fb);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
