/* Host-side test for qm_ttf.c's fallback behavior. Does NOT call
 * qm_ttf_init() (which would try to load the real font and succeed if
 * libfreetype+the font file are available on the host, which is fine --
 * but this test specifically exercises the FALLBACK path, which is only
 * reachable when qm_ttf_init() was never called or failed, i.e. the
 * module-level availability flag stays at its 0 default). Run via
 * tests/run-c-tests.sh. */
#include <stdio.h>
#include <stdlib.h>
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

int main(void)
{
    printf("qm_draw_text_ttf without qm_ttf_init (fallback path)\n");
    {
        qm_fb *fb = make_fb(64, 32);
        qm_draw_text_ttf(fb, 0, 0, "AB", 14, QM_RGB(0xFF, 0xFF, 0xFF));
        int nonzero = 0;
        for (uint32_t y = 0; y < fb->height; y++)
            for (uint32_t x = 0; x < fb->width; x++)
                if (qm_get_pixel(fb, (int)x, (int)y) != 0) nonzero++;
        check(nonzero > 0, "fallback path still draws something (bitmap font)");
        free_fb(fb);
    }

    printf("qm_ttf_text_width without qm_ttf_init (fallback path)\n");
    {
        int w = qm_ttf_text_width("AB", 14);
        check(w > 0, "fallback text width is positive");
        free(NULL); /* no-op, keeps stdlib.h include meaningful if unused elsewhere */
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
