# Daemon Settings Surface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose the daemon's 4 existing tunables (fan_on_temp, fan_off_temp, fan_poll_interval_s, switch_debounce_ms) through a new "Daemon Settings" submenu in `circuitsword-quickmenu`, with live reload (no daemon restart) and a live status readout.

**Architecture:** The daemon's config moves from "loaded once per thread at startup" to a shared, lock-protected `_current_config` that `fan_thread`/`switch_monitor` read every poll tick. Two new commands (`GET_CONFIG`, `RELOAD_CONFIG`) are added to the EXISTING joystick IPC socket (reused as-is, not renamed). Quickmenu gets a 5th main-menu item and a new editable submenu screen.

**Tech Stack:** Python 3 (`threading.Lock`), C (reuses `qm_joystick.c`'s existing static socket helpers).

## Global Constraints

- Socket path, exact string, unchanged from the joystick sub-project: `/var/run/circuitsword-joystick.sock` (reused, not renamed, even though it now carries non-joystick commands too).
- `circuitsword-quickmenu` must NEVER open `/dev/ttyACM0` directly — unchanged invariant from every prior sub-project.
- `GET_CONFIG` reply format, exact: one line, comma-separated `key=value` pairs in this exact order and these exact keys: `fan_on_temp,fan_off_temp,fan_poll_interval_s,switch_debounce_ms,fan_on` (the last is live 0/1 state, not a config-file key).
- `RELOAD_CONFIG` always replies `OK` (re-reading a missing/malformed file already falls back to defaults silently, matching `load_config()`'s existing tolerance — no new failure mode to report).
- Step sizes for the 4 editable fields: 0.5°C for `fan_on_temp`/`fan_off_temp`, 1s for `fan_poll_interval_s`, 50ms for `switch_debounce_ms`.
- Out of scope: low-battery thresholds (no consumer exists), a native ES C++ settings screen (explicitly declined), any tunable beyond the existing 4.
- Per CLAUDE.md Hard Rule #7: `rpigpioswitch` (Task 1) is plain-copy — verify via `PKG=rpigpioswitch-reinstall` + Docker-volume inspection. `circuitsword-quickmenu` (Tasks 2-4) is compiled — verify via `PKG=circuitsword-quickmenu-rebuild` + Docker-volume inspection.
- Build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (detached HEAD, currently at `e39eb3a947`). Buildroot commands use the absolute `env.sh` path discovered during the charging-status-decouple plan:
  ```bash
  bash -c '
    export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
    source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
    cd "$BATOCERA_SRC"
    make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=<name>
  ' 2>&1 | tail -40
  ```
- After each task, regenerate the patch capture:
  ```bash
  cd /Users/bas/batocera-build-wifi/batocera.linux
  git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
  ```

---

### Task 1: Shared hot-reloadable config + GET_CONFIG/RELOAD_CONFIG

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`

**Interfaces:**
- Produces: `get_current_config() -> dict`, `reload_config() -> dict`, module-level `_current_config`/`_config_lock`, module-level fan-state sharing (`get_fan_on() -> bool` / an internal setter `fan_thread` calls). Nothing outside this file consumes these directly — only the socket protocol (`GET_CONFIG`/`RELOAD_CONFIG` commands) is a cross-file interface, consumed by Task 3.

- [ ] **Step 1: Read the file fresh**

Confirm current line numbers for `load_config()` (~line 92), `fan_thread()`'s `cfg = load_config()` (~line 137), `switch_monitor()`'s `cfg = load_config()` (~line 488), `JOYSTICK_COMMANDS` dict and the dispatch `if/elif` chain inside `joystick_ipc_thread` (~lines 880-925), and `main()`'s own `cfg = load_config()` (~line 971).

- [ ] **Step 2: Add shared config state and reload/getter functions**

Near `load_config()` (right after its definition), add:

```python
_config_lock = threading.Lock()
_current_config = load_config()
_fan_on_lock = threading.Lock()
_fan_on_state = False


def get_current_config() -> dict:
    with _config_lock:
        return dict(_current_config)


def reload_config() -> dict:
    """Re-reads circuitsword.conf, logs each key that actually changed
    value, replaces the shared config. Always succeeds -- load_config()
    already tolerates a missing/malformed file by returning defaults."""
    global _current_config
    new_cfg = load_config()
    with _config_lock:
        old_cfg = _current_config
        for key, new_val in new_cfg.items():
            old_val = old_cfg.get(key)
            if old_val != new_val:
                print(f"[rpi-circuitsword] config reload: {key} {old_val} -> {new_val}",
                      file=sys.stderr)
        _current_config = new_cfg
    return new_cfg


def get_fan_on() -> bool:
    with _fan_on_lock:
        return _fan_on_state


def _set_fan_on(value: bool):
    global _fan_on_state
    with _fan_on_lock:
        _fan_on_state = value
```

- [ ] **Step 3: Switch `fan_thread()` to the shared config + shared fan-state**

Replace the single `cfg = load_config()` line near the top of `fan_thread()` — remove it entirely (no per-thread snapshot anymore). Inside the loop, replace each `cfg["fan_on_temp"]`/`cfg["fan_off_temp"]`/`cfg["fan_poll_interval_s"]` reference with a fresh `cfg = get_current_config()` read at the TOP of each loop iteration (cheap in-memory dict copy, not file I/O), and call `_set_fan_on(fan_on)` right after each place `fan_on` is assigned (both the `fan_on = True` and `fan_on = False` branches), so the shared fan-state is always current for `GET_CONFIG` to read.

Concretely, the loop body's shape becomes:

```python
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

- [ ] **Step 4: Switch `switch_monitor()` to the shared config**

Replace its single `cfg = load_config()` / `debounce_s = cfg["switch_debounce_ms"] / 1000.0` near the top with a fresh `cfg = get_current_config()` read at the top of EACH loop iteration (it polls every 50ms — re-reading a shared in-memory dict every 50ms is cheap; this is NOT a file read, so this is fine unlike a naive "re-parse the file every 50ms" approach would be). Recompute `debounce_s = cfg["switch_debounce_ms"] / 1000.0` inside the loop each iteration too, right after the `cfg = get_current_config()` line.

- [ ] **Step 5: Extend `joystick_ipc_thread`'s command dispatch**

In the existing dispatch chain (currently `if command == "STATUS": ... elif command in JOYSTICK_COMMANDS: ... else: ...`), add two more branches BEFORE the final `else`:

```python
                if command == "STATUS":
                    reply = joystick_status()
                elif command == "GET_CONFIG":
                    cfg = get_current_config()
                    fan_on = 1 if get_fan_on() else 0
                    reply = (f"fan_on_temp={cfg['fan_on_temp']},"
                             f"fan_off_temp={cfg['fan_off_temp']},"
                             f"fan_poll_interval_s={cfg['fan_poll_interval_s']},"
                             f"switch_debounce_ms={cfg['switch_debounce_ms']},"
                             f"fan_on={fan_on}")
                elif command == "RELOAD_CONFIG":
                    reload_config()
                    reply = "OK"
                elif command in JOYSTICK_COMMANDS:
                    ok = JOYSTICK_COMMANDS[command]()
                    reply = "OK" if ok else "ERR failed"
                else:
                    reply = "ERR unknown command"
```

- [ ] **Step 6: Point `main()`'s startup log at the shared config**

`main()`'s existing `cfg = load_config()` / `print(f"[rpi-circuitsword] all threads started, config={cfg}")` line should use `get_current_config()` instead of calling `load_config()` a second, separate time (both would currently produce the same value at startup, but reading the shared object is the correct source of truth going forward, and avoids a second independent file read at the exact moment threads are starting).

- [ ] **Step 7: Host syntax check**

Run: `python3 -m py_compile "package/batocera/utils/rpigpioswitch/rpi-circuitsword.py"`
Expected: no output, exit code 0.

- [ ] **Step 8: Buildroot reinstall**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=rpigpioswitch-reinstall
' 2>&1 | tail -40
```

- [ ] **Step 9: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  grep -c "reload_config\|get_current_config\|GET_CONFIG\|RELOAD_CONFIG" \
  /bcm2837/target/usr/bin/rpi-circuitsword
```

Expected: nonzero.

- [ ] **Step 10: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "$(cat <<'EOF'
rpi-circuitsword: add live-reloadable shared config + GET_CONFIG/RELOAD_CONFIG

fan_thread and switch_monitor now read a shared, lock-protected
_current_config every poll tick instead of caching a snapshot once at
thread start. Two new commands on the existing joystick IPC socket let
circuitsword-quickmenu read current tunables/live fan state and trigger
an immediate reload after writing a new circuitsword.conf -- no daemon
restart needed to see a config change take effect.
EOF
)"
```

- [ ] **Step 11: Regenerate patch capture** (command in Global Constraints)

---

### Task 2: New settings icon asset

**Files:**
- Create: `package/batocera/utils/circuitsword-quickmenu/icons/src/settings.svg`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `tools/convert-icons.py`
- Modify (generated): `package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c`

**Interfaces:**
- Produces: `QM_ICON_SETTINGS` (new `qm_icon_kind` enum member) — Task 4 draws it via `qm_draw_icon_rgba(fb, x, y, QM_ICON_SETTINGS, color)`.

- [ ] **Step 1: Create the icon SVG**

Create `package/batocera/utils/circuitsword-quickmenu/icons/src/settings.svg` (simple gear shape, matching `volume.svg`'s plain hand-authored style — an octagon-ish ring with a center hole reads as a gear at 24x24 without needing individual teeth):

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <path d="M8 2.5 L9.5 2.5 L10 4.2 L11.5 5 L13.2 4.3 L14.2 5.8 L13 7 L13 9 L14.2 10.2 L13.2 11.7 L11.5 11 L10 11.8 L9.5 13.5 L6.5 13.5 L6 11.8 L4.5 11 L2.8 11.7 L1.8 10.2 L3 9 L3 7 L1.8 5.8 L2.8 4.3 L4.5 5 L6 4.2 Z" fill="#ffffff"/>
  <circle cx="8" cy="8" r="2.3" fill="#000000"/>
</svg>
```

- [ ] **Step 2: Add the enum member to `quickmenu.h`**

Read the file fresh. Insert `QM_ICON_SETTINGS` right after `QM_ICON_JOYSTICK` and before `QM_ICON_TITLE` (current order after the Joystick sub-project: `..., QM_ICON_BRIGHTNESS, QM_ICON_JOYSTICK, QM_ICON_TITLE, ...`):

```c
typedef enum {
    QM_ICON_BATTERY_EMPTY,
    QM_ICON_BATTERY_25,
    QM_ICON_BATTERY_50,
    QM_ICON_BATTERY_75,
    QM_ICON_BATTERY_FULL,
    QM_ICON_BATTERY_CHARGING,
    QM_ICON_WIFI,
    QM_ICON_VOLUME,
    QM_ICON_BRIGHTNESS,
    QM_ICON_JOYSTICK,
    QM_ICON_SETTINGS,
    QM_ICON_TITLE,
    QM_ICON_BADGE_A,
    QM_ICON_BADGE_B,
    QM_ICON_BADGE_LEFT,
    QM_ICON_BADGE_RIGHT,
    QM_ICON_COUNT
} qm_icon_kind;
```

- [ ] **Step 3: Add the matching entry to `tools/convert-icons.py`**

Insert at the same relative position (after the `QM_ICON_JOYSTICK` entry, before `QM_ICON_TITLE`):

```python
    ("QM_ICON_SETTINGS", "settings.svg", ICON_SIZE, ICON_SIZE),
```

- [ ] **Step 4: Regenerate `qm_icon_data.c`**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
python3 tools/convert-icons.py
```

- [ ] **Step 5: Verify the new asset rasterized (not blank)**

```bash
grep -A3 "QM_ICON_SETTINGS" package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c | head -10
```

Confirm non-all-zero alpha values (guards against a silent CairoSVG failure, same check as the Joystick icon's own Task 2).

- [ ] **Step 6: Run the host C test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass, including `test_qm_icons.c`'s "all icons draw something" loop (now covering 16 icons).

- [ ] **Step 7: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -40
```

- [ ] **Step 8: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/icons/src/settings.svg \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/qm_icon_data.c \
        tools/convert-icons.py
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add QM_ICON_SETTINGS icon asset

New hand-drawn gear icon, baked through the existing
tools/convert-icons.py pipeline. Used by the upcoming Daemon Settings
main-menu item.
EOF
)"
```

- [ ] **Step 9: Regenerate patch capture**

---

### Task 3: quickmenu-side GET_CONFIG/RELOAD_CONFIG client + config-file writer

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/qm_joystick.c`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`

**Interfaces:**
- Consumes: `qm_joystick.c`'s existing internal static helpers (`qm_joystick_connect()`, `qm_joystick_set_timeout()`, `qm_joystick_read_line()`) — reused as-is, in the same translation unit, no header changes needed for these (they stay `static`).
- Produces: `int qm_daemon_config_get(double *fan_on_temp, double *fan_off_temp, int *fan_poll_interval_s, int *switch_debounce_ms, int *fan_on)`, `int qm_daemon_config_reload(void)`, `int qm_daemon_config_write(double fan_on_temp, double fan_off_temp, int fan_poll_interval_s, int switch_debounce_ms)` — Task 4 calls all three.

- [ ] **Step 1: Add declarations to `quickmenu.h`**

Read the file fresh. Add after the existing `qm_joystick.c` section's three declarations:

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

/* Connects, sends "RELOAD_CONFIG\n", reads one line. Returns 1 on "OK",
 * 0 on any failure (this command always replies OK from the daemon
 * side, so 0 here means a connection-level failure, not a rejected
 * reload). */
int qm_daemon_config_reload(void);

/* Writes the 4 tunables to /userdata/system/configs/circuitsword.conf,
 * overwriting the whole file (this file has exactly these 4 known keys
 * today -- no comment/unknown-line preservation attempted, matching the
 * simplicity of the file format itself). Returns 0 on success, -1 on
 * write failure (caller must not send RELOAD_CONFIG if this fails). */
int qm_daemon_config_write(double fan_on_temp, double fan_off_temp,
                            int fan_poll_interval_s, int switch_debounce_ms);
```

- [ ] **Step 2: Implement in `qm_joystick.c`**

Add `#include <stdlib.h>` to the file's includes (for `strtod`/`atoi`). Append these three functions at the end of the file:

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

- [ ] **Step 3: Run the host C test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass (no new test file required for this task — these functions follow the exact same shape as `qm_joystick_status()`/`qm_joystick_toggle()`, already covered by `test_qm_joystick.c`'s no-listener defensive-return tests in spirit; adding near-duplicate tests for near-duplicate functions is not required, but if the implementer wants to extend `test_qm_joystick.c` with a "no listener -> qm_daemon_config_get returns -1" style check, matching the existing file's pattern, that is a reasonable, low-cost addition — use judgment).

- [ ] **Step 4: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -40
```

- [ ] **Step 5: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c "GET_CONFIG\|circuitsword.conf"
```

Expected: nonzero.

- [ ] **Step 6: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/qm_joystick.c \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.h
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add GET_CONFIG/RELOAD_CONFIG client + config writer

Reuses qm_joystick.c's existing socket helpers to talk to the daemon's
new GET_CONFIG/RELOAD_CONFIG commands, plus a small writer for
circuitsword.conf's 4 known keys. Used by the upcoming Daemon Settings
submenu.
EOF
)"
```

- [ ] **Step 7: Regenerate patch capture**

---

### Task 4: quickmenu Daemon Settings UI

**Files:**
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.h`
- Modify: `package/batocera/utils/circuitsword-quickmenu/quickmenu.c`
- Test: `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` (extend existing `qm_render()` coverage)

**Interfaces:**
- Consumes: `QM_ICON_SETTINGS` (Task 2), `qm_daemon_config_get()`/`qm_daemon_config_reload()`/`qm_daemon_config_write()` (Task 3).

- [ ] **Step 1: Read `quickmenu.c`/`quickmenu.h` fresh**

Confirm current shape after the Joystick sub-project's changes (item/screen defines, `qm_state` fields, `qm_render()`'s screen-branch structure, `main()`'s screen-scoped input-handling `if/else`).

- [ ] **Step 2: Extend `quickmenu.h`'s item/screen model**

Add one more main-menu item and one more screen constant, plus 5 row indices for the settings submenu (4 editable + 1 status row) and new `qm_state` fields:

```c
#define QM_ITEM_WIFI            0
#define QM_ITEM_VOLUME          1
#define QM_ITEM_BRIGHTNESS      2
#define QM_ITEM_JOYSTICK        3
#define QM_ITEM_DAEMON_SETTINGS 4
#define QM_ITEM_COUNT           5

#define QM_SCREEN_MAIN            0
#define QM_SCREEN_JOYSTICK        1
#define QM_SCREEN_DAEMON_SETTINGS 2

#define QM_DS_FAN_ON_TEMP    0
#define QM_DS_FAN_OFF_TEMP   1
#define QM_DS_POLL_INTERVAL  2
#define QM_DS_DEBOUNCE_MS    3
#define QM_DS_STATUS_ROW     4
#define QM_DS_COUNT          5
```

Add to `qm_state` (after the existing `joy_status[6]` field):

```c
    int ds_selected;         /* 0 .. QM_DS_COUNT-1, daemon-settings screen only */
    double ds_fan_on_temp;
    double ds_fan_off_temp;
    int ds_fan_poll_interval_s;
    int ds_switch_debounce_ms;
    int ds_fan_on;            /* live state from the daemon, 0 or 1 */
    int ds_loaded;            /* 0 until the first successful GET_CONFIG this session */
```

- [ ] **Step 3: Initialize the new state fields in `main()`**

After the existing joystick-state init block (`st.screen = QM_SCREEN_MAIN; st.joy_selected = 0; memcpy(st.joy_status, "000000", 6);`), add:

```c
    st.ds_selected = 0;
    st.ds_fan_on_temp = 0.0;
    st.ds_fan_off_temp = 0.0;
    st.ds_fan_poll_interval_s = 0;
    st.ds_switch_debounce_ms = 0;
    st.ds_fan_on = 0;
    st.ds_loaded = 0;
```

- [ ] **Step 4: Extend `qm_render()`'s main-screen item loop**

In the existing `if/else if` chain over main-screen items (currently WIFI/VOLUME/BRIGHTNESS/else-is-JOYSTICK), change the final `else` to an explicit `else if (item == QM_ITEM_JOYSTICK)` and add a new final `else` for `QM_ITEM_DAEMON_SETTINGS`:

```c
        } else if (item == QM_ITEM_JOYSTICK) {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_JOYSTICK, fg);
        } else {
            qm_draw_icon_rgba(fb, margin, y, QM_ICON_SETTINGS, fg);
        }
```

- [ ] **Step 5: Add the Daemon Settings render branch**

`qm_render()`'s screen dispatch currently has `if (st->screen == QM_SCREEN_MAIN) { ... } else { /* QM_SCREEN_JOYSTICK */ ... }`. Change the trailing `else` to an `else if (st->screen == QM_SCREEN_JOYSTICK) { ... }` (existing body unchanged) and add a new final `else` branch:

```c
    } else {
        /* QM_SCREEN_DAEMON_SETTINGS: same text-list pattern as the
         * Joystick submenu. Row labels show the current value inline
         * (not a separate column) since each row IS one value. */
        qm_fill_rect(fb, 0, 0, (int)fb->width, (int)fb->height, QM_COLOR_BG);

        char labels[QM_DS_COUNT][40];
        snprintf(labels[QM_DS_FAN_ON_TEMP], sizeof(labels[0]),
                 "Fan ON temp: %.1fC", st->ds_fan_on_temp);
        snprintf(labels[QM_DS_FAN_OFF_TEMP], sizeof(labels[0]),
                 "Fan OFF temp: %.1fC", st->ds_fan_off_temp);
        snprintf(labels[QM_DS_POLL_INTERVAL], sizeof(labels[0]),
                 "Fan poll interval: %ds", st->ds_fan_poll_interval_s);
        snprintf(labels[QM_DS_DEBOUNCE_MS], sizeof(labels[0]),
                 "Switch debounce: %dms", st->ds_switch_debounce_ms);
        snprintf(labels[QM_DS_STATUS_ROW], sizeof(labels[0]),
                 "Fan is currently: %s", st->ds_fan_on ? "ON" : "OFF");

        int row_h3 = QM_GLYPH_H * scale + 2 * scale;
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
            qm_draw_text(fb, margin, y3, labels[item], scale, fg3);
            y3 += row_h3;
        }
    }
```

- [ ] **Step 6: Restructure `main()`'s input handling for the new screen**

The existing `if (st.screen == QM_SCREEN_MAIN) { ... } else { /* QM_SCREEN_JOYSTICK */ ... }` needs to become a 3-way dispatch. Change the trailing `else` to `else if (st.screen == QM_SCREEN_JOYSTICK) { ... }` (existing body unchanged) and add a new final `else` branch. Also, in the `QM_SCREEN_MAIN` branch's existing `QM_EV_A`/`QM_EV_RIGHT` handling for entering the Joystick submenu, add a matching case for `QM_ITEM_DAEMON_SETTINGS`:

In the `QM_SCREEN_MAIN` branch's `case QM_EV_A:`, alongside the existing `else if (st.selected == QM_ITEM_JOYSTICK) { ... }`, add:

```c
                } else if (st.selected == QM_ITEM_DAEMON_SETTINGS) {
                    st.screen = QM_SCREEN_DAEMON_SETTINGS;
                    st.ds_selected = 0;
                    double fot, foft;
                    int poll, debounce, fan_on;
                    if (qm_daemon_config_get(&fot, &foft, &poll, &debounce, &fan_on) == 0) {
                        st.ds_fan_on_temp = fot;
                        st.ds_fan_off_temp = foft;
                        st.ds_fan_poll_interval_s = poll;
                        st.ds_switch_debounce_ms = debounce;
                        st.ds_fan_on = fan_on;
                        st.ds_loaded = 1;
                    }
```

(Mirror this same block in the `QM_EV_LEFT`/`QM_EV_RIGHT` case's `QM_ITEM_JOYSTICK` handling, alongside its existing joystick-entry logic, so both A and Right open the Daemon Settings screen the same way Joystick already works.)

Add the new final `else` branch for `QM_SCREEN_DAEMON_SETTINGS`:

```c
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
                if (st.ds_selected != QM_DS_STATUS_ROW) {
                    if (qm_daemon_config_write(st.ds_fan_on_temp, st.ds_fan_off_temp,
                                                 st.ds_fan_poll_interval_s,
                                                 st.ds_switch_debounce_ms) == 0) {
                        qm_daemon_config_reload();
                        double fot, foft;
                        int poll, debounce, fan_on;
                        if (qm_daemon_config_get(&fot, &foft, &poll, &debounce, &fan_on) == 0) {
                            st.ds_fan_on = fan_on;
                        }
                    }
                } else {
                    dirty = 0;
                }
                break;
            default:
                dirty = 0;
                break;
            }
        }
```

- [ ] **Step 7: Extend the host test suite's `qm_render()` coverage**

Read `/Users/bas/Circuit-Sword Batocera/tests/test_qm_font.c` fresh. After the existing joystick-submenu blocks, add:

```c
    printf("qm_render daemon settings screen\n");
    {
        qm_fb *fb = make_fb(320, 240);
        qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS, .ds_selected = 0,
                         .ds_fan_on_temp = 58.0, .ds_fan_off_temp = 50.0,
                         .ds_fan_poll_interval_s = 3, .ds_switch_debounce_ms = 800,
                         .ds_fan_on = 1 };
        qm_render(fb, &st);
        check(count_nonzero(fb) > 0, "renders something");
        check(qm_get_pixel(fb, 0, 0) == QM_COLOR_BG, "background painted");
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "daemon settings screen stays within panel width");
        free_fb(fb);
    }

    printf("qm_render daemon settings screen at 640x480\n");
    {
        qm_fb *fb = make_fb(640, 480);
        qm_state st = { .screen = QM_SCREEN_DAEMON_SETTINGS, .ds_selected = QM_DS_DEBOUNCE_MS,
                         .ds_fan_on_temp = 60.5, .ds_fan_off_temp = 45.0,
                         .ds_fan_poll_interval_s = 5, .ds_switch_debounce_ms = 1200,
                         .ds_fan_on = 0 };
        qm_render(fb, &st);
        int rightmost = rightmost_nonbg_x(fb);
        check(rightmost >= 0 && rightmost < (int)fb->width,
              "640x480 daemon settings screen stays within panel width");
        free_fb(fb);
    }
```

- [ ] **Step 8: Run the host test suite**

```bash
cd "/Users/bas/Circuit-Sword Batocera/tests"
./run-c-tests.sh
```

Expected: all pass.

- [ ] **Step 9: Buildroot rebuild**

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/env.sh"
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=circuitsword-quickmenu-rebuild
' 2>&1 | tail -40
```

- [ ] **Step 10: Verify in the Docker output volume**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  strings /bcm2837/target/usr/bin/circuitsword-quickmenu | grep -c "Fan ON temp\|Switch debounce"
```

Expected: nonzero.

- [ ] **Step 11: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-quickmenu/quickmenu.h \
        package/batocera/utils/circuitsword-quickmenu/quickmenu.c
git commit -m "$(cat <<'EOF'
circuitsword-quickmenu: add Daemon Settings submenu

New 5th main-menu item opens a submenu for the daemon's 4 tunables
(fan on/off thresholds, fan poll interval, switch debounce) plus a
live fan-state status row. Edits write circuitsword.conf directly and
trigger an immediate RELOAD_CONFIG -- no daemon restart needed.
EOF
)"
```

- [ ] **Step 12: Regenerate patch capture**

- [ ] **Step 13: Write the findings log**

Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE6-DAEMON-SETTINGS-FINDINGS.md`, same structure as `PHASE6-JOYSTICK-CALIBRATION-FINDINGS.md`:

```markdown
# Daemon Settings Surface: Findings

Implements `docs/superpowers/specs/2026-08-11-daemon-settings-design.md`.
Adds live-reloadable config + a "Daemon Settings" submenu in
circuitsword-quickmenu for the daemon's 4 existing tunables, reusing the
joystick sub-project's Unix-domain socket (GET_CONFIG/RELOAD_CONFIG,
alongside the existing joystick commands).

## What was built

- `rpi-circuitsword.py`: config moved from per-thread load-once snapshots
  to a shared, lock-protected `_current_config` that `fan_thread`/
  `switch_monitor` read every poll tick; `reload_config()` logs each
  changed key; `GET_CONFIG`/`RELOAD_CONFIG` added to the existing socket
  dispatch.
- `circuitsword-quickmenu`: new `QM_ICON_SETTINGS` (hand-drawn gear,
  baked via the existing icon pipeline); `qm_joystick.c` gains
  `qm_daemon_config_get/reload/write`; new 5th main-menu item opening a
  `QM_SCREEN_DAEMON_SETTINGS` submenu with 4 editable rows + 1 live
  status row (current fan on/off state).

## Verified off-device

- Host C test suite covers the new render branch at both panel sizes
  (renders without crashing, stays within panel bounds).
- Both `PKG=rpigpioswitch-reinstall` and `PKG=circuitsword-quickmenu-rebuild`
  Buildroot builds succeed; Docker-volume inspection confirms the new
  symbols/strings are present in both installed artifacts.

## Needs on-device validation (not yet done)

- **Live reload actually working**: change `fan_on_temp` from the new
  screen, confirm the fan's real on/off behavior changes within one
  `fan_poll_interval_s` without a daemon restart.
- **`switch_debounce_ms` live change**: confirm the power-switch poll
  loop's timing actually reflects a changed value without restart.
- **Logging usefulness**: confirm `logread | grep rpi-circuitsword`
  after a `RELOAD_CONFIG` clearly shows which keys changed and to what
  value -- this was the user's explicit "so we know what works and
  what doesn't" ask for this sub-project.
- **Live fan-state status row accuracy**: confirm the "Fan is currently:
  ON/OFF" row matches the fan's real physical state, including right
  after a fresh submenu entry (not stale from a previous session).
- **Socket command coexistence**: confirm `GET_CONFIG`/`RELOAD_CONFIG`
  requests don't interfere with in-flight joystick commands on the same
  socket (both share one accept loop, one connection at a time,
  sequential by construction -- should be fine, but not yet exercised
  on real hardware with both submenus used in the same session).
```

- [ ] **Step 14: Self-review**

Read the final combined diff across all 4 tasks. Confirm: `QM_ITEM_COUNT`/`QM_DS_COUNT` used consistently everywhere; the socket path string is unchanged (still `/var/run/circuitsword-joystick.sock`, byte-identical in both files); `GET_CONFIG`'s reply format string in the Python daemon and the `sscanf` format string in the C client match EXACTLY (same key order, same separators) — a mismatch here would silently fail every reply.
