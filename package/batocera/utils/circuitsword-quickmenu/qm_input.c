/* Raw evdev reader for the on-board Arduino Leonardo joystick.
 *
 * No libevdev dependency -- <linux/input.h> plus read() is enough for
 * six inputs, matching this project's minimal-dependency preference.
 *
 * Joysticks do NOT route through Wayland: even though the menu draws as
 * a Wayland layer-shell surface with keyboard_interactivity=none, input
 * still comes straight off the evdev node, exactly as it would with any
 * other display mechanism.
 *
 * Button/hat codes come from this repo's own es_input.cfg entry for
 * "Arduino LLC Arduino Leonardo":
 *   a     = button code 288 (BTN_TRIGGER)
 *   b     = button code 289 (BTN_THUMB)
 *   d-pad = hat 0  (ABS_HAT0X: -1 left / +1 right,
 *                   ABS_HAT0Y: -1 up   / +1 down)
 * REVERTED (2026-08-11, at explicit user request) back to a=288/b=289,
 * the mapping from before the 2026-08-10 "second pass" correction. See
 * the matching comment in es_input.cfg.
 *
 * The device is grabbed with EVIOCGRAB so the paused RetroArch behind us
 * does not also see menu navigation. The grab is released implicitly on
 * close(). */
#include "quickmenu.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define QM_DEVICE_NAME "Arduino LLC Arduino Leonardo"
#define QM_BTN_A 288  /* BTN_TRIGGER */
#define QM_BTN_B 289  /* BTN_THUMB   */

static int qm_open_if_match(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    char name[256];
    memset(name, 0, sizeof(name));
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        close(fd);
        return -1;
    }
    if (strcmp(name, QM_DEVICE_NAME) != 0) {
        close(fd);
        return -1;
    }
    /* Best effort: if the grab fails we still use the device. */
    if (ioctl(fd, EVIOCGRAB, 1) < 0)
        fprintf(stderr, "circuitsword-quickmenu: EVIOCGRAB failed on %s\n", path);
    return fd;
}

int qm_input_open(void)
{
    DIR *dir = opendir("/dev/input");
    if (dir == NULL) {
        fprintf(stderr, "circuitsword-quickmenu: cannot open /dev/input\n");
        return -1;
    }
    int fd = -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        fd = qm_open_if_match(path);
        if (fd >= 0)
            break;
    }
    closedir(dir);
    if (fd < 0)
        fprintf(stderr, "circuitsword-quickmenu: '%s' not found\n", QM_DEVICE_NAME);
    return fd;
}

void qm_input_close(int fd)
{
    if (fd >= 0) {
        ioctl(fd, EVIOCGRAB, 0);
        close(fd);
    }
}

enum qm_event qm_input_poll(int fd, int timeout_ms)
{
    if (fd < 0)
        return QM_EV_NONE;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr <= 0)
        return QM_EV_NONE;

    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            if (ev.code == QM_BTN_A)
                return QM_EV_A;
            if (ev.code == QM_BTN_B)
                return QM_EV_B;
        } else if (ev.type == EV_ABS) {
            if (ev.code == ABS_HAT0X) {
                if (ev.value < 0) return QM_EV_LEFT;
                if (ev.value > 0) return QM_EV_RIGHT;
            } else if (ev.code == ABS_HAT0Y) {
                if (ev.value < 0) return QM_EV_UP;
                if (ev.value > 0) return QM_EV_DOWN;
            }
            /* value == 0 is the release-to-centre, deliberately ignored:
             * it is what gives us one event per D-pad tap. */
        }
    }
    return QM_EV_NONE;
}
