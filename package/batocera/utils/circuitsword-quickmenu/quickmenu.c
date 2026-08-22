/* circuitsword-quickmenu: 3-item in-game menu for the Circuit-Sword.
 *
 * Launched by rpi-circuitsword.py after it has paused RetroArch. Draws
 * a full-screen opaque Wayland overlay-layer surface ON TOP of the still
 * running (paused) game -- there is no display hand-off, labwc keeps
 * compositing RetroArch's surface underneath the whole time.
 *
 * Exit contract (relied on by the daemon):
 *   0        normal close: B pressed, SIGTERM handled, or the compositor
 *            closed our surface. The daemon resumes the game.
 *   non-zero could not run at all (no input device, no compositor, no
 *            layer-shell global) -- the daemon resumes immediately, and
 *            nothing was ever drawn, so there is no visible glitch.
 *   SIGTERM  = "close now"; the surface is destroyed and we exit well
 *            inside the daemon's 5s watchdog.
 *
 * Compiling with -DQM_NO_MAIN builds only the pure qm_render() half, for
 * the host-side unit tests in tests/run-c-tests.sh. */
#include "quickmenu.h"

#include <stdio.h>
#include <string.h>

#define QM_STEP 5   /* % per left/right press, volume and brightness alike */

static void qm_draw_bar(qm_fb *fb, int x, int y, int w, int h, int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    qm_fill_rect(fb, x, y, w, h, QM_COLOR_BAR_BG);
    qm_fill_rect(fb, x, y, w * percent / 100, h, QM_COLOR_BAR_FG);
}

/* Short explanation of what pressing A does on the currently selected
 * Joystick submenu row -- addresses real on-device feedback that rows like
 * "Invert J1 X" gave no clue what the toggle actually affected. */
static const char *qm_joy_hint(int selected)
{
    switch (selected) {
    case QM_JOY_CALIBRATE:
        return "Rotate joysticks to calibrate";
    case QM_JOY_INVERT_J1X:
    case QM_JOY_INVERT_J1Y:
    case QM_JOY_INVERT_J2X:
    case QM_JOY_INVERT_J2Y:
        return "Reverses the direction of this axis";
    case QM_JOY_TOGGLE_J1:
    case QM_JOY_TOGGLE_J2:
        return "Enable/disable this joystick";
    default:
        return "";
    }
}

void qm_render(qm_fb *fb, const qm_state *st)
{
    const int scale = (fb->width >= 640) ? 4 : 2;
    const int px_size = (scale >= 4) ? 14 : 10;
    const int margin = 8 * scale;
    const int gap = 2 * scale;
    const int bar_h = 3 * scale;
    const int bar_x = margin + QM_ICON_SIZE + gap;
    const int row_h = QM_ICON_SIZE + 2 * scale;

    if (st->screen == QM_SCREEN_MAIN) {
    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

    int title_x = ((int)fb->width - QM_TITLE_W) / 2;
    qm_draw_icon_rgba(fb, title_x, margin, QM_ICON_TITLE, QM_COLOR_DIM);

    /* Icons are baked to a fixed pixel size regardless of panel resolution
     * (QM_ICON_SIZE, by design), but the vertical space between the title
     * and the bottom-anchored hint line grows a lot with `scale` (640px
     * primary panel vs 320px alt panel). Fill that space by spreading it
     * evenly across the gaps between title/rows/hint instead of using a
     * small fixed step and leaving one huge unexplained gap right before
     * the hint line. `hy` (the hint line's top) is computed here, ahead of
     * its own drawing code below, because the row loop needs it to work
     * out how much vertical space it has to fill. */
    int hy = (int)fb->height - margin - QM_ICON_SIZE;
    int title_bottom = margin + QM_TITLE_H;
    int content_rows_h = QM_ITEM_COUNT * QM_ICON_SIZE;
    int avail = hy - title_bottom;
    int slot_gap = (avail - content_rows_h) / (QM_ITEM_COUNT + 1);
    if (slot_gap < gap) slot_gap = gap;  /* floor: never tighter than the base gap */

    int y = title_bottom + slot_gap;

    for (int item = 0; item < QM_ITEM_COUNT; item++) {
        if (item == st->selected)
            qm_fill_rect(fb, margin / 2, y - scale,
                         (int)fb->width - margin, row_h, QM_COLOR_SEL_BG);

        uint32_t fg = (item == st->selected) ? QM_COLOR_FG : QM_COLOR_DIM;
        /* Vertically center the label against the fixed-size icon -- icons
         * are baked at QM_ICON_SIZE regardless of scale, but px_size (the
         * TTF-rendered glyph height) varies with it. */
        int label_y = y + (QM_ICON_SIZE - px_size) / 2;
        char label[32];

        if (item == QM_ITEM_WIFI) {
            uint32_t wifi_fg = st->wifi_on ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_WIFI, wifi_fg);
            int wifi_icon_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
            int wifi_icon_y = y + (QM_ICON_SIZE - QM_TOGGLE_ICON_H) / 2;
            qm_draw_icon_rgba(fb, wifi_icon_x, wifi_icon_y,
                               st->wifi_on ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF,
                               wifi_fg);
        } else if (item == QM_ITEM_VOLUME) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_VOLUME, fg);
            snprintf(label, sizeof(label), "Volume: %d%%", st->volume);
            int label_w = qm_ttf_text_width(label, px_size);
            int label_x = (int)fb->width - margin - label_w;
            int bar_w2 = label_x - gap - bar_x;
            if (bar_w2 < 0) bar_w2 = 0;
            qm_draw_bar(fb, bar_x, y + (QM_ICON_SIZE - bar_h) / 2, bar_w2, bar_h,
                        st->volume);
            qm_draw_text_ttf(fb, label_x, label_y, label, px_size, fg);
        } else if (item == QM_ITEM_BRIGHTNESS) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_BRIGHTNESS, fg);
            snprintf(label, sizeof(label), "Brightness: %d%%", st->brightness);
            int label_w = qm_ttf_text_width(label, px_size);
            int label_x = (int)fb->width - margin - label_w;
            int bar_w2 = label_x - gap - bar_x;
            if (bar_w2 < 0) bar_w2 = 0;
            qm_draw_bar(fb, bar_x, y + (QM_ICON_SIZE - bar_h) / 2, bar_w2, bar_h,
                        st->brightness);
            qm_draw_text_ttf(fb, label_x, label_y, label, px_size, fg);
        } else if (item == QM_ITEM_JOYSTICK) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_JOYSTICK, fg);
            qm_draw_text_ttf(fb, bar_x, label_y, "Joystick", px_size, fg);
        } else {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_SETTINGS, fg);
            qm_draw_text_ttf(fb, bar_x, label_y, "Settings", px_size, fg);
        }
        y += QM_ICON_SIZE + slot_gap;
    }

    /* Hint line: badge + label, badge + label, badge-pair + label,
     * centered as one row. Compute the total width first so the whole
     * line can be centered, matching what qm_draw_text(hint, ...) used
     * to do via qm_text_width(). */
    const char *lbl_select = "SELECT";
    const char *lbl_back = "BACK";
    const char *lbl_adjust = "ADJUST";
    int hint_gap = 3 * 1;      /* small gap between a badge and its label, scale 1 */
    int group_gap = 14;        /* gap between hint groups */
    int total_w =
        QM_ICON_SIZE + hint_gap + qm_ttf_text_width(lbl_select, px_size) + group_gap +
        QM_ICON_SIZE + hint_gap + qm_ttf_text_width(lbl_back, px_size) + group_gap +
        QM_ICON_SIZE + QM_ICON_SIZE + hint_gap + qm_ttf_text_width(lbl_adjust, px_size);

    int hx = ((int)fb->width - total_w) / 2;
    /* hy already computed above, before the row loop. */
    int text_y = hy + (QM_ICON_SIZE - px_size) / 2;

    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_A, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE + hint_gap;
    qm_draw_text_ttf(fb, hx, text_y, lbl_select, px_size, QM_COLOR_DIM);
    hx += qm_ttf_text_width(lbl_select, px_size) + group_gap;

    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_B, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE + hint_gap;
    qm_draw_text_ttf(fb, hx, text_y, lbl_back, px_size, QM_COLOR_DIM);
    hx += qm_ttf_text_width(lbl_back, px_size) + group_gap;

    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_LEFT, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE;
    qm_draw_icon_rgba(fb, hx, hy, QM_ICON_BADGE_RIGHT, QM_COLOR_BAR_FG);
    hx += QM_ICON_SIZE + hint_gap;
    qm_draw_text_ttf(fb, hx, text_y, lbl_adjust, px_size, QM_COLOR_DIM);
    } else if (st->screen == QM_SCREEN_JOYSTICK) {
        /* QM_SCREEN_JOYSTICK: plain text list, not icon-based --
         * 7 distinct short-English-label actions don't warrant 7 new
         * hand-drawn icons (see design doc). */
        static const char *labels[QM_JOY_COUNT] = {
            "Calibrate", "J1 X-axis: invert", "J1 Y-axis: invert",
            "J2 X-axis: invert", "J2 Y-axis: invert",
            "J1: enabled", "J2: enabled",
        };
        /* Row index -> bit index in st->joy_status, per the firmware's
         * status-byte order (design doc's Components section). -1 means
         * "no indicator" (the Calibrate row is an action, not a toggle). */
        static const int status_bit[QM_JOY_COUNT] = { -1, 2, 3, 4, 5, 0, 1 };

        qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

        int row_h2 = QM_GLYPH_H * scale + 2 * scale;
        int y2 = margin;
        for (int item = 0; item < QM_JOY_COUNT; item++) {
            if (item == st->joy_selected)
                qm_fill_rect(fb, margin / 2, y2 - scale,
                             (int)fb->width - margin, row_h2, QM_COLOR_SEL_BG);
            uint32_t fg2 = (item == st->joy_selected) ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text_ttf(fb, margin, y2, labels[item], px_size, fg2);
            if (status_bit[item] >= 0) {
                qm_icon_kind mark_icon = (st->joy_status[status_bit[item]] == '1')
                                        ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF;
                int mark_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
                int mark_y = y2 + (px_size - QM_TOGGLE_ICON_H) / 2;
                qm_draw_icon_rgba(fb, mark_x, mark_y, mark_icon, fg2);
            }
            y2 += row_h2;
        }

        /* Selection-dependent hint line, bottom-anchored like the main
         * screen's badge row -- explains what pressing A on the currently
         * selected row actually does (rows alone, e.g. "J1 X-axis:
         * invert", don't make that obvious). row_h2-tall rows for
         * QM_JOY_COUNT items leave ample room above this at both panel
         * scales (checked: 320x240 and 640x480). */
        const char *hint = qm_joy_hint(st->joy_selected);
        int hint_y = (int)fb->height - margin - QM_GLYPH_H * scale;
        qm_draw_text_ttf(fb, margin, hint_y, hint, px_size, QM_COLOR_DIM);
    } else {
        /* QM_SCREEN_DAEMON_SETTINGS: same text-list pattern as the
         * Joystick submenu. Row labels show the current value inline
         * (not a separate column) since each row IS one value. */
        qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);
        int row_h3 = QM_GLYPH_H * scale + 2 * scale;

        if (!st->ds_loaded) {
            /* Never successfully loaded this session (daemon unreachable,
             * socket refused, request timed out behind another in-flight
             * request, etc). Do NOT show the zero-initialized fields --
             * they look like real data, and writing them live would
             * busy-loop the fan-control thread and can trigger a
             * spurious shutdown (0 poll interval / 0 debounce) or latch
             * the fan permanently on (0/0 thresholds). Show a plain
             * unreachable message instead; A is a no-op below while
             * st->ds_loaded stays 0. */
            qm_draw_text_ttf(fb, margin, margin, "Daemon unreachable", px_size, QM_COLOR_FG);
            qm_draw_text_ttf(fb, margin, margin + row_h3, "Press B to go back", px_size, QM_COLOR_DIM);
            return;
        }

        /* Each settings row is a "label: value" pair -- the label stays
         * left-aligned, the value right-aligned, matching the Volume/
         * Brightness rows on the main screen (the layout the rest of the
         * overlay already uses; this screen was the one inconsistent
         * holdout, drawing everything as one left-aligned string). */
        char labels[QM_DS_COUNT][32];
        char values[QM_DS_COUNT][16];
        snprintf(labels[QM_DS_FAN_ENABLED], sizeof(labels[0]), "Fan enabled:");
        snprintf(labels[QM_DS_FAN_ON_TEMP], sizeof(labels[0]), "Fan ON temp:");
        snprintf(values[QM_DS_FAN_ON_TEMP], sizeof(values[0]),
                 "%.1fC", st->ds_fan_on_temp);
        snprintf(labels[QM_DS_FAN_OFF_TEMP], sizeof(labels[0]), "Fan OFF temp:");
        snprintf(values[QM_DS_FAN_OFF_TEMP], sizeof(values[0]),
                 "%.1fC", st->ds_fan_off_temp);
        snprintf(labels[QM_DS_POLL_INTERVAL], sizeof(labels[0]), "Fan poll interval:");
        snprintf(values[QM_DS_POLL_INTERVAL], sizeof(values[0]),
                 "%ds", st->ds_fan_poll_interval_s);
        snprintf(labels[QM_DS_DEBOUNCE_MS], sizeof(labels[0]), "Switch debounce:");
        snprintf(values[QM_DS_DEBOUNCE_MS], sizeof(values[0]),
                 "%dms", st->ds_switch_debounce_ms);
        /* Status row: a transient save/reload/refresh error message wins
         * over the normal CPU+fan display when one is pending; otherwise
         * show CPU temp (if it was ever read successfully this screen
         * visit) alongside the live fan state -- this is the more useful
         * half of the row per the design doc (lets the user judge
         * whether their new threshold makes sense). */
        int ds_status_icon = -1;  /* -1 = no icon (a message is shown instead) */
        if (st->ds_message[0] != '\0') {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]), "%s", st->ds_message);
        } else if (st->ds_cpu_temp_ok) {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]),
                     "CPU: %.1fC, Fan:", st->ds_cpu_temp);
            ds_status_icon = st->ds_fan_on ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF;
        } else {
            snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]), "Fan is currently:");
            ds_status_icon = st->ds_fan_on ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF;
        }

        int y3 = margin;
        for (int item = 0; item < QM_DS_COUNT; item++) {
            if (item == QM_DS_STATUS_ROW) {
                /* Visual gap before the read-only status row. */
                y3 += row_h3 / 2;
            }
            if (item != QM_DS_STATUS_ROW && item == st->ds_selected)
                qm_fill_rect(fb, margin / 2, y3 - scale,
                             (int)fb->width - margin, row_h3, QM_COLOR_SEL_BG);
            uint32_t fg3 = (item != QM_DS_STATUS_ROW && item == st->ds_selected)
                           ? QM_COLOR_FG : QM_COLOR_DIM;
            qm_draw_text_ttf(fb, margin, y3, labels[item], px_size, fg3);
            if (item == QM_DS_FAN_ENABLED) {
                int icon_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
                int icon_y = y3 + (px_size - QM_TOGGLE_ICON_H) / 2;
                qm_draw_icon_rgba(fb, icon_x, icon_y,
                                   st->ds_fan_enabled ? QM_ICON_TOGGLE_ON : QM_ICON_TOGGLE_OFF,
                                   fg3);
            } else if (item != QM_DS_STATUS_ROW) {
                int value_x = (int)fb->width - margin
                            - qm_ttf_text_width(values[item], px_size);
                qm_draw_text_ttf(fb, value_x, y3, values[item], px_size, fg3);
            } else if (ds_status_icon >= 0) {
                int icon_x = (int)fb->width - margin - QM_TOGGLE_ICON_W;
                int icon_y = y3 + (px_size - QM_TOGGLE_ICON_H) / 2;
                qm_draw_icon_rgba(fb, icon_x, icon_y, (qm_icon_kind)ds_status_icon, fg3);
            }
            y3 += row_h3;
        }
    }
}

#ifndef QM_NO_MAIN

#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* How long qm_wl_pump() waits per iteration. Short enough that a SIGTERM
 * is acted on well inside the daemon's 5s watchdog. */
#define QM_PUMP_TIMEOUT_MS 100

static volatile sig_atomic_t qm_quit = 0;

static void qm_on_signal(int signum)
{
    (void)signum;
    qm_quit = 1;
}

static int qm_clamp_pct(int v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

/* Enters the Daemon Settings screen: resets the row cursor, clears any
 * stale message, and refreshes the 4 tunables + live fan state + CPU
 * temp. On a failed GET_CONFIG, st->ds_loaded is left as-is (0 if this
 * is the first attempt this session -- qm_render() shows "Daemon
 * unreachable" for that case; the A-handler below also refuses to write
 * while ds_loaded is 0) so a transient failure never overwrites
 * previously-good values with the zero-initialized defaults. */
static void qm_ds_enter(qm_state *st)
{
    st->screen = QM_SCREEN_DAEMON_SETTINGS;
    st->ds_selected = 0;
    st->ds_message[0] = '\0';

    double fot, foft;
    int poll, debounce, fan_on, fan_enabled;
    if (qm_daemon_config_get(&fot, &foft, &poll, &debounce, &fan_on, &fan_enabled) == 0) {
        st->ds_fan_on_temp = fot;
        st->ds_fan_off_temp = foft;
        st->ds_fan_poll_interval_s = poll;
        st->ds_switch_debounce_ms = debounce;
        st->ds_fan_on = fan_on;
        st->ds_fan_enabled = fan_enabled;
        st->ds_loaded = 1;
    } else if (st->ds_loaded) {
        /* Had good values from an earlier visit this session -- keep
         * showing them, but flag that this particular refresh failed. */
        snprintf(st->ds_message, sizeof(st->ds_message), "Refresh failed");
    }

    double cpu_temp;
    st->ds_cpu_temp_ok = (qm_cpu_temp_get(&cpu_temp) == 0);
    if (st->ds_cpu_temp_ok)
        st->ds_cpu_temp = cpu_temp;
}

/* Runs the ~10s calibration round trip: repaints a countdown between
 * short (~200ms) socket polls. Input is never read while this runs, so
 * it queues in the kernel's evdev buffer rather than being acted on
 * (matches the original cs-configure.py tool's fully-blocking behavior
 * -- there is nothing meaningful to cancel back to mid-calibration,
 * since the Arduino is already committed to its own EEPROM-writing
 * routine regardless of what the Linux side does); the queued input is
 * drained (discarded) right before returning so it can't replay into
 * the main loop and re-trigger anything. Also bails out promptly on
 * qm_quit (SIGTERM/SIGINT), since the daemon's own watchdog is blocked
 * on the serial lock for this whole window and won't tolerate a slow
 * exit once it wakes back up. */
static void qm_run_calibration(qm_wl *wl, int input_fd, qm_fb *fb)
{
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int conn_fd = -1;
    int result = 0;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec)
                        + (now.tv_nsec - start.tv_nsec) / 1e9;

        int remaining = 10 - (int)elapsed;
        if (remaining < 0) remaining = 0;

        qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);
        int scale = (fb->width >= 640) ? 4 : 2;
        int px_size = (scale >= 4) ? 14 : 10;
        qm_draw_text_ttf(fb, 8 * scale, fb->height / 2 - 20 * scale,
                     "Rotate all joysticks in a", px_size, QM_COLOR_FG);
        qm_draw_text_ttf(fb, 8 * scale, fb->height / 2 - 10 * scale,
                     "circular motion now.", px_size, QM_COLOR_FG);
        char remaining_msg[32];
        snprintf(remaining_msg, sizeof(remaining_msg),
                 "%d seconds remaining", remaining);
        qm_draw_text_ttf(fb, 8 * scale, fb->height / 2 + 10 * scale,
                     remaining_msg, px_size, QM_COLOR_DIM);
        qm_wl_present(wl);

        if (elapsed > 13.0) {
            result = 0;
            if (conn_fd >= 0) close(conn_fd);
            break;
        }

        int poll_result = qm_joystick_calibrate_poll(&conn_fd);
        if (poll_result >= 0) {
            result = poll_result;
            if (conn_fd >= 0) close(conn_fd);
            break;
        }

        if (qm_quit) {
            if (conn_fd >= 0) close(conn_fd);
            break;
        }

        /* Keep the compositor connection serviced; any input that
         * arrives during calibration is left queued on input_fd (not
         * read here) and drained below once the countdown/result
         * screen is done -- nothing to act on mid-calibration. */
        qm_wl_pump(wl, input_fd, 200);
    }

    qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);
    int scale = (fb->width >= 640) ? 4 : 2;
    int px_size = (scale >= 4) ? 14 : 10;
    qm_draw_text_ttf(fb, 8 * scale, fb->height / 2,
                 result ? "Calibration complete" : "Calibration failed",
                 px_size, QM_COLOR_FG);
    qm_wl_present(wl);
    usleep(1500000);

    /* Drain any input that queued up during the countdown/result
     * screen -- it was never read (qm_wl_pump() only poll()s
     * input_fd, it doesn't consume it), so without this an impatient
     * A-press during calibration would replay into the main loop the
     * instant this function returns and could silently re-trigger
     * calibration if QM_JOY_CALIBRATE is still selected. */
    while (qm_input_poll(input_fd, 0) != QM_EV_NONE)
        ;
}

int main(void)
{
    struct sigaction sa;
    sa.sa_handler = qm_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* no SA_RESTART: poll() must return EINTR */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    qm_ttf_init();

    int input_fd = qm_input_open();
    if (input_fd < 0) {
        fprintf(stderr, "circuitsword-quickmenu: no input device, aborting\n");
        return 2;
    }

    qm_wl *wl = qm_wl_open();
    if (wl == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: no overlay surface, aborting\n");
        qm_input_close(input_fd);
        return 3;
    }

    qm_state st;
    st.selected = QM_ITEM_WIFI;
    st.wifi_on = qm_wifi_get();
    if (st.wifi_on < 0) st.wifi_on = 0;
    st.volume = qm_volume_get();
    if (st.volume < 0) st.volume = 0;
    st.brightness = qm_brightness_get();
    if (st.brightness < 0) st.brightness = 0;
    st.screen = QM_SCREEN_MAIN;
    st.joy_selected = 0;
    memcpy(st.joy_status, "000000", 6);
    st.ds_selected = 0;
    st.ds_fan_enabled = 0;
    st.ds_fan_on_temp = 0.0;
    st.ds_fan_off_temp = 0.0;
    st.ds_fan_poll_interval_s = 0;
    st.ds_switch_debounce_ms = 0;
    st.ds_fan_on = 0;
    st.ds_loaded = 0;
    st.ds_message[0] = '\0';
    st.ds_cpu_temp = 0.0;
    st.ds_cpu_temp_ok = 0;

    qm_fb *fb = qm_wl_fb(wl);
    qm_render(fb, &st);
    if (qm_wl_present(wl) != 0) {
        qm_wl_close(wl);
        qm_input_close(input_fd);
        return 4;
    }

    while (!qm_quit) {
        int pr = qm_wl_pump(wl, input_fd, QM_PUMP_TIMEOUT_MS);
        if (pr < 0)
            break;              /* compositor closed us or connection lost */
        if (pr == 0)
            continue;           /* timeout, or Wayland-only activity */

        /* Non-blocking drain: qm_wl_pump() already told us the evdev fd
         * is readable, so this never waits. */
        enum qm_event ev = qm_input_poll(input_fd, 0);
        if (ev == QM_EV_NONE)
            continue;

        int dirty = 1;
        if (st.screen == QM_SCREEN_MAIN) {
            switch (ev) {
            case QM_EV_B:
                qm_quit = 1;
                dirty = 0;
                break;
            case QM_EV_UP:
                st.selected = (st.selected + QM_ITEM_COUNT - 1) % QM_ITEM_COUNT;
                break;
            case QM_EV_DOWN:
                st.selected = (st.selected + 1) % QM_ITEM_COUNT;
                break;
            case QM_EV_A:
                if (st.selected == QM_ITEM_WIFI) {
                    int want = st.wifi_on ? 0 : 1;
                    if (qm_wifi_set(want) == 0)
                        st.wifi_on = want;
                } else if (st.selected == QM_ITEM_JOYSTICK) {
                    st.screen = QM_SCREEN_JOYSTICK;
                    st.joy_selected = 0;
                    char status[6];
                    if (qm_joystick_status(status) == 0)
                        memcpy(st.joy_status, status, 6);
                } else if (st.selected == QM_ITEM_DAEMON_SETTINGS) {
                    qm_ds_enter(&st);
                } else {
                    dirty = 0;
                }
                break;
            case QM_EV_LEFT:
            case QM_EV_RIGHT: {
                if (st.selected == QM_ITEM_JOYSTICK) {
                    if (ev == QM_EV_RIGHT) {
                        st.screen = QM_SCREEN_JOYSTICK;
                        st.joy_selected = 0;
                        char status[6];
                        if (qm_joystick_status(status) == 0)
                            memcpy(st.joy_status, status, 6);
                    } else {
                        dirty = 0;
                    }
                    break;
                }
                if (st.selected == QM_ITEM_DAEMON_SETTINGS) {
                    if (ev == QM_EV_RIGHT) {
                        qm_ds_enter(&st);
                    } else {
                        dirty = 0;
                    }
                    break;
                }
                int delta = (ev == QM_EV_RIGHT) ? QM_STEP : -QM_STEP;
                if (st.selected == QM_ITEM_WIFI) {
                    int want = st.wifi_on ? 0 : 1;
                    if (qm_wifi_set(want) == 0)
                        st.wifi_on = want;
                } else if (st.selected == QM_ITEM_VOLUME) {
                    int want = qm_clamp_pct(st.volume + delta);
                    if (qm_volume_set(want) == 0)
                        st.volume = want;
                } else {
                    int want = qm_clamp_pct(st.brightness + delta);
                    if (qm_brightness_set(want) == 0)
                        st.brightness = want;
                }
                break;
            }
            default:
                dirty = 0;
                break;
            }
        } else if (st.screen == QM_SCREEN_JOYSTICK) {
            switch (ev) {
            case QM_EV_B:
                st.screen = QM_SCREEN_MAIN;
                break;
            case QM_EV_UP:
                st.joy_selected = (st.joy_selected + QM_JOY_COUNT - 1) % QM_JOY_COUNT;
                break;
            case QM_EV_DOWN:
                st.joy_selected = (st.joy_selected + 1) % QM_JOY_COUNT;
                break;
            case QM_EV_A:
                if (st.joy_selected == QM_JOY_CALIBRATE) {
                    qm_run_calibration(wl, input_fd, fb);
                    char status[6];
                    if (qm_joystick_status(status) == 0)
                        memcpy(st.joy_status, status, 6);
                } else {
                    static const char *cmds[QM_JOY_COUNT] = {
                        NULL, "INVERT_J1X", "INVERT_J1Y", "INVERT_J2X",
                        "INVERT_J2Y", "TOGGLE_J1", "TOGGLE_J2",
                    };
                    if (qm_joystick_toggle(cmds[st.joy_selected])) {
                        char status[6];
                        if (qm_joystick_status(status) == 0)
                            memcpy(st.joy_status, status, 6);
                    }
                }
                break;
            default:
                dirty = 0;
                break;
            }
        } else {
            /* QM_SCREEN_DAEMON_SETTINGS */
            switch (ev) {
            case QM_EV_B:
                st.screen = QM_SCREEN_MAIN;
                break;
            case QM_EV_UP:
                do {
                    st.ds_selected = (st.ds_selected + QM_DS_COUNT - 1) % QM_DS_COUNT;
                } while (st.ds_selected == QM_DS_STATUS_ROW);
                break;
            case QM_EV_DOWN:
                do {
                    st.ds_selected = (st.ds_selected + 1) % QM_DS_COUNT;
                } while (st.ds_selected == QM_DS_STATUS_ROW);
                break;
            case QM_EV_LEFT:
            case QM_EV_RIGHT: {
                double step_dir = (ev == QM_EV_RIGHT) ? 1.0 : -1.0;
                if (st.ds_selected == QM_DS_FAN_ON_TEMP) {
                    st.ds_fan_on_temp += 0.5 * step_dir;
                    if (st.ds_fan_on_temp < 0) st.ds_fan_on_temp = 0;
                    if (st.ds_fan_on_temp > 90) st.ds_fan_on_temp = 90;
                } else if (st.ds_selected == QM_DS_FAN_OFF_TEMP) {
                    st.ds_fan_off_temp += 0.5 * step_dir;
                    if (st.ds_fan_off_temp < 0) st.ds_fan_off_temp = 0;
                    if (st.ds_fan_off_temp > 90) st.ds_fan_off_temp = 90;
                } else if (st.ds_selected == QM_DS_POLL_INTERVAL) {
                    st.ds_fan_poll_interval_s += (int)step_dir;
                    if (st.ds_fan_poll_interval_s < 1) st.ds_fan_poll_interval_s = 1;
                    if (st.ds_fan_poll_interval_s > 60) st.ds_fan_poll_interval_s = 60;
                } else if (st.ds_selected == QM_DS_DEBOUNCE_MS) {
                    st.ds_switch_debounce_ms += (int)(50 * step_dir);
                    if (st.ds_switch_debounce_ms < 50) st.ds_switch_debounce_ms = 50;
                    if (st.ds_switch_debounce_ms > 5000) st.ds_switch_debounce_ms = 5000;
                } else {
                    dirty = 0;
                }
                break;
            }
            case QM_EV_A:
                if (st.ds_selected == QM_DS_STATUS_ROW) {
                    dirty = 0;
                } else if (!st.ds_loaded) {
                    /* Never successfully loaded this session -- the
                     * on-screen values are not real daemon state (see
                     * qm_ds_enter()/qm_render()), so writing them would
                     * push zero-initialized junk (fan_poll_interval_s=0,
                     * switch_debounce_ms=0, ...) into circuitsword.conf
                     * and reload it live. Refuse outright. */
                    dirty = 0;
                } else {
                    int prev_fan_enabled = st.ds_fan_enabled;
                    if (st.ds_selected == QM_DS_FAN_ENABLED)
                        st.ds_fan_enabled = st.ds_fan_enabled ? 0 : 1;

                    if (qm_daemon_config_write(st.ds_fan_on_temp, st.ds_fan_off_temp,
                                                st.ds_fan_poll_interval_s,
                                                st.ds_switch_debounce_ms,
                                                st.ds_fan_enabled) != 0) {
                        /* Write itself failed -- nothing was persisted, so
                         * the flipped in-memory value would be a lie on
                         * screen. Revert it. (Reload-failed below is left
                         * alone: the file WAS written with the new value
                         * there, so the toggle still matches what's on
                         * disk even if the live daemon hasn't picked it
                         * up yet.) */
                        st.ds_fan_enabled = prev_fan_enabled;
                        snprintf(st.ds_message, sizeof(st.ds_message), "Save failed");
                    } else if (!qm_daemon_config_reload()) {
                        /* File was written but the daemon may still be
                         * running the old values -- don't claim success. */
                        snprintf(st.ds_message, sizeof(st.ds_message), "Reload failed");
                    } else {
                        /* Written and reloaded -- pull back what the daemon
                         * actually has (it may have clamped an out-of-range
                         * value) rather than assuming our write landed
                         * verbatim, and refresh the CPU temp alongside it. */
                        double fot, foft;
                        int poll, debounce, fan_on, fan_enabled;
                        if (qm_daemon_config_get(&fot, &foft, &poll, &debounce,
                                                  &fan_on, &fan_enabled) == 0) {
                            st.ds_fan_on_temp = fot;
                            st.ds_fan_off_temp = foft;
                            st.ds_fan_poll_interval_s = poll;
                            st.ds_switch_debounce_ms = debounce;
                            st.ds_fan_on = fan_on;
                            st.ds_fan_enabled = fan_enabled;
                            st.ds_message[0] = '\0';
                        } else {
                            snprintf(st.ds_message, sizeof(st.ds_message), "Refresh failed");
                        }
                        double cpu_temp;
                        st.ds_cpu_temp_ok = (qm_cpu_temp_get(&cpu_temp) == 0);
                        if (st.ds_cpu_temp_ok)
                            st.ds_cpu_temp = cpu_temp;
                    }
                }
                break;
            default:
                dirty = 0;
                break;
            }
        }

        if (dirty && !qm_quit) {
            qm_render(fb, &st);
            qm_wl_present(wl);
        }
    }

    qm_wl_close(wl);
    qm_input_close(input_fd);
    return 0;
}

#endif /* QM_NO_MAIN */
