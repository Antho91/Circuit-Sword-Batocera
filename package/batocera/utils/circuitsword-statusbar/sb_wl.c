/* Wayland overlay-layer output for circuitsword-statusbar.
 *
 * Adapted from circuitsword-quickmenu's qm_wl.c, which proved this
 * mechanism works on real hardware: labwc keeps DRM master the whole
 * time, RetroArch is just another Wayland client, and a
 * zwlr_layer_surface_v1 on the OVERLAY layer draws above even fullscreen
 * clients. The difference here: quickmenu anchors all four edges
 * (full-screen); this anchors only TOP+LEFT+RIGHT (a thin bar stretched
 * to the screen width, fixed height) and requests exclusive_zone 0 (does
 * not reserve screen space or push other surfaces around -- the game
 * underneath is unaffected). No evdev, no keyboard interactivity: this
 * surface is purely passive.
 *
 * Boilerplate sequence follows labwc's own clients/labnag.c and
 * clients/pool-buffer.c, with all cairo/pango/glib/wlroots use removed:
 * this file links only libwayland-client. */
#include "statusbar.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* Bar height is our own fixed choice (not inferred from the compositor,
 * since only the TOP edge is anchored -- LEFT+RIGHT stretch width
 * automatically, but height has no opposing anchor to infer from). Chosen
 * to comfortably fit the fixed QM_ICON_SIZE (24px) status icons, vertically
 * centered -- sb_render.c draws no text and has no `scale` variable, only
 * fixed-size icon draws via qm_draw_icon_rgba(). */
#define SB_BAR_HEIGHT 40
/* Used only if the compositor sends a 0 width despite LEFT+RIGHT anchor,
 * which it should not do. Matches this board's DPI panel (640x480). */
#define SB_FALLBACK_W 640

#define SB_LAYER_NAMESPACE "circuitsword-statusbar"
#define SB_CONFIGURE_ROUNDTRIPS 20

struct sb_wl {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer *buffer;
    uint32_t width;
    uint32_t height;
    int configured;
    int closed;
    size_t map_size;
    qm_fb fb;
};

/* ---------------- registry ---------------- */

static void sb_registry_global(void *data, struct wl_registry *reg,
                               uint32_t name, const char *iface,
                               uint32_t version)
{
    struct sb_wl *w = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        uint32_t v = (version < 4) ? version : 4;
        w->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, v);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        w->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t v = (version < 4) ? version : 4;
        w->layer_shell = wl_registry_bind(reg, name,
                                          &zwlr_layer_shell_v1_interface, v);
    }
}

static void sb_registry_global_remove(void *data, struct wl_registry *reg,
                                      uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener sb_registry_listener = {
    .global = sb_registry_global,
    .global_remove = sb_registry_global_remove,
};

/* ---------------- layer surface ---------------- */

static void sb_ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t width, uint32_t height)
{
    struct sb_wl *w = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w->configured)
        return;
    w->width = (width == 0) ? SB_FALLBACK_W : width;
    w->height = (height == 0) ? SB_BAR_HEIGHT : height;
    w->configured = 1;
}

static void sb_ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    ((struct sb_wl *)data)->closed = 1;
}

static const struct zwlr_layer_surface_v1_listener sb_ls_listener = {
    .configure = sb_ls_configure,
    .closed = sb_ls_closed,
};

/* ---------------- shm buffer ---------------- */

static int sb_anon_shm(void)
{
    for (int tries = 0; tries < 100; tries++) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        char name[64];
        snprintf(name, sizeof(name), "/circuitsword-statusbar-%x-%x",
                 (unsigned int)getpid(), (unsigned int)ts.tv_nsec);
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
        if (errno != EEXIST)
            return -1;
    }
    return -1;
}

static int sb_create_buffer(struct sb_wl *w)
{
    uint32_t stride = w->width * 4;
    size_t size = (size_t)stride * w->height;

    int fd = sb_anon_shm();
    if (fd < 0) {
        fprintf(stderr, "circuitsword-statusbar: shm_open failed (%s)\n",
                strerror(errno));
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        fprintf(stderr, "circuitsword-statusbar: ftruncate failed (%s)\n",
                strerror(errno));
        close(fd);
        return -1;
    }
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "circuitsword-statusbar: mmap failed (%s)\n",
                strerror(errno));
        close(fd);
        return -1;
    }
    memset(map, 0, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(w->shm, fd, (int32_t)size);
    if (pool == NULL) {
        munmap(map, size);
        close(fd);
        return -1;
    }
    w->buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)w->width,
                                          (int32_t)w->height, (int32_t)stride,
                                          WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    if (w->buffer == NULL) {
        munmap(map, size);
        return -1;
    }

    w->map_size = size;
    w->fb.pixels = (uint8_t *)map;
    w->fb.width = w->width;
    w->fb.height = w->height;
    w->fb.pitch = stride;
    return 0;
}

/* ---------------- public API ---------------- */

sb_wl *sb_wl_open(void)
{
    struct sb_wl *w = calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;

    if (getenv("XDG_RUNTIME_DIR") == NULL)
        setenv("XDG_RUNTIME_DIR", "/var/run", 1);
    const char *disp = getenv("WAYLAND_DISPLAY");
    w->display = wl_display_connect((disp != NULL) ? disp : "wayland-0");
    if (w->display == NULL) {
        fprintf(stderr, "circuitsword-statusbar: cannot connect to the "
                        "compositor (WAYLAND_DISPLAY=%s)\n",
                (disp != NULL) ? disp : "wayland-0");
        free(w);
        return NULL;
    }

    w->registry = wl_display_get_registry(w->display);
    wl_registry_add_listener(w->registry, &sb_registry_listener, w);
    if (wl_display_roundtrip(w->display) < 0) {
        fprintf(stderr, "circuitsword-statusbar: registry roundtrip failed\n");
        sb_wl_close(w);
        return NULL;
    }
    if (w->compositor == NULL || w->shm == NULL || w->layer_shell == NULL) {
        fprintf(stderr, "circuitsword-statusbar: compositor lacks %s%s%s\n",
                (w->compositor == NULL) ? "wl_compositor " : "",
                (w->shm == NULL) ? "wl_shm " : "",
                (w->layer_shell == NULL) ? "zwlr_layer_shell_v1 " : "");
        sb_wl_close(w);
        return NULL;
    }

    w->surface = wl_compositor_create_surface(w->compositor);
    if (w->surface == NULL) {
        sb_wl_close(w);
        return NULL;
    }

    /* Empty input region: this surface takes no pointer or touch input at
     * all (it has no input path whatsoever), so nothing should ever be
     * routed to it and stolen from the game underneath. */
    struct wl_region *empty = wl_compositor_create_region(w->compositor);
    if (empty != NULL) {
        wl_surface_set_input_region(w->surface, empty);
        wl_region_destroy(empty);
    }

    w->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        w->layer_shell, w->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, SB_LAYER_NAMESPACE);
    if (w->layer_surface == NULL) {
        fprintf(stderr, "circuitsword-statusbar: get_layer_surface failed\n");
        sb_wl_close(w);
        return NULL;
    }
    zwlr_layer_surface_v1_add_listener(w->layer_surface, &sb_ls_listener, w);
    /* TOP+LEFT+RIGHT only (no BOTTOM): a bar stretched to the full screen
     * width, anchored to the top edge, fixed height. Width 0 in set_size
     * is valid because LEFT+RIGHT are both anchored (stretches
     * automatically); height must be explicit since only TOP is anchored
     * on that axis. exclusive_zone 0 (unlike quickmenu's -1): this bar
     * does not reserve screen space or push other surfaces around -- it
     * just draws on the overlay layer, on top of whatever is there. */
    zwlr_layer_surface_v1_set_anchor(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(w->layer_surface, 0, SB_BAR_HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(w->layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    wl_surface_commit(w->surface);

    for (int i = 0; i < SB_CONFIGURE_ROUNDTRIPS && !w->configured && !w->closed; i++) {
        if (wl_display_roundtrip(w->display) < 0) {
            fprintf(stderr, "circuitsword-statusbar: roundtrip failed while "
                            "waiting for configure\n");
            sb_wl_close(w);
            return NULL;
        }
    }
    if (!w->configured || w->closed) {
        fprintf(stderr, "circuitsword-statusbar: no layer-surface configure "
                        "(configured=%d closed=%d)\n", w->configured, w->closed);
        sb_wl_close(w);
        return NULL;
    }

    if (sb_create_buffer(w) != 0) {
        sb_wl_close(w);
        return NULL;
    }

    return w;
}

qm_fb *sb_wl_fb(sb_wl *w)
{
    return (w == NULL) ? NULL : &w->fb;
}

int sb_wl_present(sb_wl *w)
{
    if (w == NULL || w->buffer == NULL)
        return -1;
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    wl_surface_damage(w->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(w->surface);
    if (wl_display_flush(w->display) < 0 && errno != EAGAIN) {
        fprintf(stderr, "circuitsword-statusbar: display flush failed (%s)\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

int sb_wl_pump(sb_wl *w, int timeout_ms)
{
    if (w == NULL || w->closed)
        return -1;

    while (wl_display_prepare_read(w->display) != 0) {
        if (wl_display_dispatch_pending(w->display) < 0)
            return -1;
    }
    errno = 0;
    if (wl_display_flush(w->display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(w->display);
        return -1;
    }

    struct pollfd pfd;
    pfd.fd = wl_display_get_fd(w->display);
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);

    if (pr > 0 && (pfd.revents & POLLIN)) {
        if (wl_display_read_events(w->display) < 0)
            return -1;
    } else {
        wl_display_cancel_read(w->display);
    }
    if (wl_display_dispatch_pending(w->display) < 0)
        return -1;
    if (w->closed)
        return -1;
    if (pr < 0 && errno != EINTR)
        return -1;
    return 0;
}

void sb_wl_close(sb_wl *w)
{
    if (w == NULL)
        return;
    if (w->buffer != NULL) {
        wl_buffer_destroy(w->buffer);
        w->buffer = NULL;
    }
    if (w->fb.pixels != NULL) {
        munmap(w->fb.pixels, w->map_size);
        w->fb.pixels = NULL;
    }
    if (w->layer_surface != NULL) {
        zwlr_layer_surface_v1_destroy(w->layer_surface);
        w->layer_surface = NULL;
    }
    if (w->surface != NULL) {
        wl_surface_destroy(w->surface);
        w->surface = NULL;
    }
    if (w->layer_shell != NULL) {
        zwlr_layer_shell_v1_destroy(w->layer_shell);
        w->layer_shell = NULL;
    }
    if (w->shm != NULL) {
        wl_shm_destroy(w->shm);
        w->shm = NULL;
    }
    if (w->compositor != NULL) {
        wl_compositor_destroy(w->compositor);
        w->compositor = NULL;
    }
    if (w->registry != NULL) {
        wl_registry_destroy(w->registry);
        w->registry = NULL;
    }
    if (w->display != NULL) {
        wl_display_flush(w->display);
        wl_display_disconnect(w->display);
        w->display = NULL;
    }
    free(w);
}
