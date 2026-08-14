# AC/Battery Detection Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `batocera-battery-checker`'s AC-vs-battery detection actually work, by exposing a real `online` property on the `circuitsword_battery` virtual power-supply device, driven by the already-wired-but-unused `GPIO_PIN_POWER_GOOD` (GPIO 38).

**Architecture:** Two pieces, one on each side of the existing `capacity`/`charging` module-parameter bridge pattern: (1) the `circuitsword_battery` kernel module gains a third module parameter, `online`, exposed as `POWER_SUPPLY_PROP_ONLINE`; (2) the `rpi-circuitsword.py` daemon's existing `charging_thread()` (which already watches GPIO 36 for `charging`) is extended to watch GPIO 38 in the same `gpiod` request and write `online` the same way it writes `charging`. No changes to `batocera-battery-checker` or `batocera-power-mode` — they already read `/sys/class/power_supply/*/online` correctly, they just have nothing to read today.

**Tech Stack:** C (Linux kernel module, `power_supply` subsystem), Python 3 (`gpiod` line-request API), Buildroot/Docker build pipeline.

## Global Constraints

- New kernel module parameter is named exactly `online` (int, default `0`), written by `rpi-circuitsword.py`, mirroring the existing `charging` parameter's declaration style byte-for-byte (same qualifiers, same comment style).
- GPIO 38 (`GPIO_PIN_POWER_GOOD`, already declared in `rpi-circuitsword.py`, "USB power good, active HIGH") is the sole source for `online`. No other detection mechanism.
- GPIO 36 (charging) and GPIO 38 (power-good) are requested together in **one** `gpiod.request_lines()` call / one request object — not two separate requests. This applies to **both** code paths in `charging_thread()`: the primary edge-detection path and the plain-polling fallback path (for older `python3-gpiod` bindings without edge-detection support). Missing either path leaves that path silently broken.
- `batocera-battery-checker` and `batocera-power-mode` are **out of scope** — do not modify them.
- No UI changes of any kind — this is a background-only fix.
- No host-side unit test for the GPIO-thread logic — GPIO access can't be host-tested without hardware, and the design doc explicitly marks this on-device-only. Per CLAUDE.md Hard Rule #7: a full image build does NOT pick up edited source in already-built packages — every task below force-refreshes its package and verifies directly against the Docker-volume build artifact, never trusting a clean build log alone.
- Patch capture after every task: `cd /Users/bas/batocera-build-wifi/batocera.linux && git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"`.

---

### Task 1: Kernel module — add `online` property

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/circuitsword-battery/circuitsword_battery.c`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: a new writable sysfs node `/sys/module/circuitsword_battery/parameters/online` (int, `0`/`1`) and a new `POWER_SUPPLY_PROP_ONLINE` property on the `circuitsword-battery` power-supply device. Task 2 writes to this sysfs node.

- [ ] **Step 1: Add the `online` module parameter**

Current lines 22-27:

```c
static int capacity = 50;          /* 0..100, written by rpi-circuitsword.py */
static int charging;               /* 0 = discharging, 1 = charging */
module_param(capacity, int, 0644);
MODULE_PARM_DESC(capacity, "Battery charge level 0-100 (written by rpi-circuitsword.py)");
module_param(charging, int, 0644);
MODULE_PARM_DESC(charging, "1 = charging, 0 = discharging (written by rpi-circuitsword.py)");
```

Replace with:

```c
static int capacity = 50;          /* 0..100, written by rpi-circuitsword.py */
static int charging;               /* 0 = discharging, 1 = charging */
static int online;                 /* 0 = no external power, 1 = external power present */
module_param(capacity, int, 0644);
MODULE_PARM_DESC(capacity, "Battery charge level 0-100 (written by rpi-circuitsword.py)");
module_param(charging, int, 0644);
MODULE_PARM_DESC(charging, "1 = charging, 0 = discharging (written by rpi-circuitsword.py)");
module_param(online, int, 0644);
MODULE_PARM_DESC(online, "1 = external/USB power present, 0 = not present (written by rpi-circuitsword.py)");
```

- [ ] **Step 2: Add `POWER_SUPPLY_PROP_ONLINE` to the property table**

Current lines 29-35:

```c
static enum power_supply_property circuitsword_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
};
```

Replace with:

```c
static enum power_supply_property circuitsword_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_ONLINE,
};
```

- [ ] **Step 3: Handle `POWER_SUPPLY_PROP_ONLINE` in `circuitsword_battery_get_property()`**

Current lines 41-60:

```c
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = charging ? POWER_SUPPLY_STATUS_CHARGING
				       : POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = clamp(capacity, 0, 100);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	default:
		return -EINVAL;
	}
	return 0;
```

Replace with:

```c
	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = charging ? POWER_SUPPLY_STATUS_CHARGING
				       : POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = clamp(capacity, 0, 100);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = online;
		break;
	default:
		return -EINVAL;
	}
	return 0;
```

No `clamp()` needed for `online` — it's driven by a clean GPIO 0/1 edge value, unlike `capacity` which can arrive as an arbitrary computed percentage.

- [ ] **Step 4: Verify the module still compiles conceptually**

This is a kernel module — it cannot be compiled on the host (no matching kernel headers here). Skip straight to the Buildroot rebuild in Step 5, which is the real compile check.

- [ ] **Step 5: Buildroot verification (Hard Rule #7 — force rebuild, don't trust a clean log)**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=circuitsword-battery bcm2837-pkg
```

If `PKG=circuitsword-battery bcm2837-pkg` doesn't force a recompile of an already-built kernel module (some kernel-module packages only rebuild on a `-rebuild` suffix), use:

```bash
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=circuitsword-battery-rebuild bcm2837-pkg
```

Expected: build completes with no `Error 1`/`Error 2` lines.

- [ ] **Step 6: Verify directly against the Docker-volume artifact**

Find the built `.ko` inside the volume and confirm it actually contains the new parameter name — a clean build log alone is not proof, per Hard Rule #7:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "
find /bcm2837 -name 'circuitsword_battery.ko' 2>/dev/null | while read -r ko; do
  echo \"=== \$ko ===\"
  strings \"\$ko\" | grep -i online
done
"
```

Expected: at least one match containing `online` (the parameter name and/or the `MODULE_PARM_DESC` text) from the `.ko` inside the volume.

- [ ] **Step 7: Commit and capture the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/circuitsword-battery/circuitsword_battery.c
git commit -m "circuitsword-battery: add online power_supply property"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 2: Daemon — extend `charging_thread()` to also drive `online`

**Files:**
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`

**Interfaces:**
- Consumes: `GPIO_PIN_POWER_GOOD` (already declared, line 29: `GPIO_PIN_POWER_GOOD = 38`), `BATTERY_SYSFS_DIR` (already declared, line 319: `"/sys/module/circuitsword_battery/parameters"`), and the `online` sysfs parameter Task 1 adds at `{BATTERY_SYSFS_DIR}/online`.
- Produces: `write_online_sysfs(online: bool) -> None`, and `charging_thread()` extended to poll/edge-watch both GPIO 36 and GPIO 38. Nothing later in this plan consumes these directly — this is the last task.

- [ ] **Step 1: Add `write_online_sysfs()`**

Current `write_charging_sysfs()` (around line 360-365):

```python
def write_charging_sysfs(charging: bool):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/charging", "w") as f:
            f.write("1" if charging else "0")
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)
```

Add immediately after it:

```python
def write_online_sysfs(online: bool):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/online", "w") as f:
            f.write("1" if online else "0")
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)
```

- [ ] **Step 2: Update the stale comment above the battery section**

The comment block above `BATT_VOLTSCALE` (around line 301-309) currently reads:

```python
# ============================================================
# Battery: CMD_GET_VOLT ('c') -> 2-byte raw ADC -> voltage -> percent.
# Percent-only here; charging state (GPIO 36, active HIGH) is NOT read in
# this section -- it's owned exclusively by charging_thread() below, which
# reads it directly via GPIO (not over the serial link), independently of
# this section's 30s serial poll. See hardware_read_charging()/
# hardware_read_power_good() in Retropie_source/cs-hud_new/src/hardware.c
# for the original combined-read reference. Constants below are from
# Retropie_source/cs-hud_new/src/config.h.
# ============================================================
```

Read the current exact text fresh before editing (it may have shifted a line or two from other work today), then update it to reflect that `charging_thread()` now owns *both* GPIO 36 (charging) and GPIO 38 (power-good/online), e.g.:

```python
# ============================================================
# Battery: CMD_GET_VOLT ('c') -> 2-byte raw ADC -> voltage -> percent.
# Percent-only here; charging state (GPIO 36, active HIGH) and external-
# power-present state (GPIO 38, active HIGH) are NOT read in this section
# -- both are owned exclusively by charging_thread() below, which reads
# them directly via GPIO (not over the serial link), independently of
# this section's 30s serial poll. See hardware_read_charging()/
# hardware_read_power_good() in Retropie_source/cs-hud_new/src/hardware.c
# for the original combined-read reference. Constants below are from
# Retropie_source/cs-hud_new/src/config.h.
# ============================================================
```

- [ ] **Step 3: Read the current `charging_thread()` in full before editing**

Read `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` lines 383-449 fresh right before this step — it has two separate code paths (primary edge-detection, and a plain-polling fallback for older `gpiod` bindings), each with its own `gpiod.request_lines()` call and its own loop. Both need editing. Do not edit from memory of the excerpt below; confirm line numbers and exact current text first, since other work may have touched this file today.

- [ ] **Step 4: Extend the primary edge-detection path's `request_lines()` call**

The current primary-path request (inside the outer `try:` block):

```python
    try:
        request = gpiod.request_lines(
            "/dev/gpiochip0",
            consumer="circuitsword-charging",
            config={GPIO_PIN_CHARGING: gpiod.LineSettings(
                direction=Direction.INPUT,
                edge_detection=Edge.BOTH,
            )},
        )
```

Change the `config={...}` dict to request both lines in one call:

```python
    try:
        request = gpiod.request_lines(
            "/dev/gpiochip0",
            consumer="circuitsword-charging",
            config={
                GPIO_PIN_CHARGING: gpiod.LineSettings(
                    direction=Direction.INPUT,
                    edge_detection=Edge.BOTH,
                ),
                GPIO_PIN_POWER_GOOD: gpiod.LineSettings(
                    direction=Direction.INPUT,
                    edge_detection=Edge.BOTH,
                ),
            },
        )
```

- [ ] **Step 5: Extend the plain-polling fallback path's `request_lines()` call**

The current fallback-path request (inside the `except Exception as e:` block, the second `gpiod.request_lines()` call):

```python
        try:
            request = gpiod.request_lines(
                "/dev/gpiochip0",
                consumer="circuitsword-charging",
                config={GPIO_PIN_CHARGING: gpiod.LineSettings(direction=Direction.INPUT)},
            )
        except OSError as e2:
            print(f"[rpi-circuitsword] charging GPIO unavailable, "
                  f"charging state will not be updated: {e2}", file=sys.stderr)
            return
```

Change the `config={...}` dict the same way (no `edge_detection=` here — this fallback path doesn't use it, matching its existing style):

```python
        try:
            request = gpiod.request_lines(
                "/dev/gpiochip0",
                consumer="circuitsword-charging",
                config={
                    GPIO_PIN_CHARGING: gpiod.LineSettings(direction=Direction.INPUT),
                    GPIO_PIN_POWER_GOOD: gpiod.LineSettings(direction=Direction.INPUT),
                },
            )
        except OSError as e2:
            print(f"[rpi-circuitsword] charging GPIO unavailable, "
                  f"charging/online state will not be updated: {e2}", file=sys.stderr)
            return
```

- [ ] **Step 6: Extend the plain-polling fallback loop**

The current fallback loop:

```python
        try:
            last_charging = None
            while not stop_event.is_set():
                charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
                if charging != last_charging:
                    write_charging_sysfs(charging)
                    print(f"[rpi-circuitsword] charging={charging}", file=sys.stderr)
                    last_charging = charging
                stop_event.wait(CHARGING_FALLBACK_POLL_S)
        finally:
            request.release()
        return
```

Replace with:

```python
        try:
            last_charging = None
            last_online = None
            while not stop_event.is_set():
                charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
                if charging != last_charging:
                    write_charging_sysfs(charging)
                    print(f"[rpi-circuitsword] charging={charging}", file=sys.stderr)
                    last_charging = charging
                online = request.get_value(GPIO_PIN_POWER_GOOD) == Value.ACTIVE
                if online != last_online:
                    write_online_sysfs(online)
                    print(f"[rpi-circuitsword] online={online}", file=sys.stderr)
                    last_online = online
                stop_event.wait(CHARGING_FALLBACK_POLL_S)
        finally:
            request.release()
        return
```

- [ ] **Step 7: Extend the primary edge-detection path's seed + loop**

The current primary path body (inside the final `try:`/`finally:`):

```python
    try:
        # Seed initial state immediately -- don't wait for the first real
        # edge, or consumers see stale/default data until the charger is
        # next toggled.
        last_charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
        write_charging_sysfs(last_charging)
        print(f"[rpi-circuitsword] charging={last_charging} (initial)", file=sys.stderr)

        while not stop_event.is_set():
            if request.wait_edge_events(CHARGING_EDGE_TIMEOUT_S):
                request.read_edge_events()
                charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
                if charging != last_charging:
                    write_charging_sysfs(charging)
                    print(f"[rpi-circuitsword] charging={charging}", file=sys.stderr)
                    last_charging = charging
    finally:
        request.release()
```

Replace with:

```python
    try:
        # Seed initial state immediately -- don't wait for the first real
        # edge, or consumers see stale/default data until the charger is
        # next toggled.
        last_charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
        write_charging_sysfs(last_charging)
        print(f"[rpi-circuitsword] charging={last_charging} (initial)", file=sys.stderr)

        last_online = request.get_value(GPIO_PIN_POWER_GOOD) == Value.ACTIVE
        write_online_sysfs(last_online)
        print(f"[rpi-circuitsword] online={last_online} (initial)", file=sys.stderr)

        while not stop_event.is_set():
            if request.wait_edge_events(CHARGING_EDGE_TIMEOUT_S):
                request.read_edge_events()
                charging = request.get_value(GPIO_PIN_CHARGING) == Value.ACTIVE
                if charging != last_charging:
                    write_charging_sysfs(charging)
                    print(f"[rpi-circuitsword] charging={charging}", file=sys.stderr)
                    last_charging = charging
                online = request.get_value(GPIO_PIN_POWER_GOOD) == Value.ACTIVE
                if online != last_online:
                    write_online_sysfs(online)
                    print(f"[rpi-circuitsword] online={online}", file=sys.stderr)
                    last_online = online
    finally:
        request.release()
```

Both lines are read on every edge event regardless of which line actually fired — cheap, and avoids needing to inspect which line an event belongs to.

- [ ] **Step 8: Buildroot verification (Hard Rule #7 — force reinstall, don't trust a clean log)**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=rpigpioswitch-reinstall bcm2837-pkg
```

Expected: build completes with no `Error 1`/`Error 2` lines.

- [ ] **Step 9: Verify directly against the Docker-volume artifact**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "
grep -n 'write_online_sysfs' /bcm2837/target/usr/bin/rpi-circuitsword
grep -n 'GPIO_PIN_POWER_GOOD: gpiod.LineSettings' /bcm2837/target/usr/bin/rpi-circuitsword
"
```

Expected: both greps return at least one match each, confirming `write_online_sysfs` exists in the built script and `GPIO_PIN_POWER_GOOD` is present inside a `LineSettings(...)` entry (i.e. requested as a line, not just referenced elsewhere) in the deployed artifact — not just a clean build log.

- [ ] **Step 10: Commit and capture the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
git commit -m "rpi-circuitsword: drive online power_supply property from GPIO 38"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```
