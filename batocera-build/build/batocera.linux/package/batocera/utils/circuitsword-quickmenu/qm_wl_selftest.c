/* Smallest possible proof that qm_wl.c works on the real device:
 * create the overlay-layer surface, fill it with a solid colour, hold it
 * for a few seconds, exit cleanly.
 *
 * NOT installed into the image -- built alongside the real binary and
 * scp'd to the device by hand (Phase 4 v2 plan, Task 11).
 *
 * Usage: qm-wl-selftest [seconds]     (default 5)
 * Exit:  0 = surface was created and held; non-zero = could not create. */
#include "quickmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
    int seconds = 5;
    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0 && v <= 60)
            seconds = v;
    }

    qm_wl *w = qm_wl_open();
    if (w == NULL) {
        fprintf(stderr, "qm-wl-selftest: qm_wl_open() failed\n");
        return 1;
    }

    qm_fb *fb = qm_wl_fb(w);
    printf("qm-wl-selftest: surface %ux%u pitch=%u, holding %ds\n",
           fb->width, fb->height, fb->pitch, seconds);

    /* Solid magenta: unmistakable against any game frame. */
    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height,
                 QM_RGB(0xC0, 0x00, 0xC0));
    if (qm_wl_present(w) != 0) {
        fprintf(stderr, "qm-wl-selftest: qm_wl_present() failed\n");
        qm_wl_close(w);
        return 2;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) >= seconds)
            break;
        /* input_fd = -1: this test has no evdev device, it only pumps
         * the Wayland connection. */
        if (qm_wl_pump(w, -1, 100) < 0) {
            fprintf(stderr, "qm-wl-selftest: connection lost / surface closed\n");
            qm_wl_close(w);
            return 3;
        }
    }

    qm_wl_close(w);
    printf("qm-wl-selftest: done\n");
    return 0;
}
