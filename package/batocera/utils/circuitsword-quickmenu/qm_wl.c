/* Wayland overlay-layer output for circuitsword-quickmenu.
 *
 * This Batocera build runs labwc (wlroots-based) as EmulationStation's
 * persistent compositor, and RetroArch is a Wayland client of it. labwc
 * keeps /dev/dri/card0 (DRM master) the entire time. So the menu is just
 * another client: a wl_surface promoted to a zwlr_layer_surface_v1 on
 * the OVERLAY layer -- the topmost layer, drawn above even fullscreen
 * clients -- anchored to all four edges and filled from a wl_shm buffer.
 *
 * There is no VT switch, no drmSetMaster(), no display hand-off, and
 * therefore no blank-screen failure mode: if anything here fails we just
 * exit and the user never sees a change.
 *
 * Boilerplate sequence follows labwc's own clients/labnag.c and
 * clients/pool-buffer.c, with all cairo/pango/glib/wlroots use removed:
 * this file links only libwayland-client. */
#include "quickmenu.h"

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

/* Used only if the compositor sends a 0x0 configure, which it should not
 * do for a surface anchored to all four edges. Matches this board's DPI
 * panel (640x480, see batocera-drminfo output recorded in the findings
 * log). */
#define QM_FALLBACK_W 640
#define QM_FALLBACK_H 480

#define QM_LAYER_NAMESPACE "circuitsword-quickmenu"
#define QM_CONFIGURE_ROUNDTRIPS 20

struct qm_wl {
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

static void qm_registry_global(void *data, struct wl_registry *reg,
                               uint32_t name, const char *iface,
                               uint32_t version)
{
    struct qm_wl *w = data;
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

static void qm_registry_global_remove(void *data, struct wl_registry *reg,
                                      uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener qm_registry_listener = {
    .global = qm_registry_global,
    .global_remove = qm_registry_global_remove,
};

/* ---------------- layer surface ---------------- */

static void qm_ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t width, uint32_t height)
{
    struct qm_wl *w = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    /* Only the FIRST configure sizes us. The menu is opened, used and
     * closed in a couple of seconds on a fixed-resolution built-in
     * panel; a mid-session resize is not a case worth carrying code for,
     * and ignoring it is strictly safer than reallocating the buffer
     * under the renderer. */
    if (w->configured)
        return;
    w->width = (width == 0) ? QM_FALLBACK_W : width;
    w->height = (height == 0) ? QM_FALLBACK_H : height;
    w->configured = 1;
}

static void qm_ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    ((struct qm_wl *)data)->closed = 1;
}

static const struct zwlr_layer_surface_v1_listener qm_ls_listener = {
    .configure = qm_ls_configure,
    .closed = qm_ls_closed,
};

/* ---------------- shm buffer ---------------- */

static int qm_anon_shm(void)
{
    for (int tries = 0; tries < 100; tries++) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        char name[64];
        snprintf(name, sizeof(name), "/circuitsword-quickmenu-%x-%x",
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

static int qm_create_buffer(struct qm_wl *w)
{
    uint32_t stride = w->width * 4;
    size_t size = (size_t)stride * w->height;

    int fd = qm_anon_shm();
    if (fd < 0) {
        fprintf(stderr, "circuitsword-quickmenu: shm_open failed (%s)\n",
                strerror(errno));
        return -1;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        fprintf(stderr, "circuitsword-quickmenu: ftruncate failed (%s)\n",
                strerror(errno));
        close(fd);
        return -1;
    }
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        fprintf(stderr, "circuitsword-quickmenu: mmap failed (%s)\n",
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
                                          WL_SHM_FORMAT_XRGB8888);
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

qm_wl *qm_wl_open(void)
{
    struct qm_wl *w = calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;

    /* rpi-circuitsword.py passes these explicitly, but default to what
     * package/batocera/emulationstation/batocera-emulationstation/wayland/
     * labwc/04-labwc.sh exports, so the binary is also usable by hand
     * over SSH. */
    if (getenv("XDG_RUNTIME_DIR") == NULL)
        setenv("XDG_RUNTIME_DIR", "/var/run", 1);
    const char *disp = getenv("WAYLAND_DISPLAY");
    w->display = wl_display_connect((disp != NULL) ? disp : "wayland-0");
    if (w->display == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: cannot connect to the "
                        "compositor (WAYLAND_DISPLAY=%s)\n",
                (disp != NULL) ? disp : "wayland-0");
        free(w);
        return NULL;
    }

    w->registry = wl_display_get_registry(w->display);
    wl_registry_add_listener(w->registry, &qm_registry_listener, w);
    if (wl_display_roundtrip(w->display) < 0) {
        fprintf(stderr, "circuitsword-quickmenu: registry roundtrip failed\n");
        qm_wl_close(w);
        return NULL;
    }
    if (w->compositor == NULL || w->shm == NULL || w->layer_shell == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: compositor lacks %s%s%s\n",
                (w->compositor == NULL) ? "wl_compositor " : "",
                (w->shm == NULL) ? "wl_shm " : "",
                (w->layer_shell == NULL) ? "zwlr_layer_shell_v1 " : "");
        qm_wl_close(w);
        return NULL;
    }

    w->surface = wl_compositor_create_surface(w->compositor);
    if (w->surface == NULL) {
        qm_wl_close(w);
        return NULL;
    }

    /* Empty input region: the menu takes no pointer or touch input at
     * all (everything comes from evdev), so nothing should be routed to
     * us and stolen from whatever is underneath. */
    struct wl_region *empty = wl_compositor_create_region(w->compositor);
    if (empty != NULL) {
        wl_surface_set_input_region(w->surface, empty);
        wl_region_destroy(empty);
    }

    w->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        w->layer_shell, w->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, QM_LAYER_NAMESPACE);
    if (w->layer_surface == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: get_layer_surface failed\n");
        qm_wl_close(w);
        return NULL;
    }
    zwlr_layer_surface_v1_add_listener(w->layer_surface, &qm_ls_listener, w);
    /* Anchored to all four edges with size 0x0 => the compositor tells us
     * the full output size in the configure event. Exclusive zone -1 means
     * "do not move me to accommodate panels, stretch to the anchored
     * edges". Keyboard interactivity none: Wayland keyboard focus is
     * irrelevant here, all input arrives via evdev. */
    zwlr_layer_surface_v1_set_anchor(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(w->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(w->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(w->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    /* Protocol requires an initial commit with NO buffer attached; the
     * compositor answers with configure, and only then may we attach. */
    wl_surface_commit(w->surface);

    for (int i = 0; i < QM_CONFIGURE_ROUNDTRIPS && !w->configured && !w->closed; i++) {
        if (wl_display_roundtrip(w->display) < 0) {
            fprintf(stderr, "circuitsword-quickmenu: roundtrip failed while "
                            "waiting for configure\n");
            qm_wl_close(w);
            return NULL;
        }
    }
    if (!w->configured || w->closed) {
        fprintf(stderr, "circuitsword-quickmenu: no layer-surface configure "
                        "(configured=%d closed=%d)\n", w->configured, w->closed);
        qm_wl_close(w);
        return NULL;
    }

    if (qm_create_buffer(w) != 0) {
        qm_wl_close(w);
        return NULL;
    }

    /* The whole surface is opaque -- tell the compositor so it can skip
     * blending and, on some paths, skip repainting what is underneath. */
    struct wl_region *opaque = wl_compositor_create_region(w->compositor);
    if (opaque != NULL) {
        wl_region_add(opaque, 0, 0, (int32_t)w->width, (int32_t)w->height);
        wl_surface_set_opaque_region(w->surface, opaque);
        wl_region_destroy(opaque);
    }

    return w;
}

qm_fb *qm_wl_fb(qm_wl *w)
{
    return (w == NULL) ? NULL : &w->fb;
}

int qm_wl_present(qm_wl *w)
{
    if (w == NULL || w->buffer == NULL)
        return -1;
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    /* wl_surface_damage (not damage_buffer) so this works at any bound
     * wl_surface version; INT32_MAX x INT32_MAX is the idiomatic
     * "everything" rectangle. */
    wl_surface_damage(w->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(w->surface);
    if (wl_display_flush(w->display) < 0 && errno != EAGAIN) {
        fprintf(stderr, "circuitsword-quickmenu: display flush failed (%s)\n",
                strerror(errno));
        return -1;
    }
    return 0;
}

int qm_wl_pump(qm_wl *w, int input_fd, int timeout_ms)
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

    struct pollfd pfds[2];
    pfds[0].fd = wl_display_get_fd(w->display);
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = input_fd;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;
    int nfds = (input_fd >= 0) ? 2 : 1;

    int pr = poll(pfds, (nfds_t)nfds, timeout_ms);

    if (pr > 0 && (pfds[0].revents & POLLIN)) {
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
    if (nfds == 2 && (pfds[1].revents & POLLIN))
        return 1;
    return 0;
}

void qm_wl_close(qm_wl *w)
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
