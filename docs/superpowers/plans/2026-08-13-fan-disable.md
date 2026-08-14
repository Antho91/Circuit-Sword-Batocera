# Fan Disable Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user fully disable the built-in fan via a new toggle row in the Daemon Settings submenu, with a hard-coded 80°C/75°C safety ceiling that overrides the disable if the CPU gets dangerously hot.

**Architecture:** A new `fan_enabled` config key (daemon side) gates which hysteresis `fan_thread()` uses — the normal user-configurable one, or a fixed 80/75°C safety one. The existing `GET_CONFIG`/`RELOAD_CONFIG` socket protocol and whole-file config writer are extended by one field, following the exact pattern the four existing tunables already use. The quickmenu UI gets a new toggle-switch row, reusing the icon primitive added earlier today for WiFi/joystick.

**Tech Stack:** Python (`rpi-circuitsword.py`), C (`circuitsword-quickmenu`).

## Global Constraints

- Config key: `fan_enabled`, integer `0`/`1` (NOT Python `bool` — `load_config()`'s generic parser does `type(cfg[key])(value)`, and `bool("0")` evaluates to `True`, so the default value in `DEFAULT_CONFIG` must be the int `1`, not `True`).
- Safety ceiling: hard-coded `80.0`/`75.0` °C, NOT read from config, in both the decision logic and any override-widening. Only applies when `fan_enabled` is falsy.
- New UI row is positioned FIRST in the Daemon Settings submenu (above "Fan ON temp").
- The two temperature rows stay visible and editable regardless of `fan_enabled`'s state — no conditional hiding, no other UI change tied to the value besides the new row itself.
- Wire format for the new field is appended at the END of the existing `GET_CONFIG` reply string and the config-file writer's field list (after `fan_on=`) — this is a wire-protocol ordering choice, independent of the UI's row ordering, chosen to minimize the diff against the four existing fields.

---

### Task 1: Daemon-side `fan_enabled` config + safety-ceiling hysteresis

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`
- Test: `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py`

**Interfaces:**
- Produces: `fan_decision(fan_on: bool, temp_c: float, cfg: dict) -> bool` — pure function, no I/O. `cfg` is the dict `get_current_config()`/`load_config()` already produce (has `fan_enabled`, `fan_on_temp`, `fan_off_temp` keys). Task 2 does not call this directly (it's daemon-internal), but its behavior is what Task 2's UI ultimately controls via the `fan_enabled` config key.
- Produces: `GET_CONFIG`'s socket reply gains a `fan_enabled=<0|1>` field, appended after `fan_on=<0|1>`. Task 2 parses this exact field.

- [ ] **Step 1: Add `fan_enabled` to the config schema**

Open `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`. Find:

```python
DEFAULT_CONFIG = {
    "fan_on_temp": 58.0,
    "fan_off_temp": 50.0,
    "fan_poll_interval_s": 3,
    "switch_debounce_ms": 800,
}
```

Change to:

```python
DEFAULT_CONFIG = {
    "fan_on_temp": 58.0,
    "fan_off_temp": 50.0,
    "fan_poll_interval_s": 3,
    "switch_debounce_ms": 800,
    "fan_enabled": 1,
}
```

(Must be the int `1`, not `True` — `load_config()`'s parser below does `type(cfg[key])(value)`, and `bool("0")` is `True` in Python, which would make the config file unable to ever express "disabled".)

- [ ] **Step 2: Add a fail-safe default in `load_config()`'s range-clamping block**

Find this block (immediately after the existing `fan_poll_interval_s`/`switch_debounce_ms` clamps, right before `return cfg`):

```python
    if cfg["switch_debounce_ms"] < 50:
        print(f"[rpi-circuitsword] config: switch_debounce_ms={cfg['switch_debounce_ms']} "
              f"out of range, clamping to default {DEFAULT_CONFIG['switch_debounce_ms']}",
              file=sys.stderr)
        cfg["switch_debounce_ms"] = DEFAULT_CONFIG["switch_debounce_ms"]

    return cfg
```

Add a clamp for `fan_enabled` right before `return cfg` — any value other than exactly `0` or `1` (a garbage/hand-edited file, or a value from before this feature existed that doesn't parse to 0/1) must fail safe to *enabled*, not disabled, since disabling active cooling must be an explicit, intentional choice, never an accidental default:

```python
    if cfg["fan_enabled"] not in (0, 1):
        print(f"[rpi-circuitsword] config: fan_enabled={cfg['fan_enabled']} "
              f"out of range, clamping to default {DEFAULT_CONFIG['fan_enabled']}",
              file=sys.stderr)
        cfg["fan_enabled"] = DEFAULT_CONFIG["fan_enabled"]

    return cfg
```

- [ ] **Step 3: Extract the fan on/off decision into a pure, testable function**

Find `fan_thread()`'s current inline hysteresis:

```python
def fan_thread(stop_event: threading.Event):
    import gpiod
    from gpiod.line import Direction, Value
    request = gpiod.request_lines(
        "/dev/gpiochip0",
        consumer="circuitsword-fan",
        config={GPIO_PIN_OVERTEMP: gpiod.LineSettings(
            direction=Direction.OUTPUT,
            output_value=Value.ACTIVE,  # start OFF (active-LOW: ACTIVE/1=off)
        )},
    )

    fan_on = False

    try:
        while not stop_event.is_set():
            cfg = get_current_config()
            temp = read_cpu_temp_c()
            if temp is not None:
                if not fan_on and temp >= cfg["fan_on_temp"]:
                    fan_on = True
                    _set_fan_on(True)
                    request.set_value(GPIO_PIN_OVERTEMP, Value.INACTIVE)  # active-LOW: 0=on
                    print(f"[rpi-circuitsword] fan ON at {temp:.1f}C (threshold {cfg['fan_on_temp']}C)", file=sys.stderr)
                elif fan_on and temp < cfg["fan_off_temp"]:
                    fan_on = False
                    _set_fan_on(False)
                    request.set_value(GPIO_PIN_OVERTEMP, Value.ACTIVE)
                    print(f"[rpi-circuitsword] fan OFF at {temp:.1f}C (threshold {cfg['fan_off_temp']}C)", file=sys.stderr)
            stop_event.wait(cfg["fan_poll_interval_s"])
    finally:
        request.set_value(GPIO_PIN_OVERTEMP, Value.ACTIVE)  # leave fan off on exit
        request.release()
```

Replace it with a new module-level pure function placed immediately above `fan_thread()`, plus a rewritten `fan_thread()` that calls it:

```python
# Hard-coded, NOT read from config -- the whole point of a safety ceiling
# is that it can't be configured away by the same toggle that disables
# the fan. Independent 5C hysteresis margin from the user-configurable
# thresholds, so it can't chatter once triggered.
FAN_SAFETY_ON_C = 80.0
FAN_SAFETY_OFF_C = 75.0


def fan_decision(fan_on: bool, temp_c: float, cfg: dict) -> bool:
    """Pure hysteresis decision, no I/O. Mirrors fan_thread()'s original
    inline logic exactly when cfg['fan_enabled'] is truthy. When falsy,
    the normal fan_on_temp/fan_off_temp thresholds are ignored entirely
    and a fixed FAN_SAFETY_ON_C/FAN_SAFETY_OFF_C hysteresis is used
    instead, so a disabled fan still engages if the CPU gets dangerously
    hot."""
    if cfg["fan_enabled"]:
        if not fan_on and temp_c >= cfg["fan_on_temp"]:
            return True
        if fan_on and temp_c < cfg["fan_off_temp"]:
            return False
        return fan_on
    else:
        if not fan_on and temp_c >= FAN_SAFETY_ON_C:
            return True
        if fan_on and temp_c < FAN_SAFETY_OFF_C:
            return False
        return fan_on


def fan_thread(stop_event: threading.Event):
    import gpiod
    from gpiod.line import Direction, Value
    request = gpiod.request_lines(
        "/dev/gpiochip0",
        consumer="circuitsword-fan",
        config={GPIO_PIN_OVERTEMP: gpiod.LineSettings(
            direction=Direction.OUTPUT,
            output_value=Value.ACTIVE,  # start OFF (active-LOW: ACTIVE/1=off)
        )},
    )

    fan_on = False

    try:
        while not stop_event.is_set():
            cfg = get_current_config()
            temp = read_cpu_temp_c()
            if temp is not None:
                new_fan_on = fan_decision(fan_on, temp, cfg)
                if new_fan_on != fan_on:
                    fan_on = new_fan_on
                    _set_fan_on(fan_on)
                    request.set_value(GPIO_PIN_OVERTEMP,
                                       Value.INACTIVE if fan_on else Value.ACTIVE)  # active-LOW: 0=on
                    label = "ON" if fan_on else "OFF"
                    print(f"[rpi-circuitsword] fan {label} at {temp:.1f}C "
                          f"(fan_enabled={cfg['fan_enabled']})", file=sys.stderr)
            stop_event.wait(cfg["fan_poll_interval_s"])
    finally:
        request.set_value(GPIO_PIN_OVERTEMP, Value.ACTIVE)  # leave fan off on exit
        request.release()
```

- [ ] **Step 4: Add `fan_enabled` to the `GET_CONFIG` socket reply**

Find, inside `joystick_ipc_thread()`:

```python
                elif command == "GET_CONFIG":
                    cfg = get_current_config()
                    fan_on = 1 if get_fan_on() else 0
                    reply = (f"fan_on_temp={cfg['fan_on_temp']},"
                             f"fan_off_temp={cfg['fan_off_temp']},"
                             f"fan_poll_interval_s={cfg['fan_poll_interval_s']},"
                             f"switch_debounce_ms={cfg['switch_debounce_ms']},"
                             f"fan_on={fan_on}")
```

Change to (adds one field, appended at the end):

```python
                elif command == "GET_CONFIG":
                    cfg = get_current_config()
                    fan_on = 1 if get_fan_on() else 0
                    reply = (f"fan_on_temp={cfg['fan_on_temp']},"
                             f"fan_off_temp={cfg['fan_off_temp']},"
                             f"fan_poll_interval_s={cfg['fan_poll_interval_s']},"
                             f"switch_debounce_ms={cfg['switch_debounce_ms']},"
                             f"fan_on={fan_on},"
                             f"fan_enabled={cfg['fan_enabled']}")
```

- [ ] **Step 5: Write the host test**

Read `/Users/bas/Circuit-Sword Batocera/tests/test_quickmenu_logic.py` first — it already stubs the `serial` module and loads `rpi-circuitsword.py` by path via `load_daemon()` (the filename has a hyphen, so a plain `import` can't reach it), then does `daemon = load_daemon()` and calls things as `daemon.SomeFunction`. Find the existing `class TestVolumeBridgeTick(unittest.TestCase):` (it tests another pure decision function, `volume_bridge_tick`, using the exact same `load_daemon()` module handle) and add a new test class immediately after it, following the same style:

```python
class TestFanDecision(unittest.TestCase):
    def setUp(self):
        self.daemon = load_daemon()

    def _cfg(self, fan_enabled=1, fan_on_temp=58.0, fan_off_temp=50.0):
        return {
            "fan_on_temp": fan_on_temp,
            "fan_off_temp": fan_off_temp,
            "fan_poll_interval_s": 3,
            "switch_debounce_ms": 800,
            "fan_enabled": fan_enabled,
        }

    def test_enabled_normal_hysteresis_turns_on(self):
        cfg = self._cfg(fan_enabled=1)
        self.assertTrue(self.daemon.fan_decision(False, 60.0, cfg))

    def test_enabled_normal_hysteresis_turns_off(self):
        cfg = self._cfg(fan_enabled=1)
        self.assertFalse(self.daemon.fan_decision(True, 45.0, cfg))

    def test_enabled_normal_hysteresis_dead_zone_keeps_state(self):
        cfg = self._cfg(fan_enabled=1)
        self.assertTrue(self.daemon.fan_decision(True, 54.0, cfg))
        self.assertFalse(self.daemon.fan_decision(False, 54.0, cfg))

    def test_disabled_stays_off_below_safety_ceiling(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertFalse(self.daemon.fan_decision(False, 79.9, cfg))

    def test_disabled_forces_on_at_safety_ceiling(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertTrue(self.daemon.fan_decision(False, 80.0, cfg))

    def test_disabled_forced_on_turns_back_off_below_safety_floor(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertFalse(self.daemon.fan_decision(True, 74.9, cfg))

    def test_disabled_forced_on_stays_on_in_safety_dead_zone(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertTrue(self.daemon.fan_decision(True, 77.0, cfg))

    def test_disabled_ignores_user_configured_thresholds(self):
        # Even with fan_on_temp set very low, a disabled fan must not
        # turn on below the hard-coded safety ceiling.
        cfg = self._cfg(fan_enabled=0, fan_on_temp=10.0, fan_off_temp=5.0)
        self.assertFalse(self.daemon.fan_decision(False, 50.0, cfg))
```

- [ ] **Step 6: Run the test and verify it passes**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
python3 test_quickmenu_logic.py
```

Expected: all tests pass, including the 8 new `TestFanDecision` cases (36 total, up from 27... actually 35, since 8 new tests are added to the existing 27 — verify the exact final count matches `27 + <number of test methods you wrote>`, don't hard-code an expected number in the test file itself).

- [ ] **Step 7: Sanity-check the whole file still parses**

```bash
python3 -m py_compile /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
```

Expected: no output, exit code 0.

- [ ] **Step 8: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: add fan_enabled config key with a hard-coded 80/75C safety ceiling"
```

- [ ] **Step 9: Rebuild the package and verify in the Docker volume**

Per this project's Hard Rule #7 (a full image build silently reuses already-built packages), force a package-level rebuild:

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" BATCH_MODE=1 bcm2837-pkg PKG=rpigpioswitch-reinstall
```

Verify the change actually landed in the built artifact (do not trust a clean build log alone):

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "grep -c 'fan_enabled\|fan_decision\|FAN_SAFETY_ON_C' /bcm2837/target/usr/bin/rpi-circuitsword"
```

Expected: a number greater than 0.

- [ ] **Step 10: Regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 2: quickmenu-side read/write + new Daemon Settings row

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/qm_joystick.c`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c`
- Modify: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c`

**Interfaces:**
- Consumes: Task 1's `GET_CONFIG` reply format, which now ends `...,fan_on=<0|1>,fan_enabled=<0|1>` (exact field name and position — appended last).

- [ ] **Step 1: Extend `qm_daemon_config_get()`'s signature and parser**

In `quickmenu.h`, find:

```c
/* Connects, sends "GET_CONFIG\n", parses the reply
 * "fan_on_temp=..,fan_off_temp=..,fan_poll_interval_s=..,
 * switch_debounce_ms=..,fan_on=.." into the 5 out-parameters. Returns 0
 * on success, -1 on any failure (connection error, timeout, malformed
 * reply) -- out-parameters are left unmodified on failure, caller
 * should keep showing its last-known values. */
int qm_daemon_config_get(double *fan_on_temp, double *fan_off_temp,
                          int *fan_poll_interval_s, int *switch_debounce_ms,
                          int *fan_on);
```

Replace with:

```c
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
```

Find, immediately after:

```c
/* Writes the 4 tunables to /userdata/system/configs/circuitsword.conf,
 * overwriting the whole file (this file has exactly these 4 known keys
 * today -- no comment/unknown-line preservation attempted, matching the
 * simplicity of the file format itself). Returns 0 on success, -1 on
 * write failure (caller must not send RELOAD_CONFIG if this fails). */
int qm_daemon_config_write(double fan_on_temp, double fan_off_temp,
                            int fan_poll_interval_s, int switch_debounce_ms);
```

Replace with:

```c
/* Writes the 5 tunables to /userdata/system/configs/circuitsword.conf,
 * overwriting the whole file (this file has exactly these 5 known keys
 * today -- no comment/unknown-line preservation attempted, matching the
 * simplicity of the file format itself). Returns 0 on success, -1 on
 * write failure (caller must not send RELOAD_CONFIG if this fails). */
int qm_daemon_config_write(double fan_on_temp, double fan_off_temp,
                            int fan_poll_interval_s, int switch_debounce_ms,
                            int fan_enabled);
```

In `qm_joystick.c`, find:

```c
int qm_daemon_config_get(double *fan_on_temp, double *fan_off_temp,
                          int *fan_poll_interval_s, int *switch_debounce_ms,
                          int *fan_on)
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

    char line[160];
    int n = qm_joystick_read_line(fd, line, sizeof(line));
    close(fd);
    if (n <= 0)
        return -1;

    double parsed_fan_on_temp = 0, parsed_fan_off_temp = 0;
    int parsed_poll = 0, parsed_debounce = 0, parsed_fan_on = 0;
    int matched = sscanf(line,
        "fan_on_temp=%lf,fan_off_temp=%lf,fan_poll_interval_s=%d,switch_debounce_ms=%d,fan_on=%d",
        &parsed_fan_on_temp, &parsed_fan_off_temp, &parsed_poll, &parsed_debounce, &parsed_fan_on);
    if (matched != 5)
        return -1;

    *fan_on_temp = parsed_fan_on_temp;
    *fan_off_temp = parsed_fan_off_temp;
    *fan_poll_interval_s = parsed_poll;
    *switch_debounce_ms = parsed_debounce;
    *fan_on = parsed_fan_on;
    return 0;
}
```

Replace with:

```c
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
```

(The `line` buffer grew from `160` to `192` bytes to comfortably fit the one extra field.)

- [ ] **Step 2: Extend `qm_daemon_config_write()`**

In `qm_joystick.c`, find:

```c
int qm_daemon_config_write(double fan_on_temp, double fan_off_temp,
                            int fan_poll_interval_s, int switch_debounce_ms)
{
    FILE *fp = fopen(QM_DAEMON_CONFIG_PATH, "w");
    if (fp == NULL)
        return -1;
    int n = fprintf(fp,
        "# Written by circuitsword-quickmenu's Daemon Settings screen.\n"
        "fan_on_temp=%.1f\n"
        "fan_off_temp=%.1f\n"
        "fan_poll_interval_s=%d\n"
        "switch_debounce_ms=%d\n",
        fan_on_temp, fan_off_temp, fan_poll_interval_s, switch_debounce_ms);
    if (fclose(fp) != 0 || n <= 0)
        return -1;
    return 0;
}
```

Replace with:

```c
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
```

- [ ] **Step 3: Add the new row constant and `qm_state` field in `quickmenu.h`**

Find:

```c
#define QM_DS_FAN_ON_TEMP    0
#define QM_DS_FAN_OFF_TEMP   1
#define QM_DS_POLL_INTERVAL  2
#define QM_DS_DEBOUNCE_MS    3
#define QM_DS_STATUS_ROW     4
#define QM_DS_COUNT          5
```

Replace with (the new row goes FIRST, per the design; every existing constant shifts by one):

```c
#define QM_DS_FAN_ENABLED    0
#define QM_DS_FAN_ON_TEMP    1
#define QM_DS_FAN_OFF_TEMP   2
#define QM_DS_POLL_INTERVAL  3
#define QM_DS_DEBOUNCE_MS    4
#define QM_DS_STATUS_ROW     5
#define QM_DS_COUNT          6
```

Find, inside the `qm_state` struct:

```c
    int ds_selected;         /* 0 .. QM_DS_COUNT-1, daemon-settings screen only */
    double ds_fan_on_temp;
```

Replace with:

```c
    int ds_selected;         /* 0 .. QM_DS_COUNT-1, daemon-settings screen only */
    int ds_fan_enabled;       /* 0 or 1 -- gates fan_thread()'s hysteresis on the
                                  daemon side; the two temperature rows below stay
                                  visible/editable regardless of this value */
    double ds_fan_on_temp;
```

- [ ] **Step 4: Update `qm_ds_enter()` in `quickmenu.c`**

Find:

```c
static void qm_ds_enter(qm_state *st)
{
    st->screen = QM_SCREEN_DAEMON_SETTINGS;
    st->ds_selected = 0;
    st->ds_message[0] = '\0';

    double fot, foft;
    int poll, debounce, fan_on;
    if (qm_daemon_config_get(&fot, &foft, &poll, &debounce, &fan_on) == 0) {
        st->ds_fan_on_temp = fot;
        st->ds_fan_off_temp = foft;
        st->ds_fan_poll_interval_s = poll;
        st->ds_switch_debounce_ms = debounce;
        st->ds_fan_on = fan_on;
        st->ds_loaded = 1;
    } else if (st->ds_loaded) {
        /* Had good values from an earlier visit this session -- keep
         * showing them, but flag that this particular refresh failed. */
        snprintf(st->ds_message, sizeof(st->ds_message), "Refresh failed");
    }
```

Replace with:

```c
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
```

- [ ] **Step 5: Update the QM_EV_A write/reload/refresh flow in `quickmenu.c`**

Find:

```c
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
                } else if (qm_daemon_config_write(st.ds_fan_on_temp, st.ds_fan_off_temp,
                                                    st.ds_fan_poll_interval_s,
                                                    st.ds_switch_debounce_ms) != 0) {
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
                    int poll, debounce, fan_on;
                    if (qm_daemon_config_get(&fot, &foft, &poll, &debounce, &fan_on) == 0) {
                        st.ds_fan_on_temp = fot;
                        st.ds_fan_off_temp = foft;
                        st.ds_fan_poll_interval_s = poll;
                        st.ds_switch_debounce_ms = debounce;
                        st.ds_fan_on = fan_on;
                        st.ds_message[0] = '\0';
                    } else {
                        snprintf(st.ds_message, sizeof(st.ds_message), "Refresh failed");
                    }
                    double cpu_temp;
                    st.ds_cpu_temp_ok = (qm_cpu_temp_get(&cpu_temp) == 0);
                    if (st.ds_cpu_temp_ok)
                        st.ds_cpu_temp = cpu_temp;
                }
                break;
```

Replace with (adds a toggle-before-write step for the new row, extends the write call with the 5th argument, and extends the post-write refresh to also pull back `fan_enabled`):

```c
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
                    if (st.ds_selected == QM_DS_FAN_ENABLED)
                        st.ds_fan_enabled = st.ds_fan_enabled ? 0 : 1;

                    if (qm_daemon_config_write(st.ds_fan_on_temp, st.ds_fan_off_temp,
                                                st.ds_fan_poll_interval_s,
                                                st.ds_switch_debounce_ms,
                                                st.ds_fan_enabled) != 0) {
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
```

Do NOT add any conditional hiding/graying of the `QM_DS_FAN_ON_TEMP`/`QM_DS_FAN_OFF_TEMP` rows based on `ds_fan_enabled` — per the design, they stay visible and editable unconditionally. This step is the entire scope of the input-handling change.

- [ ] **Step 6: Add the new row to the render loop**

Find, inside `qm_render()`'s `QM_SCREEN_DAEMON_SETTINGS` branch:

```c
        char labels[QM_DS_COUNT][32];
        char values[QM_DS_COUNT][16];
        snprintf(labels[QM_DS_FAN_ON_TEMP], sizeof(labels[0]), "Fan ON temp:");
```

Replace with (adds one `snprintf` for the new row's label; it has no `values[]` entry since it renders as an icon, not text):

```c
        char labels[QM_DS_COUNT][32];
        char values[QM_DS_COUNT][16];
        snprintf(labels[QM_DS_FAN_ENABLED], sizeof(labels[0]), "Fan enabled:");
        snprintf(labels[QM_DS_FAN_ON_TEMP], sizeof(labels[0]), "Fan ON temp:");
```

Find the render loop itself:

```c
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
            if (item != QM_DS_STATUS_ROW) {
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
```

Replace with (adds a third branch for `QM_DS_FAN_ENABLED`, drawn as a toggle icon instead of value text, using the exact same right-alignment math the status row's icon already uses):

```c
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
```

- [ ] **Step 7: Update the host tests' `qm_state` initializers**

Read `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` first to see the exact current `qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS, ... }` initializers (there are several, covering different scenarios: basic 320x240, 640x480, CPU-temp-with-message, daemon-unreachable). For every one of them that already sets `.ds_loaded = 1` (i.e. every one EXCEPT the deliberate `ds_loaded == 0` "daemon unreachable" test case), add `.ds_fan_enabled = 1,` to the initializer list, so the new row's toggle-on icon is actually exercised by at least one test. Match the existing style (these are C99 designated initializers, order doesn't matter, just add the one field).

- [ ] **Step 8: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
bash run-c-tests.sh
```

Expected: 0 failures across the whole suite.

- [ ] **Step 9: Rebuild the package and verify in the Docker volume**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
```

Verify the change actually landed in the built binary:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c 'Fan enabled:'"
```

Expected: `1`.

- [ ] **Step 10: Commit and regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_joystick.c \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "circuitsword-quickmenu: add Fan enabled toggle row to Daemon Settings"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

## Self-Review Notes

**Spec coverage:** config key + persistence (Task 1, Steps 1-2), hard-coded non-configurable 80/75°C safety ceiling (Task 1, Step 3 — `FAN_SAFETY_ON_C`/`FAN_SAFETY_OFF_C` are module constants, never read from `cfg`), UI row placed first with the toggle-icon pattern (Task 2, Steps 3 and 6), temperature rows staying visible/editable with no conditional logic (Task 2, Step 5's explicit note — confirmed no such logic was added anywhere in this plan), data flow through the existing `GET_CONFIG`/write/reload pattern (Task 2, Steps 1-2, 4-5).

**Type consistency:** `fan_enabled` is `int` (0/1) end-to-end — Python `DEFAULT_CONFIG`/`cfg` dict, the socket wire format, and the C `int *fan_enabled`/`int fan_enabled` parameters all agree. `qm_daemon_config_get()`'s and `qm_daemon_config_write()`'s signatures match their declarations in `quickmenu.h` exactly, including the new 6th/5th parameters respectively.

**Placeholder scan:** no TBD/TODO; every code block is the literal find/replace text to apply, not a description of it.
