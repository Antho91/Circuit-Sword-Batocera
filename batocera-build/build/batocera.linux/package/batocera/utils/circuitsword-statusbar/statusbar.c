/* circuitsword-statusbar: permanent, passive top bar for the Circuit-Sword.
 *
 * Launched by rpi-circuitsword.py's statusbar_thread whenever a game is
 * running, killed when it exits (see statusbar_tick() in that file).
 * Draws battery/WiFi/volume on a top-anchored Wayland overlay-layer
 * surface (brightness is deliberately not drawn -- see sb_render.c).
 * Never grabs input, never pauses anything -- the game underneath stays
 * fully playable the entire time this runs.
 *
 * Exit codes: 0 = normal (SIGTERM/SIGINT handled, or the compositor
 * closed our surface). 3 = could not open the Wayland surface at all
 * (nothing was ever drawn, no visible glitch). 4 = first present failed.
 *
 * See docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md. */
#include "statusbar.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* Poll/redraw-on-change cadence. None of battery/WiFi/volume change fast
 * enough to need anything tighter -- unlike quickmenu's input-driven
 * 100ms pump, there is no interactivity to be responsive to. */
#define SB_POLL_INTERVAL_MS 2000

static volatile sig_atomic_t sb_quit = 0;

static void sb_on_signal(int signum)
{
    (void)signum;
    sb_quit = 1;
}

int main(void)
{
    struct sigaction sa;
    sa.sa_handler = sb_on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sb_wl *wl = sb_wl_open();
    if (wl == NULL) {
        fprintf(stderr, "circuitsword-statusbar: no overlay surface, aborting\n");
        return 3;
    }

    int wifi_on = qm_wifi_get();
    if (wifi_on < 0) wifi_on = 0;
    int volume = qm_volume_get();
    if (volume < 0) volume = 0;
    /* sb_render() never draws brightness (see its own header comment) --
     * not polled here, just passed through as an ignored 0. */
    const int brightness = 0;
    int battery_percent = 50, battery_charging = 0;
    qm_battery_get(&battery_percent, &battery_charging);

    qm_fb *fb = sb_wl_fb(wl);
    sb_render(fb, wifi_on, volume, brightness, battery_percent, battery_charging);
    if (sb_wl_present(wl) != 0) {
        sb_wl_close(wl);
        return 4;
    }

    while (!sb_quit) {
        int pr = sb_wl_pump(wl, SB_POLL_INTERVAL_MS);
        if (pr < 0)
            break;   /* compositor closed us or connection lost */

        /* Hold last-known-good on any read failure, same convention as
         * rpi-circuitsword.py's own read_battery_percent(). */
        int new_wifi = qm_wifi_get();
        if (new_wifi < 0) new_wifi = wifi_on;
        int new_volume = qm_volume_get();
        if (new_volume < 0) new_volume = volume;
        int new_battery_percent = battery_percent, new_battery_charging = battery_charging;
        qm_battery_get(&new_battery_percent, &new_battery_charging);

        int dirty = (new_wifi != wifi_on) || (new_volume != volume) ||
                    (new_battery_percent != battery_percent) ||
                    (new_battery_charging != battery_charging);

        wifi_on = new_wifi;
        volume = new_volume;
        battery_percent = new_battery_percent;
        battery_charging = new_battery_charging;

        if (dirty && !sb_quit) {
            sb_render(fb, wifi_on, volume, brightness, battery_percent, battery_charging);
            sb_wl_present(wl);
        }
    }

    sb_wl_close(wl);
    return 0;
}
