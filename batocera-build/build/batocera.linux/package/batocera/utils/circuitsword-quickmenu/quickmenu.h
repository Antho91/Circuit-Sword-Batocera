#ifndef QUICKMENU_H
#define QUICKMENU_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Framebuffer: 32-bit XRGB8888, which is what the wl_shm buffer       */
/* created in qm_wl.c uses (WL_SHM_FORMAT_XRGB8888). `pixels` may be   */
/* an mmap of shared memory or plain malloc'd memory (host unit tests  */
/* use the latter) -- nothing in the drawing code knows the difference.*/
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;   /* bytes per row, >= width * 4 */
} qm_fb;

#define QM_RGB(r, g, b) \
    (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define QM_ARGB(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define QM_COLOR_BG       QM_RGB(0x00, 0x00, 0x00)   /* black - matches Batocera's current on-device menu background */
#define QM_COLOR_FG       QM_RGB(0xDD, 0xDD, 0xDD)   /* es-theme-carbon systemInfoColor */
#define QM_COLOR_DIM       QM_RGB(0x80, 0x80, 0x80)   /* neutral gray, for unselected/secondary text against black */
#define QM_COLOR_SEL_BG    QM_RGB(0x8B, 0x00, 0x00)   /* es-theme-carbon baseColor - selection/header fill */
#define QM_COLOR_BAR_BG    QM_RGB(0x30, 0x30, 0x30)   /* dark neutral gray, for bar track against black */
#define QM_COLOR_BAR_FG    QM_RGB(0xBD, 0x47, 0x47)   /* es-theme-carbon groupColor - bar fill / active accent */

/* ------------------------------------------------------------------ */
/* qm_font.c — pure drawing, no syscalls, host-unit-testable           */
/* ------------------------------------------------------------------ */
#define QM_GLYPH_W 5   /* glyph cell, in unscaled pixels */
#define QM_GLYPH_H 7
#define QM_GLYPH_ADVANCE 6   /* QM_GLYPH_W + 1px spacing */

void qm_fill_rect(qm_fb *fb, int x, int y, int w, int h, uint32_t color);
void qm_draw_char(qm_fb *fb, int x, int y, char c, int scale, uint32_t color);
void qm_draw_text(qm_fb *fb, int x, int y, const char *s, int scale, uint32_t color);
int  qm_text_width(const char *s, int scale);
uint32_t qm_get_pixel(const qm_fb *fb, int x, int y);

/* ------------------------------------------------------------------ */
/* qm_ttf.c -- real FreeType-rendered text, replaces qm_font.c's       */
/* bitmap font everywhere text appears in this overlay. Falls back to  */
/* qm_font.c automatically if FreeType init/font load ever fails --    */
/* callers never need to check availability themselves.                */
/* ------------------------------------------------------------------ */
#define QM_TTF_FONT_PATH "/usr/share/circuitsword-quickmenu/Cabin-Regular.ttf"

/* Call once at startup. Returns 0 on success, -1 on failure (logs the
 * reason to stderr either way). On failure, qm_draw_text_ttf() and
 * qm_ttf_text_width() transparently fall back to qm_font.c's
 * qm_draw_text()/qm_text_width() -- callers don't need to branch. */
int qm_ttf_init(void);

/* Renders `text` at `px_size` pixels tall, alpha-blending each glyph
 * into `fb` via the same qm_alpha_blend() primitive qm_icons.c uses.
 * `color` is the fill color (QM_RGB/QM_ARGB), same convention as
 * qm_draw_icon_rgba(). Falls back to qm_draw_text() (at a scale
 * approximating px_size / QM_GLYPH_H) if FreeType is unavailable. */
void qm_draw_text_ttf(qm_fb *fb, int x, int y, const char *text, int px_size, uint32_t color);

/* Pixel width `text` would occupy at `px_size`, for centering/right-
 * alignment. Falls back to qm_text_width() (scaled) if FreeType is
 * unavailable. */
int qm_ttf_text_width(const char *text, int px_size);

/* ------------------------------------------------------------------ */
/* qm_icons.c / qm_icon_data.c — real vector-sourced icons, baked to   */
/* fixed-size alpha-only bitmaps by the developer-run                  */
/* tools/convert-icons.py (see that script and icons/src/svg files --  */
/* NOT part of the Buildroot build). Every asset renders at its own    */
/* baked pixel size on BOTH panel resolutions -- no runtime scaling.   */
/* Pure drawing, no syscalls, host-testable.                           */
/* ------------------------------------------------------------------ */
#define QM_ICON_SIZE 24   /* width and height of the 10 status icons + 4 badges */
#define QM_TITLE_W   200  /* the title wordmark is wide, not square */
#define QM_TITLE_H   28
#define QM_TOGGLE_ICON_W 32   /* width of the toggle-switch icon (non-square, unlike QM_ICON_SIZE) */
#define QM_TOGGLE_ICON_H 16   /* height of the toggle-switch icon */

typedef enum {
    QM_ICON_BATTERY_EMPTY,
    QM_ICON_BATTERY_25,
    QM_ICON_BATTERY_50,
    QM_ICON_BATTERY_75,
    QM_ICON_BATTERY_FULL,
    QM_ICON_BATTERY_CHARGING,
    QM_ICON_WIFI,
    QM_ICON_VOLUME,
    QM_ICON_VOLUME_MUTE,
    QM_ICON_VOLUME_LOW,
    QM_ICON_BRIGHTNESS,
    QM_ICON_JOYSTICK,
    QM_ICON_SETTINGS,
    QM_ICON_TOGGLE_ON,
    QM_ICON_TOGGLE_OFF,
    QM_ICON_TITLE,
    QM_ICON_BADGE_A,
    QM_ICON_BADGE_B,
    QM_ICON_BADGE_LEFT,
    QM_ICON_BADGE_RIGHT,
    QM_ICON_COUNT
} qm_icon_kind;

typedef struct {
    int width;
    int height;
    const uint8_t *alpha;   /* width * height bytes, row-major, one byte
                                per pixel, 0 = transparent, 255 = fully
                                the draw color */
} qm_icon_asset;

/* Defined in the generated qm_icon_data.c (tools/convert-icons.py). */
extern const qm_icon_asset qm_icon_assets[QM_ICON_COUNT];

/* Pure, no I/O: (bg, fg, alpha) -> blended channel value. Exposed for
 * direct unit testing of the blend math, independent of any real icon
 * data -- see tests/test_qm_icons.c. */
uint8_t qm_alpha_blend(uint8_t bg, uint8_t fg, uint8_t alpha);

/* Alpha-blends `color` over the framebuffer at (x,y) using the icon's
 * own baked width/height alpha mask -- no runtime scaling, every asset
 * draws at its exact baked pixel size. Out-of-range kind no-ops (same
 * defensive default the prior bitmap version had). */
void qm_draw_icon_rgba(qm_fb *fb, int x, int y, qm_icon_kind kind, uint32_t color);

/* Pure, no I/O. charging always wins regardless of percent. Boundaries:
 * <10 -> EMPTY, <35 -> 25, <60 -> 50, <85 -> 75, else -> FULL. */
qm_icon_kind qm_battery_icon_kind(int percent, int charging);

/* Pure, no I/O. 3-tier volume level for the statusbar (the main-menu
 * volume row shows an exact percent via its bar+text instead, so it
 * stays on the plain QM_ICON_VOLUME icon). Boundaries: 0 -> MUTE,
 * 1..50 -> LOW, >50 -> QM_ICON_VOLUME (the existing two-arc icon, used
 * as the "high" tier). */
qm_icon_kind qm_volume_icon_kind(int percent);

/* ------------------------------------------------------------------ */
/* qm_input.c — raw evdev on the Arduino Leonardo joystick             */
/*                                                                     */
/* Joysticks never route through Wayland -- evdev is the only path     */
/* regardless of compositor -- so this layer is identical to what a    */
/* non-Wayland build would use.                                        */
/* ------------------------------------------------------------------ */
enum qm_event {
    QM_EV_NONE = 0,
    QM_EV_UP,
    QM_EV_DOWN,
    QM_EV_LEFT,
    QM_EV_RIGHT,
    QM_EV_A,
    QM_EV_B
};

int  qm_input_open(void);                 /* fd on success, -1 on failure */
void qm_input_close(int fd);
enum qm_event qm_input_poll(int fd, int timeout_ms);

/* ------------------------------------------------------------------ */
/* qm_settings.c — each call reuses the primitive the existing         */
/* Batocera counterpart already uses; one source of truth per setting  */
/* ------------------------------------------------------------------ */
int qm_wifi_get(void);                    /* 0, 1, or -1 on error */
int qm_wifi_set(int enabled);             /* 0 ok, -1 error */
int qm_volume_get(void);                  /* 0..100, or -1 on error */
int qm_volume_set(int percent);           /* 0 ok, -1 error */
int qm_brightness_get(void);              /* 0..100, or -1 on error */
int qm_brightness_set(int percent);       /* 0 ok, -1 error */
/* Pure logic (no I/O), host-unit-testable in isolation -- see
 * tests/test_qm_settings.c. Clamps raw_capacity to 0..100; -1 means the
 * error sentinel (capacity file unreadable), not just "out of range". */
int qm_battery_parse(int raw_capacity, const char *status, int *percent, int *charging);
int qm_battery_get(int *percent, int *charging);   /* 0 ok, -1 on error */
/* Reads /sys/class/thermal/thermal_zone0/temp directly (same path and
 * millidegree->C conversion + sanity range as the daemon's own
 * read_cpu_temp_c()) -- no daemon round trip needed. 0 ok, -1 on error
 * (file missing/unreadable, or the value fails the 0..160C sanity check). */
int qm_cpu_temp_get(double *out);

/* ------------------------------------------------------------------ */
/* qm_joystick.c -- Unix-domain-socket client to rpi-circuitsword.py's */
/* joystick_ipc_thread. circuitsword-quickmenu never touches           */
/* /dev/ttyACM0 directly -- the daemon remains the sole serial owner.  */
/* ------------------------------------------------------------------ */
#define QM_JOYSTICK_SOCK_PATH "/var/run/circuitsword-joystick.sock"

/* One poll attempt of an in-progress CALIBRATE round trip. On the
 * first call, pass *conn_fd == -1: this function opens the connection,
 * sends "CALIBRATE\n", stores the new fd in *conn_fd, and does one
 * short (~200ms) recv() attempt. On subsequent calls (with the same
 * *conn_fd still set), it does another short recv() attempt on the
 * existing connection -- no new connect/send. Returns:
 *    1  = final reply "OK" was read (caller must close(*conn_fd))
 *    0  = final reply "ERR ..." was read, or a connection error
 *         occurred (caller must close(*conn_fd) if >= 0)
 *   -1  = still waiting, no reply yet this poll (caller keeps polling,
 *         *conn_fd stays open and valid) */
int qm_joystick_calibrate_poll(int *conn_fd);

/* Connects, sends `cmd` + "\n" (one of "INVERT_J1X"/"INVERT_J1Y"/
 * "INVERT_J2X"/"INVERT_J2Y"/"TOGGLE_J1"/"TOGGLE_J2"), reads one line
 * with a ~1s timeout, closes the connection. Returns 1 on "OK", 0 on
 * any failure (connection error, timeout, "ERR" reply). */
int qm_joystick_toggle(const char *cmd);

/* Connects, sends "STATUS\n", reads the 6-character bit-string reply
 * into out[0..5] (NOT null-terminated -- out must be at least 6 bytes),
 * closes the connection. Returns 0 on success, -1 on any failure
 * (connection refused, timeout, malformed reply -- caller treats -1 as
 * "unknown state", matching qm_wifi_get()'s -1-on-error convention). */
int qm_joystick_status(char out[6]);

/* Connects, sends "GET_CONFIG\n", parses the reply
 * "fan_on_temp=..,fan_off_temp=..,fan_poll_interval_s=..,
 * switch_debounce_ms=..,fan_on=..,fan_enabled=.." into the 6
 * out-parameters. Returns 0 on success, -1 on any failure (connection
 * error, timeout, malformed reply) -- out-parameters are left
 * unmodified on failure, caller should keep showing its last-known
 * values. */
int qm_daemon_config_get(double *fan_on_temp, double *fan_off_temp,
                          int *fan_poll_interval_s, int *switch_debounce_ms,
                          int *fan_on, int *fan_enabled);

/* Connects, sends "RELOAD_CONFIG\n", reads one line. Returns 1 on "OK",
 * 0 on any failure (this command always replies OK from the daemon
 * side, so 0 here means a connection-level failure, not a rejected
 * reload). */
int qm_daemon_config_reload(void);

/* Writes the 5 tunables to /userdata/system/configs/circuitsword.conf,
 * overwriting the whole file (this file has exactly these 5 known keys
 * today -- no comment/unknown-line preservation attempted, matching the
 * simplicity of the file format itself). Returns 0 on success, -1 on
 * write failure (caller must not send RELOAD_CONFIG if this fails). */
int qm_daemon_config_write(double fan_on_temp, double fan_off_temp,
                            int fan_poll_interval_s, int switch_debounce_ms,
                            int fan_enabled);

/* ------------------------------------------------------------------ */
/* qm_wl.c — Wayland overlay-layer surface backed by wl_shm            */
/*                                                                     */
/* Replaces the abandoned libdrm/KMS backend entirely. labwc stays DRM */
/* master the whole time; we are just another client of it, on the     */
/* topmost (overlay) layer, above the running fullscreen RetroArch.    */
/* ------------------------------------------------------------------ */
typedef struct qm_wl qm_wl;

/* Connect, bind wl_compositor/wl_shm/zwlr_layer_shell_v1, create a
 * full-screen overlay-layer surface, and block until the compositor's
 * first configure has been handled and the shm buffer exists.
 * Returns NULL on any failure (no compositor, no layer-shell global,
 * no shm, configure never arrived). */
qm_wl *qm_wl_open(void);

/* Borrowed pointer to the shm-backed framebuffer, valid until
 * qm_wl_close(). Never NULL for a non-NULL qm_wl. */
qm_fb *qm_wl_fb(qm_wl *w);

/* Attach + damage + commit + flush. 0 ok, -1 error. */
int qm_wl_present(qm_wl *w);

/* One iteration of the merged event loop: prepare/flush the Wayland
 * connection, poll BOTH the Wayland fd and `input_fd` for up to
 * timeout_ms, read+dispatch any Wayland events, and report whether
 * `input_fd` has data waiting.
 * Returns  1  input_fd is readable (caller should call qm_input_poll)
 *          0  timeout or Wayland-only activity
 *         -1  connection error, or the compositor closed our surface */
int qm_wl_pump(qm_wl *w, int input_fd, int timeout_ms);

/* Destroy the layer surface and disconnect. Safe on NULL. */
void qm_wl_close(qm_wl *w);

/* ------------------------------------------------------------------ */
/* quickmenu.c — menu model + rendering                                */
/* ------------------------------------------------------------------ */
#define QM_ITEM_WIFI            0
#define QM_ITEM_VOLUME          1
#define QM_ITEM_BRIGHTNESS      2
#define QM_ITEM_JOYSTICK        3
#define QM_ITEM_DAEMON_SETTINGS 4
#define QM_ITEM_COUNT           5

#define QM_SCREEN_MAIN            0
#define QM_SCREEN_JOYSTICK        1
#define QM_SCREEN_DAEMON_SETTINGS 2

#define QM_JOY_CALIBRATE      0
#define QM_JOY_INVERT_J1X     1
#define QM_JOY_INVERT_J1Y     2
#define QM_JOY_INVERT_J2X     3
#define QM_JOY_INVERT_J2Y     4
#define QM_JOY_TOGGLE_J1      5
#define QM_JOY_TOGGLE_J2      6
#define QM_JOY_COUNT          7

#define QM_DS_FAN_ENABLED    0
#define QM_DS_FAN_ON_TEMP    1
#define QM_DS_FAN_OFF_TEMP   2
#define QM_DS_POLL_INTERVAL  3
#define QM_DS_DEBOUNCE_MS    4
#define QM_DS_STATUS_ROW     5
#define QM_DS_COUNT          6

typedef struct {
    int selected;      /* 0 .. QM_ITEM_COUNT-1, main screen only */
    int wifi_on;       /* 0 or 1 */
    int volume;        /* 0..100 */
    int brightness;    /* 0..100 */
    int screen;        /* QM_SCREEN_MAIN, QM_SCREEN_JOYSTICK, or
                           QM_SCREEN_DAEMON_SETTINGS */
    int joy_selected;  /* 0 .. QM_JOY_COUNT-1, joystick screen only */
    char joy_status[6]; /* cached STATUS reply: index0=iscalib1,
                            1=iscalib2, 2=xinvert1, 3=yinvert1,
                            4=xinvert2, 5=yinvert2 -- refreshed on
                            submenu entry and after any successful
                            toggle */
    int ds_selected;         /* 0 .. QM_DS_COUNT-1, daemon-settings screen only */
    int ds_fan_enabled;       /* 0 or 1 -- gates fan_thread()'s hysteresis on the
                                  daemon side; the two temperature rows below stay
                                  visible/editable regardless of this value */
    double ds_fan_on_temp;
    double ds_fan_off_temp;
    int ds_fan_poll_interval_s;
    int ds_switch_debounce_ms;
    int ds_fan_on;            /* live state from the daemon, 0 or 1 */
    int ds_loaded;            /* 0 until the first successful GET_CONFIG this session --
                                  MUST gate the A-handler: writing zero-initialized
                                  values (fan_poll_interval_s=0, switch_debounce_ms=0,
                                  ...) to the config file and reloading them live is a
                                  real hazard (fan-control busy-loop, spurious shutdown,
                                  fan latched on), so A is a no-op on every row while
                                  this is still 0, and qm_render() shows "Daemon
                                  unreachable" instead of the zeroed fields. */
    char ds_message[24];      /* transient status/error line for the settings screen
                                  ("Save failed" / "Reload failed" / "Refresh failed"),
                                  shown in place of the status row's fan/CPU text.
                                  Empty string = no message. Cleared on fresh submenu
                                  entry and after any fully-successful write+reload. */
    double ds_cpu_temp;       /* last known CPU temp in C, read client-side via
                                  qm_cpu_temp_get() (no daemon round trip) */
    int ds_cpu_temp_ok;       /* 0 until ds_cpu_temp has been successfully read
                                  this screen visit */
} qm_state;

void qm_render(qm_fb *fb, const qm_state *st);

#endif /* QUICKMENU_H */
