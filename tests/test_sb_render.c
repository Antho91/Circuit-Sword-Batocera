/* Host-side unit tests for circuitsword-statusbar's pure drawing code
 * (sb_render.c). Compiled with the host cc against the real source files
 * -- no Wayland, no device. Run via tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "statusbar.h"

/* Matches sb_render.c's background fill exactly (translucent black,
 * alpha=204 ~= 80% opacity) -- SB_EXPECTED_BG (alpha=0) no longer matches
 * what this file's background pixels actually contain. */
#define SB_EXPECTED_BG QM_ARGB(204, 0, 0, 0)

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
 * framebuffer is background. Used to prove text content stays on-screen
 * (qm_fill_rect/qm_draw_text clip safely, but clipped content is simply
 * missing from view -- this catches that case). */
static int rightmost_nonbg_x(const qm_fb *fb)
{
    int rightmost = -1;
    for (uint32_t y = 0; y < fb->height; y++)
        for (uint32_t x = 0; x < fb->width; x++)
            if (qm_get_pixel(fb, (int)x, (int)y) != SB_EXPECTED_BG)
                if ((int)x > rightmost) rightmost = (int)x;
    return rightmost;
}

int main(void)
{
    printf("sb_render basic\n");
    {
        qm_fb *fb = make_fb(640, 40);
        sb_render(fb, 1, 50, 70, 87, 0);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == SB_EXPECTED_BG, "background painted");
        free_fb(fb);
    }

    printf("sb_render charging vs not\n");
    {
        qm_fb *a = make_fb(640, 40), *b = make_fb(640, 40);
        sb_render(a, 1, 50, 70, 87, 0);
        sb_render(b, 1, 50, 70, 87, 1);
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "charging indicator changes pixel output");
        free_fb(a); free_fb(b);
    }

    printf("sb_render wifi on vs off\n");
    {
        qm_fb *a = make_fb(640, 40), *b = make_fb(640, 40);
        sb_render(a, 1, 50, 70, 87, 0);
        sb_render(b, 0, 50, 70, 87, 0);
        check(memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->height) != 0,
              "wifi state changes pixel output");
        free_fb(a); free_fb(b);
    }

    printf("sb_render extreme values do not crash\n");
    {
        qm_fb *fb = make_fb(320, 40);
        sb_render(fb, 0, 0, 0, 0, 0);
        check(qm_get_pixel(fb, 0, 0) == SB_EXPECTED_BG, "zeroed state still paints bg");
        sb_render(fb, 1, 100, 100, 100, 1);
        check(qm_get_pixel(fb, 0, 0) == SB_EXPECTED_BG, "maxed state still paints bg");
        free_fb(fb);
    }

    printf("sb_render icon groups stay within screen width\n");
    {
        /* Fixed-width icons (unlike the old variable-width percentage
         * text) have no "worst case" value combination -- every reading
         * takes the same on-screen width. Still worth a regression test
         * on the narrower 320px alt panel (scale 2), which the old
         * text-based layout was known not to fit. */
        qm_fb *fb = make_fb(320, 40);
        sb_render(fb, 1, 100, 100, 100, 1);
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0, "icon render draws something");
        check(rightmost < (int)fb->width,
              "icon render's rightmost pixel stays within the 320px alt panel width");
        free_fb(fb);
    }

    printf("sb_render background is translucent (alpha=204, ~80%% opacity)\n");
    {
        qm_fb *fb = make_fb(640, 40);
        sb_render(fb, 1, 50, 70, 87, 0);
        uint32_t bg_pixel = qm_get_pixel(fb, 0, 0);
        check(((bg_pixel >> 24) & 0xFF) == 204,
              "background alpha byte is 204 (80%% opacity)");
        free_fb(fb);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
