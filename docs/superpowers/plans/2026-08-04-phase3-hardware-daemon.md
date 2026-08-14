# Phase 3 Hardware Daemon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Circuit-Sword hardware daemon on Batocera — temperature-controlled fan, battery status feeding EmulationStation's native battery icon, backlight control (both a physical mode+left/right button combo and Batocera's own brightness UI), and safe shutdown triggered by the physical power switch.

**Architecture:** Two new out-of-tree Buildroot kernel-module packages (`circuitsword-battery`, a near-verbatim port of the RetroPie `cs_battery.c` virtual `power_supply` device; `circuitsword-backlight`, a new virtual `backlight` class device) plus one new Python 3 daemon (`rpi-circuitsword.py`, using `gpiod`) added as a new case profile inside Batocera's existing `rpigpioswitch` package — not a standalone package. The daemon runs two threads: a fan-control thread (pure GPIO, no Arduino) and an Arduino-serial-bridge thread (battery, backlight, mode-button combo, power-switch monitoring, all sharing one mutex-protected UART).

**Tech Stack:** C (kernel modules, Linux `power_supply`/`backlight_device` APIs), Python 3 + `gpiod` (daemon), Buildroot `kernel-module`/`generic-package` macros, bash (`rpigpioswitch` shell glue).

## Global Constraints

- **Never PWM the fan.** On/off only, even though the ported reference code contains an unused software-PWM path — do not wire it up. (Hard project rule, `CLAUDE.md`.)
- **`-j2` max for anything built on-device** — not relevant to this plan (all builds are cross-compiled via Docker/Buildroot on the host), but never introduce an on-device build step.
- Fan: GPIO 35, active-LOW (0 = on, 1 = off). Hysteresis: on at ≥58.0°C, off below 50.0°C. Values overridable via `/userdata/system/configs/circuitsword.conf`.
- Power switch: GPIO 37. Pulled up: ON idles HIGH, OFF pulls LOW (`gpiod` reads `!= 0` → ON). **Unconfirmed on real hardware** — flag, don't assert as verified.
- Charging: GPIO 36, active HIGH (charging when pin reads 1). Power-good (USB present): GPIO 38, active HIGH. Both direct GPIO reads — **not** over the Arduino serial link.
- Arduino serial: `/dev/ttyACM0`, 9600 baud, single-byte command protocol. Commands used in this plan: `CMD_GET_VOLT` = `'c'` (2-byte raw ADC, low byte then high byte), `CMD_GET_BL` = `'q'` / `CMD_SET_BL` = `'Q'` (1-byte brightness %), `CMD_GET_STATUS` = `'s'` (1 byte, bit 0 = mode button).
- Battery ADC→voltage formula (from `Retropie_source/cs-hud_new/src/hardware.c` `hardware_read_battery_voltage()`), constants from `Retropie_source/cs-hud_new/src/config.h`:
  ```
  BATT_VOLTSCALE  = 203.5
  BATT_DACRES     = 33.0
  BATT_DACMAX     = 1023.0
  BATT_RESDIVMUL  = 4.0
  BATT_RESDIVVAL  = 1000.0
  voltage = ((raw * BATT_VOLTSCALE * BATT_DACRES + BATT_DACMAX * 5.0) / ((BATT_DACRES * BATT_RESDIVVAL) / BATT_RESDIVMUL)) / 100.0
  ```
  (**Correction found during Task 5's review**: the original formula recorded
  here was missing the source's final `/ 100.0` — `hardware_read_battery_voltage()`
  ends with `return v / 100.0;`, which this plan initially omitted. Without it,
  a raw ADC value of 512 produces a nonsensical ~417V instead of ~4.17V. Fixed
  in Task 5's implementation; corrected here for anyone re-reading this plan.)
- Voltage→percent (`hardware_voltage_to_percent()`), linear, clamped:
  ```
  BATT_VOLTAGE_MIN = 3.20   # 0%
  BATT_VOLTAGE_MAX = 4.00   # 100%
  percent = 0                                    if voltage <= 3.20
  percent = 100                                  if voltage >= 4.00
  percent = int(((voltage - 3.20) / (4.00 - 3.20)) * 100.0)   otherwise
  ```
- Serial-read retry pattern (from `hardware_read_battery_voltage()`): up to 3 attempts, 25ms between attempts, discard readings that fail a sanity check (`raw == 0` or `raw > 1023`), hold last-known-good value if all attempts fail.
- Switch debounce: 800ms sustained OFF before acting (`PWRSW_OFF_DEBOUNCE_MS` in `cs-hud_new/src/config.h`).
- Shutdown call: exactly `batocera-es-swissknife --shutdown`, no extra orchestration (it already does emulator-quit-with-save + ES stop + real `shutdown -P -h now`, ignoring any configured suspend mode).
- Real build tree: `/Users/bas/batocera-build-wifi/batocera.linux` (git repo, HEAD detached at `batocera-43.1`, `BR2_EXTERNAL` points at itself). **Never** run `git init`/`git commit` in `/Users/bas/Circuit-Sword Batocera` itself (deliberately not a git repo) — the build tree's own git state is fine to use normally.
- Every new/modified file in the build tree must be re-captured into `/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch` (regenerate via `git diff --submodule=diff -- . ':!buildroot'` from the build tree root — see `batocera-build/scripts/setup-build-tree.sh` for how it's applied) before this plan is considered done, so the work is reproducible exactly like Phase 2's.
- No hardware in CI. Every task's testing step must say explicitly what's verified off-device vs. what still needs the physical device — never claim hardware behavior is confirmed when it isn't.

---

## File Structure

New files, all inside `/Users/bas/batocera-build-wifi/batocera.linux`:

- `package/batocera/utils/circuitsword-battery/Config.in` — new Buildroot package option
- `package/batocera/utils/circuitsword-battery/circuitsword-battery.mk` — Buildroot kernel-module package
- `package/batocera/utils/circuitsword-battery/circuitsword_battery.c` — the kernel module source (ported from `cs_battery.c`)
- `package/batocera/utils/circuitsword-backlight/Config.in` — new Buildroot package option
- `package/batocera/utils/circuitsword-backlight/circuitsword-backlight.mk` — Buildroot kernel-module package
- `package/batocera/utils/circuitsword-backlight/circuitsword_backlight.c` — new kernel module source
- `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` — the daemon (single file; internal structure below)
- Modified: `package/batocera/utils/rpigpioswitch/rpi_gpioswitch.sh` — add `CIRCUITSWORD` to `powerdevices` array + `circuitsword_start`/`circuitsword_stop`/`circuitsword_config` functions + dispatch case
- Modified: `package/batocera/utils/rpigpioswitch/rpigpioswitch.mk` — install `rpi-circuitsword.py`
- Modified: `Config.in` (repo root) — `source` lines for the two new packages
- Modified: `package/batocera/core/batocera-system/Config.in` — `select` lines enabling the two new packages for `BR2_PACKAGE_BATOCERA_TARGET_BCM2837`

`rpi-circuitsword.py` internal structure (one file, matching the size/style of the existing `rpigpioswitch` scripts like `rpi-argonone.py`, ~150-250 lines):
- `arduino.py`-style section: `serial_cmd(cmd, nbytes)` helper with the mutex + retry logic — used by everything else in the file.
- `fan_thread()` — independent of the Arduino, pure GPIO + thermal zone.
- `battery_bridge(stop_event)` — voltage read + conversion + write to `circuitsword-battery` sysfs.
- `backlight_bridge(stop_event)` — poll `circuitsword-backlight` sysfs `brightness`, push changed values to the Arduino; also owns the mode+left/right combo detection.
- `switch_monitor(stop_event)` — GPIO 37 debounce + shutdown call.
- `main()` — config loading, thread startup, signal handling.

New docs file: `/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/PHASE3-HARDWARE-DAEMON-FINDINGS.md` — running findings log, same pattern as `PHASE0-FINDINGS.md`/`WIFI-BUILD-FINDINGS.md`.

---

## Task 1: Findings log + confirm build-tree state

**Files:**
- Create: `docs/superpowers/plans/findings/PHASE3-HARDWARE-DAEMON-FINDINGS.md`

**Interfaces:**
- Produces: the findings-log file every later task appends to.

- [ ] **Step 1: Create the findings log with a header**

```markdown
# Phase 3 Hardware Daemon — Findings Log

Companion to docs/superpowers/specs/2026-08-04-phase3-hardware-daemon-design.md
and docs/superpowers/plans/2026-08-04-phase3-hardware-daemon.md.

Real build tree: /Users/bas/batocera-build-wifi/batocera.linux
Reproducible patches: /Users/bas/Circuit-Sword Batocera/batocera-build/patches/

## Task log
```

- [ ] **Step 2: Confirm the build tree is in the expected clean state before starting**

Run:
```bash
cd /Users/bas/batocera-build-wifi/batocera.linux && git status --short
```
Expected: only files already known-modified from Phase 2 (see
`docs/superpowers/plans/findings/WIFI-BUILD-FINDINGS.md` and
`batocera-build/patches/batocera-linux.patch`) — `board/batocera/broadcom/bcm2837/boot/config.txt`,
`board/batocera/broadcom/bcm2837/linux-defconfig.config`,
`board/batocera/scripts/post-image-script.sh`, `docker.mk`,
`package/batocera/boot/batocera-initramfs/batocera-initramfs.mk`,
`package/batocera/core/batocera-system/Config.in`,
`package/batocera/emulationstation/batocera-emulationstation/controllers/es_input.cfg`,
plus the untracked `fsoverlay` WiFi files and the `xxd`/`qemu` patch files.
If anything else is dirty, stop and ask before proceeding — don't build on
top of an unexpected state.

- [ ] **Step 3: Append a one-line confirmation to the findings log**

```markdown
### Task 1: build tree confirmed clean (only Phase 2's known changes present)
```

No commit step for this task (the findings log lives in the non-git project
directory; nothing to commit yet in the build tree).

---

## Task 2: Port the `circuitsword-battery` kernel module

**Files:**
- Create: `package/batocera/utils/circuitsword-battery/circuitsword_battery.c`
- Create: `package/batocera/utils/circuitsword-battery/Config.in`
- Create: `package/batocera/utils/circuitsword-battery/circuitsword-battery.mk`
- Reference (read, don't modify): `Retropie_source/battery-driver/cs_battery.c`, `package/batocera/network/rtw88/rtw88.mk`, `package/batocera/network/rtw88/Config.in`

**Interfaces:**
- Produces: `/sys/class/power_supply/circuitsword-battery/` with `capacity` and `charging` readable, and `/sys/module/circuitsword_battery/parameters/{capacity,charging}` writable (module params) — Task 5's daemon writes to the latter.

- [ ] **Step 1: Write the kernel module source**

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * circuitsword_battery — minimal virtual battery for the Circuit Sword,
 * ported from Retropie_source/battery-driver/cs_battery.c.
 *
 * Registers /sys/class/power_supply/circuitsword-battery so
 * EmulationStation's native BatteryIconComponent (es-core/src/utils/Platform.cpp,
 * which scans /sys/class/power_supply directly) works with zero ES-side
 * changes. power_supply is a kernel-only class; userspace can't create one
 * directly, so this tiny module exists purely to expose two values that
 * userspace (the rpi-circuitsword.py daemon) writes into it every poll:
 *
 *     echo 47 > /sys/module/circuitsword_battery/parameters/capacity
 *     echo 1  > /sys/module/circuitsword_battery/parameters/charging
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/minmax.h>
#include <linux/power_supply.h>

static int capacity = 50;          /* 0..100, written by rpi-circuitsword.py */
static int charging;               /* 0 = discharging, 1 = charging */
module_param(capacity, int, 0644);
MODULE_PARM_DESC(capacity, "Battery charge level 0-100 (written by rpi-circuitsword.py)");
module_param(charging, int, 0644);
MODULE_PARM_DESC(charging, "1 = charging, 0 = discharging (written by rpi-circuitsword.py)");

static enum power_supply_property circuitsword_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
};

static int circuitsword_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
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
}

static const struct power_supply_desc circuitsword_battery_desc = {
	.name		= "circuitsword-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= circuitsword_battery_props,
	.num_properties	= ARRAY_SIZE(circuitsword_battery_props),
	.get_property	= circuitsword_battery_get_property,
};

static struct power_supply *circuitsword_battery_psy;

static int __init circuitsword_battery_init(void)
{
	struct power_supply_config cfg = {};

	circuitsword_battery_psy = power_supply_register(NULL, &circuitsword_battery_desc, &cfg);
	if (IS_ERR(circuitsword_battery_psy))
		return PTR_ERR(circuitsword_battery_psy);

	pr_info("circuitsword_battery: registered virtual battery for EmulationStation\n");
	return 0;
}

static void __exit circuitsword_battery_exit(void)
{
	power_supply_unregister(circuitsword_battery_psy);
}

module_init(circuitsword_battery_init);
module_exit(circuitsword_battery_exit);

MODULE_AUTHOR("Circuit Sword");
MODULE_DESCRIPTION("Virtual battery feeding EmulationStation's battery icon");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
```

- [ ] **Step 2: Write the Buildroot Config.in (mirrors `package/batocera/network/rtw88/Config.in`)**

```
comment "circuitsword-battery needs a Linux kernel to be built"
	depends on !BR2_LINUX_KERNEL

config BR2_PACKAGE_CIRCUITSWORD_BATTERY
	bool "circuitsword-battery"
	depends on BR2_LINUX_KERNEL
	help
	  Virtual power_supply kernel module feeding EmulationStation's
	  native battery icon from the Circuit-Sword's Arduino Leonardo
	  battery ADC, via the rpi-circuitsword.py daemon.
```

- [ ] **Step 3: Write the Buildroot .mk (mirrors `package/batocera/network/rtw88/rtw88.mk`'s use of the `kernel-module` macro, but this is in-tree source, not a fetched tarball)**

```makefile
################################################################################
#
# circuitsword-battery
#
################################################################################

CIRCUITSWORD_BATTERY_VERSION = 1.0
CIRCUITSWORD_BATTERY_SITE = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-battery
CIRCUITSWORD_BATTERY_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-package))
```

- [ ] **Step 4: Add the `source` line to the top-level Config.in, right after rtw88's own line (`Config.in:135`)**

Read `Config.in` around line 135 first to confirm the exact surrounding
context hasn't shifted, then add immediately after the rtw88 `source` line:
```
    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-battery/Config.in"
```

- [ ] **Step 5: Enable it for our board in `package/batocera/core/batocera-system/Config.in`**

Read the file around line 346 (the existing `select BR2_PACKAGE_RPIGPIOSWITCH if BR2_PACKAGE_BATOCERA_RPI_ANY` line) first, then add nearby:
```
	select BR2_PACKAGE_CIRCUITSWORD_BATTERY	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837
```

- [ ] **Step 6: Verify off-device — the module builds against the actual kernel headers**

This can't run standalone (needs the full Buildroot kernel-module macro's
generated build rule), so defer the actual compile check to Task 9's build.
For now, sanity-check the C source has no obvious syntax errors using the
host compiler in syntax-only mode (won't catch kernel-API issues, but
catches typos before burning a multi-hour build):
```bash
gcc -fsyntax-only -I/usr/include package/batocera/utils/circuitsword-battery/circuitsword_battery.c 2>&1 | head -20
```
Expected: fails to find `<linux/power_supply.h>` (that header only exists
in the kernel source tree, not the host's `/usr/include`) — this is
expected and fine; the goal here is only to catch stray typos/mismatched
braces in the surrounding code, not a full kernel-API check. If it fails
with anything other than missing-header errors, fix those first.

- [ ] **Step 7: Append findings**

```markdown
### Task 2: circuitsword-battery kernel module ported
Near-verbatim port of Retropie_source/battery-driver/cs_battery.c.
Registers /sys/class/power_supply/circuitsword-battery. Not yet build-
verified against the real kernel headers (needs Task 9's full build).
```

---

## Task 3: Write the `circuitsword-backlight` kernel module

**Files:**
- Create: `package/batocera/utils/circuitsword-backlight/circuitsword_backlight.c`
- Create: `package/batocera/utils/circuitsword-backlight/Config.in`
- Create: `package/batocera/utils/circuitsword-backlight/circuitsword-backlight.mk`

**Interfaces:**
- Consumes: none (independent of Task 2).
- Produces: `/sys/class/backlight/circuitsword-backlight/brightness` and
  `/sys/class/backlight/circuitsword-backlight/max_brightness` (standard
  Linux backlight class) — Task 6's daemon polls `brightness` for changes
  and pushes them to the Arduino.

- [ ] **Step 1: Write the kernel module source**

No RetroPie/nixos precedent exists for this (both did brightness through
their own HUD instead of a sysfs class) — this uses the standard Linux
`backlight_device` API. `max_brightness` is fixed at 100 (percent), matching
the Arduino's own `CMD_GET_BL`/`CMD_SET_BL` 0-100 percent protocol directly
— no rescaling needed anywhere in the daemon.

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * circuitsword_backlight — virtual backlight class device for the Circuit
 * Sword. Registers /sys/class/backlight/circuitsword-backlight so
 * Batocera's own batocera-brightness script and EmulationStation's
 * brightness UI (both of which scan /sys/class/backlight directly) work
 * with zero changes on their side.
 *
 * The actual backlight hardware is driven by the on-board Arduino Leonardo
 * over a serial link (CMD_SET_BL), which the kernel has no direct access
 * to. This module only tracks the current value; the rpi-circuitsword.py
 * daemon polls .brightness for changes and forwards them to the Arduino
 * (a sysfs write can't itself notify userspace synchronously, so a
 * userspace poll loop is required either way).
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/backlight.h>

#define CIRCUITSWORD_BACKLIGHT_MAX 100

static int circuitsword_backlight_update_status(struct backlight_device *bd)
{
	/* Nothing to do here: the daemon reads .brightness itself via sysfs
	 * and forwards it to the Arduino on its own poll cycle. This
	 * callback exists only because backlight_ops requires one. */
	return 0;
}

static int circuitsword_backlight_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static const struct backlight_ops circuitsword_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= circuitsword_backlight_update_status,
	.get_brightness	= circuitsword_backlight_get_brightness,
};

static struct backlight_device *circuitsword_backlight_dev;

static int __init circuitsword_backlight_init(void)
{
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = CIRCUITSWORD_BACKLIGHT_MAX;
	props.brightness = CIRCUITSWORD_BACKLIGHT_MAX;

	circuitsword_backlight_dev = backlight_device_register(
		"circuitsword-backlight", NULL, NULL,
		&circuitsword_backlight_ops, &props);
	if (IS_ERR(circuitsword_backlight_dev))
		return PTR_ERR(circuitsword_backlight_dev);

	pr_info("circuitsword_backlight: registered virtual backlight for batocera-brightness/ES\n");
	return 0;
}

static void __exit circuitsword_backlight_exit(void)
{
	backlight_device_unregister(circuitsword_backlight_dev);
}

module_init(circuitsword_backlight_init);
module_exit(circuitsword_backlight_exit);

MODULE_AUTHOR("Circuit Sword");
MODULE_DESCRIPTION("Virtual backlight class device bridging batocera-brightness/ES to the Arduino");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
```

- [ ] **Step 2: Write the Buildroot Config.in**

```
comment "circuitsword-backlight needs a Linux kernel to be built"
	depends on !BR2_LINUX_KERNEL

config BR2_PACKAGE_CIRCUITSWORD_BACKLIGHT
	bool "circuitsword-backlight"
	depends on BR2_LINUX_KERNEL
	help
	  Virtual backlight class device bridging Batocera's native
	  brightness UI (batocera-brightness, EmulationStation settings) to
	  the Circuit-Sword's Arduino Leonardo, via the rpi-circuitsword.py
	  daemon.
```

- [ ] **Step 3: Write the Buildroot .mk**

```makefile
################################################################################
#
# circuitsword-backlight
#
################################################################################

CIRCUITSWORD_BACKLIGHT_VERSION = 1.0
CIRCUITSWORD_BACKLIGHT_SITE = $(BR2_EXTERNAL_BATOCERA_PATH)/package/batocera/utils/circuitsword-backlight
CIRCUITSWORD_BACKLIGHT_SITE_METHOD = local

$(eval $(kernel-module))
$(eval $(generic-package))
```

- [ ] **Step 4: Add the `source` line to the top-level Config.in, next to the one added in Task 2**

```
    source "$BR2_EXTERNAL_BATOCERA_PATH/package/batocera/utils/circuitsword-backlight/Config.in"
```

- [ ] **Step 5: Enable it for our board, next to Task 2's select line in `batocera-system/Config.in`**

```
	select BR2_PACKAGE_CIRCUITSWORD_BACKLIGHT	if BR2_PACKAGE_BATOCERA_TARGET_BCM2837
```

- [ ] **Step 6: Syntax sanity-check (same caveat as Task 2 Step 6 — real verification is Task 9)**

```bash
gcc -fsyntax-only -I/usr/include package/batocera/utils/circuitsword-backlight/circuitsword_backlight.c 2>&1 | head -20
```

- [ ] **Step 7: Append findings**

```markdown
### Task 3: circuitsword-backlight kernel module written (new, no direct precedent)
Standard Linux backlight_device API. max_brightness=100 matches the
Arduino's CMD_GET_BL/CMD_SET_BL percent protocol directly, no rescaling
needed. Not yet build-verified (needs Task 9).
```

---

## Task 4: `rpi-circuitsword.py` — Arduino serial helper + fan thread

**Files:**
- Create: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`
- Reference (read, don't modify): `Retropie_source/cs-hud_new/src/hardware.c` (lines 168-320ish), `Retropie_source/cs-hud_new/src/config.h`, `package/batocera/utils/rpigpioswitch/rpi-argonone.py` (style/structure reference)

**Interfaces:**
- Produces: `serial_cmd(cmd, nbytes)` — module-level function, sends a
  single command byte to `/dev/ttyACM0` and reads back `nbytes` of
  response, protected by a module-level `threading.Lock`. Returns `bytes`
  of length `nbytes` on success, or `None` after exhausting retries.
  Signature and retry contract used by Tasks 5 and 6.
- Produces: `fan_thread(stop_event: threading.Event)` — runs until
  `stop_event` is set.
- Produces: `load_config() -> dict` — reads
  `/userdata/system/configs/circuitsword.conf` (same `key=value` format as
  `argonone.conf`), returns fan thresholds with defaults if the file is
  absent or a key is missing. Used by `fan_thread` now, and by Tasks 5/6
  later for their own config keys.

- [ ] **Step 1: Write the file header, config loader, and serial helper**

```python
#!/usr/bin/env python3
"""
Circuit-Sword hardware daemon.
Fan control (temperature hysteresis) + Arduino Leonardo serial bridge
(battery, backlight, mode-button combo, power switch -> safe shutdown).

Installed via rpigpioswitch (package/batocera/utils/rpigpioswitch/),
launched by /etc/init.d/S92switch same as rpi-argonone.py / rpi-kintaro-
SafeShutdown.py.

Hardware reference: Retropie_source/cs-hud_new/src/{config.h,hardware.c}
(this daemon replicates that board's known-working GPIO pins, serial
protocol, and timing constants -- see docs/superpowers/specs/
2026-08-04-phase3-hardware-daemon-design.md for the full mapping).
"""
import os
import sys
import time
import threading
import serial  # python3-serial, already a Batocera Buildroot package

# ============================================================
# GPIO pins (Retropie_source/cs-hud_new/src/config.h)
# ============================================================
GPIO_PIN_PWRSW = 37       # Power switch (pulled up: ON=HIGH, OFF=LOW)
GPIO_PIN_CHARGING = 36    # Charging indicator, active HIGH
GPIO_PIN_POWER_GOOD = 38  # USB power good, active HIGH
GPIO_PIN_OVERTEMP = 35    # Fan, active LOW (0=on, 1=off)

# ============================================================
# Serial port + command protocol
# ============================================================
SERIAL_PORT = "/dev/ttyACM0"
SERIAL_BAUD = 9600

CMD_GET_VOLT = b'c'    # -> 2 bytes: raw ADC voltage, low byte then high byte
CMD_GET_BL = b'q'      # -> 1 byte: brightness %
CMD_SET_BL = b'Q'      # <- 1 byte: brightness %
CMD_GET_STATUS = b's'  # -> 1 byte: status flags, bit 0 = mode button

_serial_lock = threading.Lock()
_serial_conn = None


def _get_serial():
    global _serial_conn
    if _serial_conn is None or not _serial_conn.is_open:
        _serial_conn = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=0.1)
    return _serial_conn


def serial_cmd(cmd: bytes, nbytes: int, retries: int = 3, retry_delay_s: float = 0.025):
    """Send a single command byte, read back nbytes of response.
    Retries up to `retries` times with `retry_delay_s` between attempts
    (matches Retropie_source/cs-hud_new/src/hardware.c
    hardware_read_battery_voltage()'s retry loop: 3 attempts, 25ms apart).
    Returns bytes of length nbytes on success, None if all attempts fail.
    """
    with _serial_lock:
        for attempt in range(retries):
            try:
                conn = _get_serial()
                conn.reset_input_buffer()
                conn.write(cmd)
                resp = conn.read(nbytes)
                if len(resp) == nbytes:
                    return resp
            except (serial.SerialException, OSError) as e:
                print(f"[rpi-circuitsword] serial error on {cmd!r}: {e}", file=sys.stderr)
            time.sleep(retry_delay_s)
        return None


# ============================================================
# Config file: /userdata/system/configs/circuitsword.conf
# Same key=value convention as argonone.conf.
# ============================================================
CONFIG_FILE = "/userdata/system/configs/circuitsword.conf"

DEFAULT_CONFIG = {
    "fan_on_temp": 58.0,
    "fan_off_temp": 50.0,
    "fan_poll_interval_s": 3,
    "switch_debounce_ms": 800,
}


def load_config() -> dict:
    cfg = dict(DEFAULT_CONFIG)
    try:
        with open(CONFIG_FILE, "r") as fp:
            for line in fp:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                key = key.strip()
                value = value.strip()
                if key in cfg:
                    try:
                        cfg[key] = type(cfg[key])(value)
                    except ValueError:
                        continue
    except FileNotFoundError:
        pass
    return cfg
```

- [ ] **Step 2: Write the fan thread**

```python
# ============================================================
# Fan control: GPIO 35, active-LOW, on/off only (NEVER PWM the fan supply
# -- hard project rule). Hysteresis per config (defaults: on >=58.0C,
# off <50.0C, matching Retropie_source/cs-hud_new/src/config.h
# FAN_ON_TEMP/FAN_OFF_TEMP).
# ============================================================
THERMAL_ZONE = "/sys/class/thermal/thermal_zone0/temp"


def read_cpu_temp_c():
    try:
        with open(THERMAL_ZONE, "r") as f:
            milli = int(f.read().strip())
        c = milli / 1000.0
        if 0 < c <= 160:
            return c
    except (OSError, ValueError):
        pass
    return None


def fan_thread(stop_event: threading.Event):
    import gpiod
    cfg = load_config()
    chip = gpiod.Chip("/dev/gpiochip0")
    line = chip.get_line(GPIO_PIN_OVERTEMP)
    line.request(consumer="circuitsword-fan", type=gpiod.LINE_REQ_DIR_OUT)

    fan_on = False
    line.set_value(1)  # start OFF (active-LOW: 1=off)

    while not stop_event.is_set():
        temp = read_cpu_temp_c()
        if temp is not None:
            if not fan_on and temp >= cfg["fan_on_temp"]:
                fan_on = True
                line.set_value(0)  # active-LOW: 0=on
                print(f"[rpi-circuitsword] fan ON at {temp:.1f}C (threshold {cfg['fan_on_temp']}C)", file=sys.stderr)
            elif fan_on and temp < cfg["fan_off_temp"]:
                fan_on = False
                line.set_value(1)
                print(f"[rpi-circuitsword] fan OFF at {temp:.1f}C (threshold {cfg['fan_off_temp']}C)", file=sys.stderr)
        stop_event.wait(cfg["fan_poll_interval_s"])

    line.set_value(1)  # leave fan off on exit
    line.release()
```

- [ ] **Step 3: Verify off-device — syntax and import check**

```bash
python3 -m py_compile package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
echo "exit: $?"
```
Expected: `exit: 0`. This only checks syntax — `gpiod`/`serial` aren't
installed on the host doing this check, so `import` errors for those two
are expected if you additionally try to actually run the file (don't; the
compile check above doesn't execute imports).

- [ ] **Step 4: Append findings**

```markdown
### Task 4: rpi-circuitsword.py started -- serial helper + fan thread
serial_cmd() retry contract: 3 attempts, 25ms apart, matching hardware.c.
Fan thread: GPIO 35 active-LOW via gpiod, hysteresis from config.h defaults,
overridable via /userdata/system/configs/circuitsword.conf. py_compile
clean. Not yet runnable end-to-end (needs Tasks 5/6's main()).
```

---

## Task 5: `rpi-circuitsword.py` — battery bridge

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` (append)

**Interfaces:**
- Consumes: `serial_cmd()` from Task 4 (signature: `serial_cmd(cmd: bytes, nbytes: int, retries=3, retry_delay_s=0.025) -> bytes | None`).
- Produces: `battery_bridge(stop_event: threading.Event)` — runs until `stop_event` is set. Used by `main()` in Task 7.

- [ ] **Step 1: Append the voltage/percent conversion + GPIO charging read + bridge loop**

```python
# ============================================================
# Battery: CMD_GET_VOLT ('c') -> 2-byte raw ADC -> voltage -> percent.
# Charging/power-good are direct GPIO reads (GPIO 36 / 38, active HIGH),
# NOT over the serial link -- see hardware_read_charging()/
# hardware_read_power_good() in Retropie_source/cs-hud_new/src/hardware.c.
# Constants from Retropie_source/cs-hud_new/src/config.h.
# ============================================================
BATT_VOLTSCALE = 203.5
BATT_DACRES = 33.0
BATT_DACMAX = 1023.0
BATT_RESDIVMUL = 4.0
BATT_RESDIVVAL = 1000.0
BATT_VOLTAGE_MIN = 3.20   # 0%
BATT_VOLTAGE_MAX = 4.00   # 100%

BATTERY_SYSFS_DIR = "/sys/module/circuitsword_battery/parameters"
BATTERY_POLL_INTERVAL_S = 30  # matches BATT_POLL_INTERVAL_S in config.h


def raw_adc_to_voltage(raw: int) -> float:
    # NOTE: the trailing / 100.0 matches hardware.c's
    # hardware_read_battery_voltage(), which ends with `return v / 100.0;`
    # -- easy to miss when skimming the function, but required (without it,
    # a raw of 512 computes to ~417V instead of ~4.17V).
    v = (raw * BATT_VOLTSCALE * BATT_DACRES + BATT_DACMAX * 5.0) / (
        (BATT_DACRES * BATT_RESDIVVAL) / BATT_RESDIVMUL
    )
    return v / 100.0


def voltage_to_percent(voltage: float) -> int:
    if voltage <= BATT_VOLTAGE_MIN:
        return 0
    if voltage >= BATT_VOLTAGE_MAX:
        return 100
    return int(((voltage - BATT_VOLTAGE_MIN) / (BATT_VOLTAGE_MAX - BATT_VOLTAGE_MIN)) * 100.0)


def read_battery_percent(last_known: int) -> int:
    """CMD_GET_VOLT returns 2 bytes: low byte then high byte of the raw
    ADC value. serial_cmd() already retries internally; here we only
    sanity-check the decoded value (0 < raw <= 1023, matching hardware.c's
    check) and fall back to last_known otherwise."""
    resp = serial_cmd(CMD_GET_VOLT, 2)
    if resp is None:
        return last_known
    raw = resp[0] | (resp[1] << 8)
    if not (0 < raw <= 1023):
        return last_known
    return voltage_to_percent(raw_adc_to_voltage(raw))


def read_charging(gpio_line) -> bool:
    try:
        return gpio_line.get_value() != 0  # active HIGH
    except OSError:
        return False


def write_battery_sysfs(percent: int, charging: bool):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/capacity", "w") as f:
            f.write(str(percent))
        with open(f"{BATTERY_SYSFS_DIR}/charging", "w") as f:
            f.write("1" if charging else "0")
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)


def battery_bridge(stop_event: threading.Event):
    import gpiod
    chip = gpiod.Chip("/dev/gpiochip0")
    charging_line = chip.get_line(GPIO_PIN_CHARGING)
    charging_line.request(consumer="circuitsword-charging", type=gpiod.LINE_REQ_DIR_IN)

    last_percent = 50  # matches the kernel module's own initial default
    last_logged_percent = None
    while not stop_event.is_set():
        last_percent = read_battery_percent(last_percent)
        charging = read_charging(charging_line)
        write_battery_sysfs(last_percent, charging)
        # Log every poll (every BATTERY_POLL_INTERVAL_S, not spammy) so
        # `logread | grep rpi-circuitsword` shows the battery is actually
        # being read and pushed, not just on change.
        if last_percent != last_logged_percent:
            print(f"[rpi-circuitsword] battery {last_percent}% charging={charging}", file=sys.stderr)
            last_logged_percent = last_percent
        stop_event.wait(BATTERY_POLL_INTERVAL_S)

    charging_line.release()
```

- [ ] **Step 2: Verify off-device**

```bash
python3 -m py_compile package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
echo "exit: $?"
```

- [ ] **Step 3: Verify the conversion formula against a known reference point by hand**

The design doc / RetroPie source don't give a worked example, so sanity-check
the formula doesn't blow up or invert at the range edges (this is a logic
check, not a hardware test — actual accuracy needs the real ADC on-device,
flagged in Task 9):
```bash
python3 -c "
import sys
sys.path.insert(0, 'package/batocera/utils/rpigpioswitch')
# Import guard: the file imports 'serial' and 'gpiod' at module scope for
# other functions, so exec only the two pure-math functions instead of a
# full import if those packages aren't installed on this host.
BATT_VOLTSCALE=203.5; BATT_DACRES=33.0; BATT_DACMAX=1023.0
BATT_RESDIVMUL=4.0; BATT_RESDIVVAL=1000.0
BATT_VOLTAGE_MIN=3.20; BATT_VOLTAGE_MAX=4.00
def raw_adc_to_voltage(raw):
    v = (raw*BATT_VOLTSCALE*BATT_DACRES + BATT_DACMAX*5.0) / ((BATT_DACRES*BATT_RESDIVVAL)/BATT_RESDIVMUL)
    return v / 100.0
def voltage_to_percent(v):
    if v <= BATT_VOLTAGE_MIN: return 0
    if v >= BATT_VOLTAGE_MAX: return 100
    return int(((v-BATT_VOLTAGE_MIN)/(BATT_VOLTAGE_MAX-BATT_VOLTAGE_MIN))*100.0)
for raw in (1, 512, 1023):
    v = raw_adc_to_voltage(raw)
    print(f'raw={raw} -> voltage={v:.3f}V -> percent={voltage_to_percent(v)}%')
"
```
Expected: monotonically increasing voltage/percent as `raw` increases, no
exceptions. Record the printed values in the findings log (Step 4) so a
real on-device reading can later be sanity-checked against this same
formula run by hand.

- [ ] **Step 4: Append findings (include the Step 3 output)**

```markdown
### Task 5: battery bridge added
CMD_GET_VOLT decode + BATT_* formula ported verbatim from hardware.c/
config.h. Charging read via direct GPIO 36 (active HIGH), NOT serial --
corrects an earlier design-doc simplification that implied charging came
over the Arduino link. Formula sanity-check output:
<paste the raw/voltage/percent lines from Step 3 here>
Real ADC accuracy not yet verified -- needs Task 9 on-device.
```

---

## Task 6: `rpi-circuitsword.py` — backlight bridge, mode+left/right combo, switch monitor

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` (append)

**Interfaces:**
- Consumes: `serial_cmd()` from Task 4.
- Produces: `backlight_bridge(stop_event: threading.Event)` and `switch_monitor(stop_event: threading.Event)` — both used by `main()` in Task 7.

- [ ] **Step 1: Append the backlight bridge + mode-button combo**

```python
# ============================================================
# Backlight: two write paths into the same Arduino command (CMD_SET_BL),
# both funneled through serial_cmd()'s lock so they can't race:
#   1. Batocera's native brightness UI writes
#      /sys/class/backlight/circuitsword-backlight/brightness -- this
#      thread polls that file for changes and pushes them to the Arduino.
#   2. A physical mode+left/right button combo (CMD_GET_STATUS bit 0 =
#      mode button, combined with directional input) adjusts brightness
#      directly, "blind" (no on-screen feedback) -- matches how
#      Retropie/nixos did it via their own HUD, just without the HUD.
# mode+up/down (volume) is explicitly deferred, see design doc.
# ============================================================
BACKLIGHT_SYSFS_BRIGHTNESS = "/sys/class/backlight/circuitsword-backlight/brightness"
BACKLIGHT_POLL_INTERVAL_S = 1
BRIGHTNESS_STEP = 10  # % per button-combo press


def read_backlight_sysfs() -> int:
    try:
        with open(BACKLIGHT_SYSFS_BRIGHTNESS, "r") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return -1


def write_backlight_sysfs(percent: int):
    try:
        with open(BACKLIGHT_SYSFS_BRIGHTNESS, "w") as f:
            f.write(str(percent))
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-backlight module not loaded? {e}", file=sys.stderr)


def set_arduino_backlight(percent: int):
    percent = max(0, min(100, percent))
    serial_cmd(CMD_SET_BL + bytes([percent]), 0)


def read_mode_button() -> bool:
    resp = serial_cmd(CMD_GET_STATUS, 1)
    if resp is None:
        return False
    return bool(resp[0] & 0x01)


def backlight_bridge(stop_event: threading.Event):
    last_sysfs_value = read_backlight_sysfs()
    current = last_sysfs_value if last_sysfs_value >= 0 else 50

    while not stop_event.is_set():
        sysfs_value = read_backlight_sysfs()
        if sysfs_value >= 0 and sysfs_value != last_sysfs_value:
            # Native Batocera UI changed it -- push to the Arduino.
            current = sysfs_value
            set_arduino_backlight(current)
            print(f"[rpi-circuitsword] backlight -> {current}% (from batocera-brightness/ES)", file=sys.stderr)
            last_sysfs_value = sysfs_value

        # mode+left/right combo: only act on the mode button's rising
        # edge combined with a directional read to avoid repeat-firing
        # every poll while both are held.
        # NOTE: directional (left/right) button state is read via the
        # same GPIO button-combo mechanism as the physical controller
        # buttons -- wiring that read is controller-hardware-specific
        # and out of scope for this bridge; this function assumes a
        # `read_direction_pressed()` helper exists once the controller
        # GPIO/evdev read is wired up in Task 8's on-device pass. Until
        # then this loop only handles the sysfs->Arduino direction.

        stop_event.wait(BACKLIGHT_POLL_INTERVAL_S)
```

- [ ] **Step 2: Append the switch monitor**

```python
# ============================================================
# Power switch: GPIO 37, pulled up (ON=HIGH idle, OFF=LOW when flipped).
# UNCONFIRMED on real hardware -- flip the polarity check below if this
# board reads inverted (see design doc "Open items").
# 800ms sustained OFF before acting (PWRSW_OFF_DEBOUNCE_MS in config.h).
# ============================================================
SWITCH_POLL_INTERVAL_S = 0.05  # 50ms, matches POLL_INTERVAL_MS in config.h


def switch_monitor(stop_event: threading.Event):
    import gpiod
    cfg = load_config()
    debounce_s = cfg["switch_debounce_ms"] / 1000.0

    chip = gpiod.Chip("/dev/gpiochip0")
    line = chip.get_line(GPIO_PIN_PWRSW)
    line.request(consumer="circuitsword-switch", type=gpiod.LINE_REQ_DIR_IN)

    off_since = None
    shutdown_armed = True

    while not stop_event.is_set():
        is_on = line.get_value() != 0  # true = ON (pulled-up idle HIGH)
        if is_on:
            off_since = None
            shutdown_armed = True
        else:
            if off_since is None:
                off_since = time.monotonic()
            elif shutdown_armed and (time.monotonic() - off_since) >= debounce_s:
                shutdown_armed = False  # one-shot guard, matches cs-hud's
                print("[rpi-circuitsword] power switch OFF (debounced) -- shutting down", file=sys.stderr)
                os.system("/usr/bin/batocera-es-swissknife --shutdown")
        stop_event.wait(SWITCH_POLL_INTERVAL_S)

    line.release()
```

- [ ] **Step 3: Verify off-device**

```bash
python3 -m py_compile package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
echo "exit: $?"
```

- [ ] **Step 4: Append findings**

```markdown
### Task 6: backlight bridge + switch monitor added
Backlight: sysfs->Arduino direction implemented and testable in principle;
Arduino->sysfs direction (mode+left/right combo) stubbed with a comment --
reading the directional button state needs the controller's own GPIO/evdev
wiring, which is genuinely hardware-specific and deferred to Task 8's
on-device pass rather than guessed at here. Switch monitor: 800ms debounce,
one-shot guard (matches cs-hud's shutdown_armed pattern), calls
`batocera-es-swissknife --shutdown` directly, no fallback. Polarity
(ON=HIGH) unconfirmed on real hardware -- carried over as an assumption,
same as Retropie_source/FUTURE.md flags it.
```

---

## Task 7: `rpi-circuitsword.py` — `main()` and thread wiring

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi-circuitsword.py` (append)

**Interfaces:**
- Consumes: `fan_thread`, `battery_bridge`, `backlight_bridge`, `switch_monitor` (all `Callable[[threading.Event], None]`, from Tasks 4-6).
- Produces: CLI entry point — `python3 rpi-circuitsword.py start|stop`, matching the calling convention `rpi-kintaro-SafeShutdown.py` uses (see Task 8).

- [ ] **Step 1: Append `main()`**

```python
# ============================================================
# Entry point. Calling convention matches
# package/batocera/utils/rpigpioswitch/rpi-kintaro-SafeShutdown.py: no
# subcommand needed here since this script is meant to be launched once
# and killed by pid (see Task 8's circuitsword_start/circuitsword_stop
# wrapper functions in rpi_gpioswitch.sh) -- but accept `start` as a
# no-op-compatible argument so it can also be invoked the same way as
# rpi-argonone.py for consistency/debugging.
# ============================================================
def main():
    stop_event = threading.Event()

    def handle_signal(signum, frame):
        print(f"[rpi-circuitsword] received signal {signum}, stopping", file=sys.stderr)
        stop_event.set()

    import signal
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    threads = [
        threading.Thread(target=fan_thread, args=(stop_event,), name="fan", daemon=True),
        threading.Thread(target=battery_bridge, args=(stop_event,), name="battery", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
    ]
    for t in threads:
        t.start()
    cfg = load_config()
    print(f"[rpi-circuitsword] all threads started, config={cfg}", file=sys.stderr)

    while not stop_event.is_set():
        stop_event.wait(1)

    for t in threads:
        t.join(timeout=5)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Final off-device verification of the complete file**

```bash
python3 -m py_compile package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
echo "compile: $?"
python3 -c "
import ast
with open('package/batocera/utils/rpigpioswitch/rpi-circuitsword.py') as f:
    tree = ast.parse(f.read())
names = {n.name for n in ast.walk(tree) if isinstance(n, ast.FunctionDef)}
required = {'serial_cmd', 'load_config', 'fan_thread', 'battery_bridge',
            'backlight_bridge', 'switch_monitor', 'main'}
missing = required - names
print('missing functions:', missing if missing else 'none')
"
```
Expected: `compile: 0` and `missing functions: none`.

- [ ] **Step 3: Make it executable (it's installed with mode 0755 in Task 8)**

```bash
chmod +x package/batocera/utils/rpigpioswitch/rpi-circuitsword.py
```

- [ ] **Step 4: Append findings**

```markdown
### Task 7: main() wired, full daemon file complete
4 daemon threads (fan, battery, backlight, switch), SIGTERM/SIGINT handled
for clean shutdown. py_compile + function-presence check both pass. File
is executable. Still entirely unverified on real hardware (Task 9).
```

---

## Task 8: Register the `CIRCUITSWORD` profile in `rpigpioswitch`

**Files:**
- Modify: `package/batocera/utils/rpigpioswitch/rpi_gpioswitch.sh`
- Modify: `package/batocera/utils/rpigpioswitch/rpigpioswitch.mk`

**Interfaces:**
- Consumes: `rpi-circuitsword.py` from Task 7 (installed as `/usr/bin/rpi-circuitsword` on-device, matching how `rpi-kintaro-SafeShutdown.py` becomes `/usr/bin/rpi-kintaro-SafeShutdown`).

- [ ] **Step 1: Add `CIRCUITSWORD` to the `powerdevices` array**

Read `rpi_gpioswitch.sh` lines 38-63 first to confirm the array's exact
current end (it may have shifted since this plan was written), then add a
new line right after the `KINTARO` entry (both are simple switch+fan
combos, keep them adjacent):
```bash
              KINTARO "SNES style case from SuperKuma aka ROSHAMBO" \
              CIRCUITSWORD "Circuit-Sword handheld (fan, battery, backlight, safe shutdown)" \
```

- [ ] **Step 2: Add the wrapper functions, mirroring `kintaro_start`/`kintaro_stop`/`kintaro_config`
  (lines 465-485), but routing the daemon's output through `logger` —
  matching `batocera-shutdown`'s own
  `exec 1> >(tee >(logger -t batocera-shutdown -s)) 2>&1` convention (the
  only logging pattern already established anywhere in this codebase; no
  other `rpigpioswitch` script logs anything at all, which is exactly why
  this needs to be added deliberately here rather than left implicit).
  Confirmed busybox's `syslogd`/`logread` are enabled in this build
  (`buildroot/package/busybox/busybox.config`), so `logread | grep
  rpi-circuitsword` becomes the on-device way to see every battery
  reading, fan on/off transition, backlight change, and switch-triggered
  shutdown the daemon acts on.**

**Correction found during Task 8's review**: a plain trailing pipe
(`cmd | logger &`) backgrounds the whole *pipeline*, so `$!` captures
`logger`'s PID, not the daemon's — `circuitsword_stop()` would then kill
`logger` and leave the actual daemon running orphaned. Use process
substitution instead (matching `batocera-shutdown`'s own
`exec 1> >(tee >(logger ...)) 2>&1` convention), which keeps `$!` as the
daemon's own PID:

```bash
#Circuit-Sword handheld
function circuitsword_start()
{
    rpi-circuitsword > >(logger -t rpi-circuitsword -s) 2>&1 &
    pid=$!
    echo "$pid" > "/tmp/rpi-circuitsword.pid"
    wait "$pid"
}

function circuitsword_stop()
{
    pid_file="/tmp/rpi-circuitsword.pid"
    if [[ -e $pid_file ]]; then
        kill $(cat $pid_file)
    fi
}

function circuitsword_config()
{
    true
}
```

- [ ] **Step 3: Add the dispatch case, next to the existing `"KINTARO")` entry (around line 1038)**

```bash
    "CIRCUITSWORD")
        circuitsword_$1
    ;;
```

- [ ] **Step 4: Install the script in `rpigpioswitch.mk`**

Add to `RPIGPIOSWITCH_INSTALL_TARGET_CMDS`, next to the other `rpi-*.py` installs:
```makefile
	$(INSTALL) -D -m 0755 $(RPIGPIOSWITCH_SRC)/rpi-circuitsword.py \
	    $(TARGET_DIR)/usr/bin/rpi-circuitsword
```

- [ ] **Step 5: Verify off-device — shellcheck the modified script**

```bash
shellcheck package/batocera/utils/rpigpioswitch/rpi_gpioswitch.sh 2>&1 | grep -i "circuitsword" || echo "no circuitsword-specific shellcheck findings"
```
(The full script likely has pre-existing shellcheck warnings unrelated to
this change — only look for anything flagging the new `CIRCUITSWORD`
lines specifically.)

- [ ] **Step 6: Append findings**

```markdown
### Task 8: CIRCUITSWORD registered in rpigpioswitch
Added to powerdevices array, circuitsword_start/stop/config functions
(mirrors kintaro_* exactly), dispatch case, and rpigpioswitch.mk install
rule. This makes CIRCUITSWORD selectable via
`batocera-settings-set system.power.switch CIRCUITSWORD` on-device (or via
the rpi_gpioswitch dialog menu) -- not yet tested end-to-end (Task 9).
```

---

## Task 9: Regenerate reproducible patches, build, and flash

**Files:**
- Modify: `/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch`

**Interfaces:**
- Consumes: every file created/modified in Tasks 2, 3, 6, 8 (all live in the outer `batocera.linux` repo, not the `buildroot` submodule, so they're captured by `batocera-linux.patch` alone — no `buildroot.patch` changes expected).

- [ ] **Step 1: Regenerate the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add -A -- package/batocera/utils/circuitsword-battery package/batocera/utils/circuitsword-backlight
git diff --submodule=diff -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
git status --short
```
Expected `git status --short` output includes: `A` for the two new
package directories, `M` for `Config.in`,
`package/batocera/core/batocera-system/Config.in`,
`package/batocera/utils/rpigpioswitch/rpi_gpioswitch.sh`,
`package/batocera/utils/rpigpioswitch/rpigpioswitch.mk`, and `A` for
`package/batocera/utils/rpigpioswitch/rpi-circuitsword.py`.

(The `git add -A` is scoped to only the two new directories precisely so
this step can't accidentally stage unrelated files if something else in
the tree happens to be dirty — the plain `git diff` for everything else
already tracks modifications without needing `add`.)

- [ ] **Step 2: Re-verify the patch applies cleanly to a fresh checkout (cheap — no build)**

```bash
cd "/Users/bas/Circuit-Sword Batocera/batocera-build/scripts"
./setup-build-tree.sh
```
Expected: completes without `git apply` errors, final `git status --short`
in its output matches Step 1's list (plus Phase 2's already-known files).
This is the same fast check used at the end of Phase 2 — a few minutes,
no compilation.

- [ ] **Step 3: Force a kernel rebuild (new kernel modules need this — a plain image rebuild alone won't pick up brand-new out-of-tree packages any more reliably than Phase 2's es_input.cfg surprise did)**

```bash
"/Users/bas/Circuit-Sword Batocera/batocera-build/scripts/build-kernel.sh"
```
This logs to `docs/superpowers/plans/findings/wifi-build.log` (the shared build log
across phases) and runs in the background — tail it or use the Monitor
tool to watch for completion, expecting it to take a meaningful amount of
time (new kernel modules compiling against the full kernel tree, plus the
existing image repackage step). Do not assume success from a fast/quiet
run — Phase 2 already taught this lesson once (a suspiciously-fast rebuild
turned out to have skipped the actual install step). Explicitly verify:
```bash
grep -c "Error [0-9]" "/Users/bas/Circuit-Sword Batocera/docs/superpowers/plans/findings/wifi-build.log"
find /Volumes/BatoceraBuild/output/bcm2837/target/lib/modules -iname "circuitsword_battery.ko" -o -iname "circuitsword_backlight.ko"
```
Expected: `0` errors, and both `.ko` files found in the target's modules
directory — this is the precise, non-comment-matching check Phase 2's
findings log specifically recommends (grep for the actual artifact, not
text that could also appear in a comment).

- [ ] **Step 4: Flash and do the on-device pass — this is where the genuinely-unverifiable-off-device items get checked**

Flash the freshly-built image (Raspberry Pi Imager, "Use custom", re-browse
to the file explicitly rather than picking from a recent list — Phase 2's
findings log flags this as a real gotcha). Then, over SSH:

```bash
# Module loaded and sysfs present
lsmod | grep -i circuitsword
ls /sys/class/power_supply/circuitsword-battery/
ls /sys/class/backlight/circuitsword-backlight/

# Select the CIRCUITSWORD power-switch profile
batocera-settings-set system.power.switch CIRCUITSWORD
/etc/init.d/S92switch restart

# Confirm the daemon is actually running
pgrep -f rpi-circuitsword

# Confirm logging is actually working (Task 8's logger -t rpi-circuitsword)
logread | grep rpi-circuitsword | tail -20
```
Expected in the `logread` output: a startup line with the loaded config,
periodic battery-percent lines, and (once the physical checks below are
done) fan on/off and backlight-change lines. If nothing shows up here, the
`logger` piping in Task 8 isn't working — fix that before trusting any of
the other on-device checks below, since they all depend on being able to
see what the daemon is actually doing.

Then, physically on the device (not verifiable any other way — say so,
don't claim these pass without doing them):
- Battery percentage in ES's own battery icon looks plausible (compare
  against a rough manual voltage check if possible).
- Backlight: change it via ES's brightness setting, confirm the physical
  screen actually dims/brightens.
- Fan: this needs the CPU to actually reach 58°C, which may require
  `stress`/a game running for a while — note in the findings log whether
  this was actually exercised or only code-reviewed.
- Power switch: flip it OFF, confirm the device shuts down cleanly (not a
  hard power cut) within a few seconds of the 800ms debounce.
- Confirm the fan/switch GPIO polarity assumptions (active-LOW fan, ON=HIGH
  switch) — flip the polarity in code if either reads inverted on this
  actual board, and note which one in the findings log.

- [ ] **Step 5: Append final findings**

```markdown
### Task 9: build + flash + on-device validation
Patch regenerated and re-verified via setup-build-tree.sh (clean apply).
Kernel rebuild: <PASS/FAIL, error count, .ko presence confirmed>.
On-device: <fill in exactly what was and wasn't actually exercised --
battery accuracy, backlight response, fan trigger (was 58C actually
reached?), switch shutdown, and whether either polarity assumption needed
flipping>. Anything not physically tested is listed here as still open,
not silently assumed to work.
```

---

## Self-Review

**Spec coverage:** fan (Task 4), battery bridge + kernel module (Tasks 2, 5), backlight bridge + kernel module (Tasks 3, 6), switch/shutdown (Task 6), rpigpioswitch integration (Task 8), reproducibility + build + on-device validation (Task 9). Mode+up/down volume and the graphical HUD are explicitly out of scope per the design doc — no task needed for either. Every design-doc component has a task.

**Placeholder scan:** no TBD/TODO; the one intentionally-incomplete piece (directional button read for the mode+left/right combo in Task 6 Step 1) is explained, not hand-waved — it's flagged as genuinely hardware-specific and deferred to Task 9's on-device pass with a code comment explaining exactly why, not a bare "add later."

**Type consistency:** `serial_cmd(cmd: bytes, nbytes: int, retries=3, retry_delay_s=0.025)` defined in Task 4 is used with that exact signature in Tasks 5 and 6. `load_config()` defined in Task 4, consumed by `fan_thread` (Task 4) and `switch_monitor` (Task 6) with the same key names (`fan_on_temp`, `fan_off_temp`, `fan_poll_interval_s`, `switch_debounce_ms`). `stop_event: threading.Event` is the consistent shape across all four thread functions, matching `main()`'s usage in Task 7.
