/* Host-side unit tests for circuitsword-quickmenu's qm_settings.c pure
 * parsing logic. The file-I/O wrappers themselves read real system paths
 * and are not host-testable; qm_battery_parse() is the pure core split
 * out for exactly that reason -- see qm_battery_get(). Run via
 * tests/run-c-tests.sh. */
#include <stdio.h>
#include "quickmenu.h"

static int failures = 0;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("  ok   %s\n", what);
    } else {
        printf("  FAIL %s\n", what);
        failures++;
    }
}

int main(void)
{
    printf("qm_battery_parse\n");
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(87, "Discharging", &percent, &charging);
        check(rc == 0, "returns 0 on valid input");
        check(percent == 87, "percent passed through");
        check(charging == 0, "Discharging -> charging=0");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(42, "Charging", &percent, &charging);
        check(rc == 0, "returns 0 on valid input");
        check(percent == 42, "percent passed through");
        check(charging == 1, "Charging -> charging=1");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(150, "Discharging", &percent, &charging);
        check(rc == 0, "returns 0 even for an out-of-range raw value");
        check(percent == 100, "clamped to 100");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(-5, "Discharging", &percent, &charging);
        check(rc == 0, "returns 0 even for a negative-but-not-sentinel raw value");
        check(percent == 0, "clamped to 0");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(-1, "Discharging", &percent, &charging);
        check(rc == -1, "raw_capacity == -1 (unreadable capacity file) -> error");
    }
    {
        int percent = -1, charging = -1;
        int rc = qm_battery_parse(50, NULL, &percent, &charging);
        check(rc == -1, "NULL status (unreadable status file) -> error");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
