#ifndef STATUSBAR_H
#define STATUSBAR_H

/* qm_fb, QM_RGB, QM_COLOR_*, qm_fill_rect/qm_get_pixel/qm_draw_icon_rgba, and
 * qm_wifi_get/qm_volume_get/qm_brightness_get/qm_battery_get are all owned
 * by circuitsword-quickmenu and reused here as-is -- found via the -I path
 * into that package's directory set in circuitsword-statusbar.mk. (Unlike
 * quickmenu.c, sb_render.c never calls qm_draw_text/qm_text_width -- it
 * draws only fixed-size icons, no text.) See
 * docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md. */
#include "quickmenu.h"

/* ---------------- sb_wl.c -- top-anchored overlay-layer surface -------- */
typedef struct sb_wl sb_wl;

sb_wl   *sb_wl_open(void);
qm_fb   *sb_wl_fb(sb_wl *w);
int      sb_wl_present(sb_wl *w);
/* No input_fd parameter, unlike qm_wl_pump: this surface never grabs
 * input, so there is nothing to poll besides the Wayland connection
 * itself. Returns 0 on timeout, -1 on connection error or the compositor
 * closing our surface. */
int      sb_wl_pump(sb_wl *w, int timeout_ms);
void     sb_wl_close(sb_wl *w);

/* ---------------- sb_render.c -- pure drawing, host-unit-testable ------ */
void sb_render(qm_fb *fb, int wifi_on, int volume, int brightness,
               int battery_percent, int battery_charging);

#endif /* STATUSBAR_H */
