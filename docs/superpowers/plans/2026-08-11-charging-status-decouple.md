# Charging-Status Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the charging-state sysfs attribute (and therefore the
statusbar/quickmenu charging icon) update within ~1 second of a real
plug/unplug event, instead of waiting up to 30 seconds for the next
battery-voltage serial poll.

**Architecture:** Split `rpi-circuitsword.py`'s single `battery_bridge()`
thread (which today reads both voltage-derived percent over serial AND
GPIO charging state in the same 30s loop tick) into two independent
threads: the existing `battery_bridge()` keeps only the slow serial
percent poll, and a new `charging_thread()` owns GPIO 36 exclusively via
libgpiod's edge-detection API, reacting to real plug/unplug transitions
instead of polling on a timer.

**Tech Stack:** Python 3, `python3-gpiod` (libgpiod v2 Python bindings,
already a dependency of this file for `fan_thread`/`switch_monitor`),
`threading`.

## Global Constraints

- `GPIO_PIN_CHARGING = 36`, active HIGH (existing constant, unchanged).
- Fallback poll interval if edge-detection is unavailable: `1.0` second.
- `wait_edge_events` timeout: `1.0` second (keeps `stop_event` checkable
  even when no edge ever fires, matching every other thread's shutdown
  shape in this file).
- Error-log message format on edge-detect setup failure, verbatim:
  `"[rpi-circuitsword] charging edge-detect unavailable, falling back to 1s polling: {e}"`
- Never PWM the fan (unrelated to this change, but a standing project
  rule — this plan does not touch `fan_thread`).
- `-j2` max for any on-device Buildroot builds (not applicable here — this
  package is plain-copy, no compilation).
- Per CLAUDE.md Hard Rule #7: `rpigpioswitch` is a plain-copy/config
  package — verification uses `PKG=rpigpioswitch-reinstall`, not
  `-rebuild`, and the reinstalled artifact must be inspected directly in
  the Docker output volume, not just trusted from a clean build log.

---

### Task 1: Split charging detection into its own edge-driven thread

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`
  - Lines 215-220 (`read_charging(request)`) — remove entirely.
  - Lines 223-230 (`write_battery_sysfs(percent, charging)`) — replace with
    two separate functions (see Step 3).
  - Lines 233-256 (`battery_bridge(stop_event)`) — remove its GPIO request
    and `read_charging()` call; write only percent (see Step 4).
  - Lines 768-776 (`main()`'s `threads = [...]` list) — add the new
    thread (see Step 5).

**Interfaces:**
- Consumes: `GPIO_PIN_CHARGING` (existing constant, line 28),
  `BATTERY_SYSFS_DIR` (existing constant, line 182), the existing
  `stop_event: threading.Event` convention every thread function in this
  file takes as its sole argument.
- Produces: `write_charging_sysfs(charging: bool) -> None`,
  `write_battery_percent_sysfs(percent: int) -> None`,
  `charging_thread(stop_event: threading.Event) -> None` — these are the
  final names other Phase 6 sub-projects (e.g. the low-battery
  warning/auto-shutdown work) may build on if they need to read/write
  charging state; no other task in this plan depends on them, this is a
  single-task plan.

This whole feature is one self-contained, single-file change fully
specified by the design doc
(`docs/superpowers/specs/2026-08-11-charging-status-decouple-design.md`)
— no test-first cycle applies (no host-testable unit here per the design
doc's own Testing section: this needs real gpiod + a real kernel GPIO
chip, neither available off-device). Verification is a Buildroot
reinstall plus direct inspection of the installed artifact, done at the
end of this task.

- [ ] **Step 1: Read the file fresh and confirm current line numbers**

Open `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` and
confirm `read_charging`, `write_battery_sysfs`, and `battery_bridge` are
still at approximately lines 215, 223, and 233 as listed above (this repo
had no other commits touching this file since the plan was written, but
always verify against the live file before editing — never edit blind
from line numbers alone).

- [ ] **Step 2: Remove `read_charging()`**

Delete this function entirely (currently lines 215-220):

```python
def read_charging(request) -> bool:
    from gpiod.line import Value
    try:
        return request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE  # active HIGH
    except OSError:
        return False
```

Its logic moves inline into `charging_thread()` in Step 6 — it has a
single caller after this change, so a standalone helper isn't worth
keeping.

- [ ] **Step 3: Replace `write_battery_sysfs()` with two single-purpose writers**

Replace this function (currently lines 223-230):

```python
def write_battery_sysfs(percent: int, charging: bool):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/capacity", "w") as f:
            f.write(str(percent))
        with open(f"{BATTERY_SYSFS_DIR}/charging", "w") as f:
            f.write("1" if charging else "0")
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)
```

with:

```python
def write_battery_percent_sysfs(percent: int):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/capacity", "w") as f:
            f.write(str(percent))
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)


def write_charging_sysfs(charging: bool):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/charging", "w") as f:
            f.write("1" if charging else "0")
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)
```

- [ ] **Step 4: Trim `battery_bridge()` to percent-only**

Replace the whole function (currently lines 233-256):

```python
def battery_bridge(stop_event: threading.Event):
    import gpiod
    from gpiod.line import Direction
    request = gpiod.request_lines(
        "/dev/gpiochip0",
        consumer="circuitsword-charging",
        config={GPIO_PIN_CHARGING: gpiod.LineSettings(direction=Direction.INPUT)},
    )

    last_percent = 50  # matches the kernel module's own initial default
    last_logged_percent = None
    while not stop_event.is_set():
        last_percent = read_battery_percent(last_percent)
        charging = read_charging(request)
        write_battery_sysfs(last_percent, charging)
        # Log every poll (every BATTERY_POLL_INTERVAL_S, not spammy) so
        # `logread | grep rpi-circuitsword` shows the battery is actually
        # being read and pushed, not just on change.
        if last_percent != last_logged_percent:
            print(f"[rpi-circuitsword] battery {last_percent}% charging={charging}", file=sys.stderr)
            last_logged_percent = last_percent
        stop_event.wait(BATTERY_POLL_INTERVAL_S)

    request.release()
```

with:

```python
def battery_bridge(stop_event: threading.Event):
    last_percent = 50  # matches the kernel module's own initial default
    last_logged_percent = None
    while not stop_event.is_set():
        last_percent = read_battery_percent(last_percent)
        write_battery_percent_sysfs(last_percent)
        # Log every poll (every BATTERY_POLL_INTERVAL_S, not spammy) so
        # `logread | grep rpi-circuitsword` shows the battery is actually
        # being read and pushed, not just on change.
        if last_percent != last_logged_percent:
            print(f"[rpi-circuitsword] battery {last_percent}%", file=sys.stderr)
            last_logged_percent = last_percent
        stop_event.wait(BATTERY_POLL_INTERVAL_S)
```

Note: `battery_bridge` no longer requests any GPIO line, so it no longer
needs a `finally`/`request.release()` — there's nothing left to release.

- [ ] **Step 5: Add `charging_thread()`**

Insert this new function directly after `battery_bridge()` (i.e. still
within the "Battery" section of the file, before the "Backlight" section
that currently starts around line 259):

```python
CHARGING_EDGE_TIMEOUT_S = 1.0   # wait_edge_events() timeout, keeps stop_event checkable
CHARGING_FALLBACK_POLL_S = 1.0  # plain-poll interval if edge-detection is unavailable


def charging_thread(stop_event: threading.Event):
    import gpiod
    from gpiod.line import Direction, Edge, Value

    try:
        request = gpiod.request_lines(
            "/dev/gpiochip0",
            consumer="circuitsword-charging",
            config={GPIO_PIN_CHARGING: gpiod.LineSettings(
                direction=Direction.INPUT,
                edge_detection=Edge.BOTH,
            )},
        )
    except OSError as e:
        print(f"[rpi-circuitsword] charging edge-detect unavailable, "
              f"falling back to 1s polling: {e}", file=sys.stderr)
        request = gpiod.request_lines(
            "/dev/gpiochip0",
            consumer="circuitsword-charging",
            config={GPIO_PIN_CHARGING: gpiod.LineSettings(direction=Direction.INPUT)},
        )
        try:
            while not stop_event.is_set():
                charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
                write_charging_sysfs(charging)
                stop_event.wait(CHARGING_FALLBACK_POLL_S)
        finally:
            request.release()
        return

    try:
        # Seed initial state immediately -- don't wait for the first real
        # edge, or consumers see stale/default data until the charger is
        # next toggled.
        charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
        write_charging_sysfs(charging)
        print(f"[rpi-circuitsword] charging={charging} (initial)", file=sys.stderr)

        while not stop_event.is_set():
            if request.wait_edge_events(CHARGING_EDGE_TIMEOUT_S):
                request.read_edge_events()
                charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
                write_charging_sysfs(charging)
                print(f"[rpi-circuitsword] charging={charging}", file=sys.stderr)
    finally:
        request.release()
```

Two notes for the implementer:
- `request.wait_edge_events(timeout)` returns `True`/truthy when at least
  one event is pending; `request.read_edge_events()` drains the event
  queue (its return value is unused here — only the current line level
  after the edge matters, not the queued events themselves).
- The `try/except OSError` around the first `request_lines(...)` call is
  the fallback path from the design doc's Error Handling section — if
  `edge_detection=Edge.BOTH` itself isn't supported by the kernel/gpiochip,
  requesting the line that way raises, and the fallback re-requests the
  same line without `edge_detection` and polls it plainly instead.

- [ ] **Step 6: Register the new thread in `main()`**

In the `threads = [...]` list (currently lines 768-776), add one entry
after the existing `"battery"` entry:

```python
    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=charging_thread, args=(stop_event,), name="charging", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=volume_bridge, args=(stop_event,), name="volume", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
        threading.Thread(target=statusbar_thread, args=(stop_event,), name="statusbar", daemon=True),
    ]
```

- [ ] **Step 7: Sanity-check with the host Python (syntax only, not gpiod)**

`gpiod` isn't installed on the host (it's a target-only ARM package), so
a full import test isn't possible here. Instead, just confirm the edited
file is syntactically valid Python:

Run: `python3 -m py_compile "package/batocera/utils/rpigpioswitch/rpi-circuitsword.py"`
Expected: no output, exit code 0 (a `SyntaxError` would print a traceback
and exit non-zero).

- [ ] **Step 8: Buildroot reinstall**

This is a plain-copy/config package (per CLAUDE.md Hard Rule #7) — use
the cheaper `-reinstall` target, not `-rebuild`. Same env-sourcing
pattern used by every prior plan this session (`env.sh` needs `bash`, not
`zsh` — wrap in `bash -c '...'` if the shell is zsh; `BATOCERA_SRC` must
be explicitly exported or `env.sh` defaults to a stale checkout):

```bash
bash -c '
  export BATOCERA_SRC=/Users/bas/batocera-build-wifi/batocera.linux
  source ./env.sh
  cd "$BATOCERA_SRC"
  make BR_DOCKER_VOLUMES=1 O="$OUTPUT_DIR/bcm2837" BR2_EXTERNAL="$BATOCERA_SRC" DL_DIR="$DL_DIR" BATCH_MODE=1 bcm2837-pkg PKG=rpigpioswitch-reinstall
' 2>&1 | tail -40
```

Expected: build completes with no errors; log shows
`rpigpioswitch` being reinstalled (its `INSTALL_TARGET_CMDS` re-copying
`rpi-circuitsword.py` to the target).

- [ ] **Step 9: Verify the fix landed in the Docker output volume**

Per Hard Rule #7, never trust a clean build log alone — inspect the
actual installed artifact:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine \
  grep -c "charging_thread\|write_charging_sysfs\|write_battery_percent_sysfs" \
  /bcm2837/target/usr/bin/rpi-circuitsword.py
```

(Adjust the in-container path if the installed script lives elsewhere —
confirm via `docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine
find /bcm2837/target -name rpi-circuitsword.py` first if unsure.)

Expected: nonzero count — confirms all three new/renamed symbols are
present in the target-installed copy, not just the source tree.

- [ ] **Step 10: Commit**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "$(cat <<'EOF'
rpi-circuitsword: decouple charging-state detection from the 30s battery poll

battery_bridge() previously read GPIO charging state in the same loop
tick as the slow (30s) serial voltage poll, so a plug/unplug could take
up to 30s to show up. charging_thread() now owns GPIO 36 independently
via libgpiod edge-detection, reacting within ~1s (polling fallback if
edge-detection isn't supported).
EOF
)"
```

- [ ] **Step 11: Regenerate patch capture**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Verify the new patch file contains the change:

```bash
grep -c "charging_thread" "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

Expected: nonzero.

- [ ] **Step 12: Write the findings log**

Create `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE6-CHARGING-DECOUPLE-FINDINGS.md`,
following the structure of
`docs/superpowers/plans/findings/PHASE4-TRANSLUCENT-STATUSBAR-FINDINGS.md` (What
was built / Verified off-device / Needs on-device validation):

```markdown
# Charging-Status Decoupling: Findings

Implements `docs/superpowers/specs/2026-08-11-charging-status-decouple-design.md`.
Splits GPIO charging-state detection out of `battery_bridge()`'s slow
30s serial-voltage poll into its own edge-driven `charging_thread()`.

## What was built

- `charging_thread()` (new): owns GPIO 36 exclusively via libgpiod
  edge-detection (`Edge.BOTH`), reacting to real plug/unplug transitions
  instead of polling on a timer. Falls back to a 1s poll loop if
  edge-detection setup fails (kernel/gpiochip doesn't support it).
- `write_charging_sysfs()` / `write_battery_percent_sysfs()` (new):
  replace the old combined `write_battery_sysfs(percent, charging)` with
  two single-attribute writers, so the two threads never touch each
  other's sysfs file.
- `battery_bridge()` (modified): no longer requests GPIO 36 or reads
  charging state at all -- purely the 30s serial percent poll now.
- `read_charging()` / old `write_battery_sysfs()`: removed.
- `main()`: registers `charging_thread` as an 8th daemon thread.

## Verified off-device

- `python3 -m py_compile` confirms the edited file is syntactically valid
  (no real import-test possible -- `gpiod` is target-only, not installed
  on the host).
- `PKG=rpigpioswitch-reinstall` Buildroot reinstall completes with no
  errors.
- Direct inspection of the reinstalled artifact in the Docker output
  volume confirms `charging_thread`, `write_charging_sysfs`, and
  `write_battery_percent_sysfs` are present in the target-installed copy,
  not just the source tree.

## Needs on-device validation (not yet done)

- **Edge-detection actually works on this hardware/kernel**: this is the
  first use of libgpiod's edge-detection API in this codebase (every
  other GPIO thread here polls on a timer) -- confirm via
  `logread | grep rpi-circuitsword` that startup does NOT log the
  "charging edge-detect unavailable, falling back to 1s polling" message,
  and that `charging=... (initial)` appears once at startup.
- **Latency**: physically plug/unplug the charger, confirm the charging
  log line and `/sys/module/circuitsword_battery/parameters/charging`
  both update within ~1s, and that the status bar's charging icon
  (Phase 4) visibly updates at the same speed -- not the old up-to-30s
  delay.
- **Battery percentage unaffected**: confirm `battery_bridge`'s own 30s
  percent updates still work normally and are unaffected by splitting
  charging out (no missed voltage polls, no serial contention introduced
  by the new thread -- `charging_thread` never touches the serial port,
  so this should be a non-issue, but worth confirming in the logs).
```

- [ ] **Step 13: Self-review**

Read the final diff (`git show HEAD`) end to end and confirm: no leftover
references to `read_charging` or the old combined `write_battery_sysfs`
anywhere in the file (`grep -n "read_charging\|write_battery_sysfs\b"
package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` should return
nothing for `read_charging`, and only match `write_battery_percent_sysfs`
by substring for the second — confirm no exact `write_battery_sysfs(`
call sites remain); `battery_bridge`'s `finally`/`request.release()` was
correctly removed along with its GPIO request; the new thread is
registered exactly once in `main()`.
