/* Each setting is read/written through the *same* primitive the existing
 * Batocera counterpart already uses, so there is one source of truth per
 * setting -- see the table in the Phase 4 v2 plan, Task 9. */
#include "quickmenu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QM_BACKLIGHT_DIR "/sys/class/backlight/circuitsword-backlight"

static int qm_run_capture_int(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (fp == NULL)
        return -1;
    char buf[64];
    memset(buf, 0, sizeof(buf));
    char *got = fgets(buf, sizeof(buf), fp);
    int rc = pclose(fp);
    if (got == NULL || rc != 0)
        return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf || errno != 0)
        return -1;
    return (int)v;
}

static int qm_read_int_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    int v = -1;
    if (fscanf(fp, "%d", &v) != 1)
        v = -1;
    fclose(fp);
    return v;
}

static int qm_write_int_file(const char *path, int value)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    int n = fprintf(fp, "%d\n", value);
    if (fclose(fp) != 0 || n <= 0)
        return -1;
    return 0;
}

static int qm_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int qm_read_str_file(const char *path, char *buf, size_t n)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    char *got = fgets(buf, (int)n, fp);
    fclose(fp);
    if (got == NULL)
        return -1;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return 0;
}

/* ---------------- WiFi ---------------- */

int qm_wifi_get(void)
{
    /* wifi.enabled (batocera-settings-get) reflects the last-recorded
     * *intent* to enable WiFi -- it does not necessarily track the live
     * radio state. Observed on real hardware: WiFi genuinely connected
     * (SSH session active over it) while that flag read back as unset/0,
     * showing "WiFi: OFF" for a working connection. Query connman
     * directly for the wifi technology's actual Powered state instead --
     * that is what the user can see/feel. */
    FILE *fp = popen("/usr/bin/connmanctl technologies 2>/dev/null", "r");
    if (fp == NULL)
        return -1;

    char line[256];
    int in_wifi = 0;
    int result = -1;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '/') {
            in_wifi = (strstr(line, "/technology/wifi") != NULL);
            continue;
        }
        if (in_wifi && strstr(line, "Powered") != NULL) {
            result = (strstr(line, "True") != NULL) ? 1 : 0;
            break;
        }
    }
    pclose(fp);
    return result;
}

int qm_wifi_set(int enabled)
{
    char cmd[128];
    /* Call Batocera's own canonical enable/disable script directly,
     * instead of reimplementing its logic (settings write + connman
     * reload + wait-for-IP) ourselves -- this is the exact same code
     * path its native WiFi settings menu uses, so the two can never
     * disagree about what "enabling WiFi" actually does. Two prior
     * from-scratch attempts in this overlay (a full "S08connman reload"
     * restart, then a direct-connmanctl-call bypass) each showed a
     * different real-hardware desync between this overlay, Batocera's
     * native settings, and the actual radio state -- deferring to the
     * one implementation Batocera itself keeps in sync sidesteps that
     * whole class of bug. Backgrounded because batocera-wifi blocks on
     * wait_for_ip (up to several seconds), and the menu must stay
     * responsive (must not risk tripping the daemon's 5s close
     * watchdog). */
    snprintf(cmd, sizeof(cmd),
             "(/usr/bin/batocera-wifi %s >/dev/null 2>&1 &)",
             enabled ? "enable" : "disable");
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "circuitsword-quickmenu: wifi set failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

/* ---------------- Volume ---------------- */

int qm_volume_get(void)
{
    int v = qm_run_capture_int("/usr/bin/batocera-audio getSystemVolume 2>/dev/null");
    if (v < 0)
        return -1;
    return qm_clamp(v, 0, 100);
}

int qm_volume_set(int percent)
{
    percent = qm_clamp(percent, 0, 100);
    char cmd[160];
    snprintf(cmd, sizeof(cmd),
             "/usr/bin/batocera-audio setSystemVolume %d >/dev/null 2>&1",
             percent);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "circuitsword-quickmenu: volume set failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

/* ---------------- Brightness ---------------- */

int qm_brightness_get(void)
{
    int max = qm_read_int_file(QM_BACKLIGHT_DIR "/max_brightness");
    int cur = qm_read_int_file(QM_BACKLIGHT_DIR "/brightness");
    if (max <= 0 || cur < 0)
        return -1;
    /* Same percent conversion batocera-brightness uses, rounded. */
    return qm_clamp((cur * 100 + max / 2) / max, 0, 100);
}

int qm_brightness_set(int percent)
{
    percent = qm_clamp(percent, 0, 100);
    int max = qm_read_int_file(QM_BACKLIGHT_DIR "/max_brightness");
    if (max <= 0) {
        fprintf(stderr, "circuitsword-quickmenu: no circuitsword-backlight device\n");
        return -1;
    }
    /* batocera-brightness: NEWVAL = percent * max / 100 */
    int raw = qm_clamp(percent * max / 100, 0, max);
    if (qm_write_int_file(QM_BACKLIGHT_DIR "/brightness", raw) != 0) {
        fprintf(stderr, "circuitsword-quickmenu: brightness write failed\n");
        return -1;
    }
    return 0;
}

/* ---------------- CPU temperature ---------------- */

/* Same sysfs path and millidegree->C conversion + sanity range as the
 * daemon's own read_cpu_temp_c() in rpi-circuitsword.py -- read directly
 * here rather than round-tripping through the daemon socket, matching how
 * every other sysfs-backed setting in this file works. */
#define QM_THERMAL_ZONE "/sys/class/thermal/thermal_zone0/temp"

int qm_cpu_temp_get(double *out)
{
    int milli = qm_read_int_file(QM_THERMAL_ZONE);
    if (milli <= 0)
        return -1;
    double c = milli / 1000.0;
    if (c <= 0 || c > 160)
        return -1;
    *out = c;
    return 0;
}

/* ---------------- Battery ---------------- */

#define QM_BATTERY_DIR "/sys/class/power_supply/battery"

int qm_battery_parse(int raw_capacity, const char *status, int *percent, int *charging)
{
    if (raw_capacity == -1 || status == NULL)
        return -1;
    *percent = qm_clamp(raw_capacity, 0, 100);
    *charging = (strcmp(status, "Charging") == 0) ? 1 : 0;
    return 0;
}

int qm_battery_get(int *percent, int *charging)
{
    int cap = qm_read_int_file(QM_BATTERY_DIR "/capacity");
    char status[32];
    if (qm_read_str_file(QM_BATTERY_DIR "/status", status, sizeof(status)) != 0)
        return -1;
    return qm_battery_parse(cap, status, percent, charging);
}
