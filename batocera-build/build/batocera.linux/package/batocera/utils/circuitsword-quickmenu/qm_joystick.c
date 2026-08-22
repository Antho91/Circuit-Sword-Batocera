/* Unix-domain-socket client to rpi-circuitsword.py's joystick_ipc_thread.
 * See quickmenu.h for the function contracts. circuitsword-quickmenu
 * never opens /dev/ttyACM0 directly -- every joystick action is a short
 * socket round trip to the daemon, which remains the sole serial owner. */
#include "quickmenu.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static int qm_joystick_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, QM_JOYSTICK_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int qm_joystick_set_timeout(int fd, long ms)
{
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* Reads until '\n' or the socket times out/errors/closes. Always
 * NUL-terminates within buf (size >= 1). Returns the number of bytes
 * read into buf (excluding the NUL), or -1 on error/timeout with
 * nothing usable read. */
static int qm_joystick_read_line(int fd, char *buf, size_t bufsize)
{
    size_t n = 0;
    errno = 0;
    while (n + 1 < bufsize) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) {
            if (n == 0)
                return -1;
            break;
        }
        if (c == '\n')
            break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return (int)n;
}

int qm_joystick_calibrate_poll(int *conn_fd)
{
    if (*conn_fd < 0) {
        int fd = qm_joystick_connect();
        if (fd < 0)
            return 0;
        if (qm_joystick_set_timeout(fd, 200) != 0) {
            close(fd);
            return 0;
        }
        static const char cmd[] = "CALIBRATE\n";
        if (send(fd, cmd, sizeof(cmd) - 1, 0) != (ssize_t)(sizeof(cmd) - 1)) {
            close(fd);
            return 0;
        }
        *conn_fd = fd;
    }

    char line[64];
    int n = qm_joystick_read_line(*conn_fd, line, sizeof(line));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;   /* still waiting */
        return 0;        /* real error */
    }
    return strcmp(line, "OK") == 0 ? 1 : 0;
}

int qm_joystick_toggle(const char *cmd)
{
    int fd = qm_joystick_connect();
    if (fd < 0)
        return 0;
    if (qm_joystick_set_timeout(fd, 1000) != 0) {
        close(fd);
        return 0;
    }

    char msg[32];
    int len = snprintf(msg, sizeof(msg), "%s\n", cmd);
    if (len <= 0 || send(fd, msg, (size_t)len, 0) != len) {
        close(fd);
        return 0;
    }

    char line[64];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n < 0)
        return 0;
    return strcmp(line, "OK") == 0 ? 1 : 0;
}

int qm_joystick_status(char out[6])
{
    int fd = qm_joystick_connect();
    if (fd < 0)
        return -1;
    if (qm_joystick_set_timeout(fd, 1000) != 0) {
        close(fd);
        return -1;
    }

    static const char cmd[] = "STATUS\n";
    if (send(fd, cmd, sizeof(cmd) - 1, 0) != (ssize_t)(sizeof(cmd) - 1)) {
        close(fd);
        return -1;
    }

    char line[64];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n != 6)
        return -1;
    memcpy(out, line, 6);
    return 0;
}

int qm_daemon_config_get(double *fan_on_temp, double *fan_off_temp,
                          int *fan_poll_interval_s, int *switch_debounce_ms,
                          int *fan_on, int *fan_enabled)
{
    int fd = qm_joystick_connect();
    if (fd < 0)
        return -1;
    if (qm_joystick_set_timeout(fd, 1000) != 0) {
        close(fd);
        return -1;
    }

    static const char cmd[] = "GET_CONFIG\n";
    if (send(fd, cmd, sizeof(cmd) - 1, 0) != (ssize_t)(sizeof(cmd) - 1)) {
        close(fd);
        return -1;
    }

    char line[192];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n <= 0)
        return -1;

    double parsed_fan_on_temp = 0, parsed_fan_off_temp = 0;
    int parsed_poll = 0, parsed_debounce = 0, parsed_fan_on = 0, parsed_fan_enabled = 0;
    int matched = sscanf(line,
        "fan_on_temp=%lf,fan_off_temp=%lf,fan_poll_interval_s=%d,switch_debounce_ms=%d,fan_on=%d,fan_enabled=%d",
        &parsed_fan_on_temp, &parsed_fan_off_temp, &parsed_poll, &parsed_debounce,
        &parsed_fan_on, &parsed_fan_enabled);
    if (matched != 6)
        return -1;

    *fan_on_temp = parsed_fan_on_temp;
    *fan_off_temp = parsed_fan_off_temp;
    *fan_poll_interval_s = parsed_poll;
    *switch_debounce_ms = parsed_debounce;
    *fan_on = parsed_fan_on;
    *fan_enabled = parsed_fan_enabled;
    return 0;
}

int qm_daemon_config_reload(void)
{
    int fd = qm_joystick_connect();
    if (fd < 0)
        return 0;
    if (qm_joystick_set_timeout(fd, 1000) != 0) {
        close(fd);
        return 0;
    }

    static const char cmd[] = "RELOAD_CONFIG\n";
    if (send(fd, cmd, sizeof(cmd) - 1, 0) != (ssize_t)(sizeof(cmd) - 1)) {
        close(fd);
        return 0;
    }

    char line[64];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n < 0)
        return 0;
    return strcmp(line, "OK") == 0 ? 1 : 0;
}

#define QM_DAEMON_CONFIG_PATH "/userdata/system/configs/circuitsword.conf"

int qm_daemon_config_write(double fan_on_temp, double fan_off_temp,
                            int fan_poll_interval_s, int switch_debounce_ms,
                            int fan_enabled)
{
    FILE *fp = fopen(QM_DAEMON_CONFIG_PATH, "w");
    if (fp == NULL)
        return -1;
    int n = fprintf(fp,
        "# Written by circuitsword-quickmenu's Daemon Settings screen.\n"
        "fan_on_temp=%.1f\n"
        "fan_off_temp=%.1f\n"
        "fan_poll_interval_s=%d\n"
        "switch_debounce_ms=%d\n"
        "fan_enabled=%d\n",
        fan_on_temp, fan_off_temp, fan_poll_interval_s, switch_debounce_ms, fan_enabled);
    if (fclose(fp) != 0 || n <= 0)
        return -1;
    return 0;
}
