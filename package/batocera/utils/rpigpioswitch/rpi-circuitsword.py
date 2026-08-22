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
import socket
import subprocess
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
CMD_GET_VOL = b'e'     # -> 1 byte: board's internal volume %, 0-100
CMD_SET_VOL = b'E'     # <- 1 byte: volume % to store on the board

_serial_lock = threading.Lock()
_serial_conn = None


def _get_serial():
    """timeout=0.3, not 0.1: root-caused on real hardware (see
    joystick-calibration/daemon findings) -- commands that trigger an
    EEPROM write on the firmware (e.g. the joystick invert/enable
    toggles) can take longer than 100ms to reply. serial_cmd()'s retry
    loop re-sends the command byte on ANY read timeout, since it cannot
    distinguish "the write never arrived" from "the write arrived but
    the reply is just slow" -- so a too-tight timeout was silently
    double-executing these commands on the firmware (confirmed via a
    live SERIAL_DEBUG trace: every invert command logged as two
    back-to-back sends, first empty-reply/retry, second successful),
    with the resulting extra unread reply byte(s) corrupting whatever
    command read next. 0.3s comfortably covers the slowest observed
    round trip without materially slowing the common (fast) case."""
    global _serial_conn
    if _serial_conn is None or not _serial_conn.is_open:
        _serial_conn = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=0.3)
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
    "fan_enabled": 1,
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

    # Defense in depth: a malformed/hand-edited/pathological config file
    # (or a buggy writer -- e.g. a UI that writes before it has ever
    # successfully loaded real values) must never be able to push a value
    # out of a sane range into fan_thread()/switch_monitor(). This is the
    # durable guard: it applies regardless of what wrote the file, not
    # just the quickmenu UI. fan_poll_interval_s=0 turns the fan-control
    # loop into a tight busy-loop (stop_event.wait(0) returns immediately,
    # also raising the temperature it's meant to manage); switch_debounce_ms
    # near 0 makes ordinary switch bounce look "debounced" and can trigger
    # a spurious shutdown.
    if cfg["fan_poll_interval_s"] < 1:
        print(f"[rpi-circuitsword] config: fan_poll_interval_s={cfg['fan_poll_interval_s']} "
              f"out of range, clamping to default {DEFAULT_CONFIG['fan_poll_interval_s']}",
              file=sys.stderr)
        cfg["fan_poll_interval_s"] = DEFAULT_CONFIG["fan_poll_interval_s"]
    if cfg["switch_debounce_ms"] < 50:
        print(f"[rpi-circuitsword] config: switch_debounce_ms={cfg['switch_debounce_ms']} "
              f"out of range, clamping to default {DEFAULT_CONFIG['switch_debounce_ms']}",
              file=sys.stderr)
        cfg["switch_debounce_ms"] = DEFAULT_CONFIG["switch_debounce_ms"]

    if cfg["fan_enabled"] not in (0, 1):
        print(f"[rpi-circuitsword] config: fan_enabled={cfg['fan_enabled']} "
              f"out of range, clamping to default {DEFAULT_CONFIG['fan_enabled']}",
              file=sys.stderr)
        cfg["fan_enabled"] = DEFAULT_CONFIG["fan_enabled"]

    return cfg


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


# Hard-coded, NOT read from config -- the whole point of a safety ceiling
# is that it can't be configured away by the same toggle that disables
# the fan. Independent 5C hysteresis margin from the user-configurable
# thresholds, so it can't chatter once triggered. Set below the BCM2837's
# thermal throttle point (~80C soft throttle, ~85C hard limit) so the
# ceiling actually engages before performance degrades, not at the same
# point it starts.
FAN_SAFETY_ON_C = 70.0
FAN_SAFETY_OFF_C = 65.0


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
                    if cfg["fan_enabled"]:
                        threshold = cfg["fan_on_temp"] if fan_on else cfg["fan_off_temp"]
                    else:
                        threshold = FAN_SAFETY_ON_C if fan_on else FAN_SAFETY_OFF_C
                    print(f"[rpi-circuitsword] fan {label} at {temp:.1f}C "
                          f"threshold {threshold:.1f}C (fan_enabled={cfg['fan_enabled']})",
                          file=sys.stderr)
            else:
                # Sensor unreadable. If fan_enabled=0, the normal hysteresis
                # loop above never runs, so nothing would otherwise notice
                # or react -- silently voiding the safety ceiling. Fail safe:
                # force the fan on and log every tick until the sensor
                # recovers. (fan_enabled=1 keeps its pre-existing do-nothing
                # behavior on a bad read -- out of scope here.)
                if not cfg["fan_enabled"]:
                    if not fan_on:
                        fan_on = True
                        _set_fan_on(fan_on)
                        request.set_value(GPIO_PIN_OVERTEMP, Value.INACTIVE)  # active-LOW: 0=on
                    print("[rpi-circuitsword] fan ON: thermal sensor unreadable "
                          "(read_cpu_temp_c() returned None) and fan_enabled=0 "
                          "-- forcing fan on as fail-safe", file=sys.stderr)
            stop_event.wait(cfg["fan_poll_interval_s"])
    finally:
        request.set_value(GPIO_PIN_OVERTEMP, Value.ACTIVE)  # leave fan off on exit
        request.release()


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


def write_online_sysfs(online: bool):
    try:
        with open(f"{BATTERY_SYSFS_DIR}/online", "w") as f:
            f.write("1" if online else "0")
    except OSError as e:
        print(f"[rpi-circuitsword] circuitsword-battery module not loaded? {e}", file=sys.stderr)


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


CHARGING_EDGE_TIMEOUT_S = 1.0   # wait_edge_events() timeout, keeps stop_event checkable
CHARGING_FALLBACK_POLL_S = 1.0  # plain-poll interval if edge-detection is unavailable


def charging_thread(stop_event: threading.Event):
    import gpiod
    from gpiod.line import Direction, Edge, Value

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
    except Exception as e:
        # Broad except: an old python3-gpiod binding could raise
        # TypeError/ImportError on LineSettings(edge_detection=...) or the
        # `from gpiod.line import Edge` import itself, not just OSError --
        # any of these should degrade to plain polling, not crash the thread.
        print(f"[rpi-circuitsword] charging edge-detect unavailable, "
              f"falling back to 1s polling: {e}", file=sys.stderr)
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


# ============================================================
# Backlight: Batocera's native brightness UI writes
# /sys/class/backlight/circuitsword-backlight/brightness -- this thread
# polls that file for changes and pushes them to the Arduino via
# CMD_SET_BL. Direct button-driven brightness (and volume) adjustment
# happens through the quickmenu overlay instead -- see
# package/batocera/utils/circuitsword-quickmenu/quickmenu.c -- not here.
# ============================================================
BACKLIGHT_SYSFS_BRIGHTNESS = "/sys/class/backlight/circuitsword-backlight/brightness"
BACKLIGHT_POLL_INTERVAL_S = 1


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
    """Reads back the firmware's 2-byte "OK" reply (nbytes=2, not 0) --
    the firmware always replies to 'Q', and a 0-byte read here used to
    leave that reply sitting unconsumed in the serial receive buffer,
    where it could bleed into and corrupt a LATER, unrelated command's
    response (e.g. a joystick STATUS/toggle) if that read raced the
    late-arriving stray bytes. Confirmed as the root cause of a real
    on-device bug: a single joystick invert toggle appearing to flip
    unrelated bits, traced to this leak via a raw-serial vs. daemon-socket
    comparison (see the joystick-calibration/daemon findings for detail)."""
    percent = max(0, min(100, percent))
    serial_cmd(CMD_SET_BL + bytes([percent]), 2)


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

        stop_event.wait(BACKLIGHT_POLL_INTERVAL_S)


# ============================================================
# Volume: the Arduino firmware's own MODE+Up/Down hardware combo adjusts
# a volume value it keeps internally (mirrors the already-working
# MODE+Left/Right brightness combo, which stays entirely in Arduino
# hardware and needs no bridge). Volume is different: audio output is a
# software/ALSA path the Arduino cannot drive directly, so this bridge
# polls both sides and mirrors whichever changed into the other -- same
# shape as backlight_bridge() above, but bidirectional, because volume
# (unlike backlight) can legitimately change from either side: the
# hardware combo, or software UI such as circuitsword-quickmenu's own
# volume row.
# ============================================================
VOLUME_POLL_INTERVAL_S = 1


def volume_bridge_tick(last_board, last_system, board_now, system_now):
    """Decide what volume_bridge() should do this poll, given the
    previously-seen values and freshly-read current values.

    board_now / system_now are None when a read failed (treated as "no
    change this tick", matching read_backlight_sysfs()'s -1-on-failure ->
    "skip" convention in backlight_bridge()).

    Returns (new_last_board, new_last_system, action), where action is one
    of:
      None                    -- nothing changed, do nothing
      ("apply_to_alsa", pct)  -- board changed, push pct to batocera-audio
      ("apply_to_board", pct) -- system changed, push pct to the Arduino
    If both changed in the same tick, the board (hardware combo) wins --
    the physical button is the more recent, more intentional action; the
    system-side value is treated as already-stale and gets overwritten by
    the same "apply_to_alsa" outcome, avoiding a torn/ambiguous state.
    """
    board_changed = board_now is not None and board_now != last_board
    system_changed = system_now is not None and system_now != last_system

    if board_changed:
        return (board_now, board_now, ("apply_to_alsa", board_now))
    if system_changed:
        return (last_board, system_now, ("apply_to_board", system_now))
    return (last_board, last_system, None)


def read_board_volume() -> int:
    resp = serial_cmd(CMD_GET_VOL, 1)
    if resp is None:
        return -1
    v = resp[0]
    return v if 0 <= v <= 100 else -1


def read_system_volume() -> int:
    try:
        out = subprocess.run(
            ["/usr/bin/batocera-audio", "getSystemVolume"],
            capture_output=True, text=True, timeout=1,
        )
        return int(out.stdout.strip())
    except (subprocess.SubprocessError, ValueError, OSError):
        return -1


def set_arduino_volume(percent: int):
    """Reads back the firmware's 2-byte "OK" reply -- see
    set_arduino_backlight()'s comment for why nbytes must not be 0."""
    percent = max(0, min(100, percent))
    serial_cmd(CMD_SET_VOL + bytes([percent]), 2)


def set_system_volume(percent: int):
    """batocera-audio occasionally takes longer than a short timeout to
    return (observed on real hardware); a bare subprocess.run() raising
    here would kill the volume thread permanently (daemon threads don't
    auto-restart on an uncaught exception). Both the raised timeout and
    the try/except keep this a non-fatal, log-and-continue failure."""
    try:
        subprocess.run(
            ["/usr/bin/batocera-audio", "setSystemVolume", str(percent)],
            capture_output=True, timeout=3,
        )
    except (subprocess.SubprocessError, OSError) as e:
        print(f"[rpi-circuitsword] set_system_volume({percent}) failed: {e}", file=sys.stderr)


def volume_bridge(stop_event: threading.Event):
    last_board = read_board_volume()
    last_system = read_system_volume()
    if last_board < 0:
        last_board = 50
    if last_system < 0:
        last_system = 50

    while not stop_event.is_set():
        board_now = read_board_volume()
        system_now = read_system_volume()
        board_arg = board_now if board_now >= 0 else None
        system_arg = system_now if system_now >= 0 else None

        last_board, last_system, action = volume_bridge_tick(
            last_board, last_system, board_arg, system_arg
        )
        if action is not None:
            kind, pct = action
            if kind == "apply_to_alsa":
                set_system_volume(pct)
                print(f"[rpi-circuitsword] volume -> {pct}% (from MODE+Up/Down combo)", file=sys.stderr)
            else:
                set_arduino_volume(pct)
                print(f"[rpi-circuitsword] volume -> {pct}% (from batocera-audio/quickmenu, synced to board)", file=sys.stderr)

        stop_event.wait(VOLUME_POLL_INTERVAL_S)


# ============================================================
# Power switch: GPIO 37, pulled up (ON=HIGH idle, OFF=LOW when flipped).
# UNCONFIRMED on real hardware -- flip the polarity check below if this
# board reads inverted (see design doc "Open items").
# 800ms sustained OFF before acting (PWRSW_OFF_DEBOUNCE_MS in config.h).
# ============================================================
SWITCH_POLL_INTERVAL_S = 0.05  # 50ms, matches POLL_INTERVAL_MS in config.h


def switch_monitor(stop_event: threading.Event):
    import gpiod
    from gpiod.line import Direction, Value

    request = gpiod.request_lines(
        "/dev/gpiochip0",
        consumer="circuitsword-switch",
        config={GPIO_PIN_PWRSW: gpiod.LineSettings(direction=Direction.INPUT)},
    )

    off_since = None
    shutdown_armed = True

    while not stop_event.is_set():
        cfg = get_current_config()
        debounce_s = cfg["switch_debounce_ms"] / 1000.0
        is_on = request.get_value(GPIO_PIN_PWRSW) == Value.ACTIVE  # true = ON (pulled-up idle HIGH)
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

    request.release()


# ============================================================
# In-game quick menu (Phase 4).
#
# The MODE button is owned end-to-end here, because this daemon already
# polls CMD_GET_STATUS over serial regardless of what is on screen.
#
# Open : confirm RetroArch is alive -> confirm its network command port
#        answers -> PAUSE_TOGGLE -> launch circuitsword-quickmenu and
#        wait. The menu draws itself as a Wayland overlay-layer surface
#        on top of the paused game; there is NO display hand-off and no
#        VT switch, so there is no blank-screen failure mode.
# Close: quickmenu exits (B, SIGTERM from a second MODE press, crash, or
#        the 5s watchdog kill) -> PAUSE_TOGGLE.
#
# Fail-safe in one direction: we never pause without first confirming we
# can talk to RetroArch, and we always resume afterwards.
# See docs/superpowers/specs/2026-08-06-quickmenu-design.md.
# ============================================================
QUICKMENU_BIN = "/usr/bin/circuitsword-quickmenu"
MODE_DEBOUNCE_S = 0.08          # ~80ms: plain pushbutton, NOT the 800ms
                                # mechanical power switch. Starting
                                # estimate, needs on-device tuning.
MODE_TAP_MAX_S = 0.35           # Tap-vs-hold cutoff. MODE also drives the
                                # Arduino firmware's own MODE+Up/Down
                                # (volume) and MODE+Left/Right (brightness)
                                # combos entirely in hardware -- opening
                                # the quick menu on every MODE press would
                                # pause the game for the whole hold,
                                # silently eating whatever combo the user
                                # was doing. Only a plain tap (released
                                # within this window) opens the menu; a
                                # longer hold is left alone for the
                                # firmware combo and never opens anything.
QUICKMENU_POLL_INTERVAL_S = 0.05
QUICKMENU_WATCHDOG_S = 5        # same 5s convention as main()'s t.join(timeout=5)

# This daemon is started from init with no session environment, but the
# menu is a Wayland client. These are exactly what
# package/batocera/emulationstation/batocera-emulationstation/wayland/
# labwc/04-labwc.sh exports for the ES session.
QUICKMENU_ENV = {
    "WAYLAND_DISPLAY": "wayland-0",
    "XDG_RUNTIME_DIR": "/var/run",
}

RETROARCH_CMD_HOST = "127.0.0.1"
RETROARCH_CMD_PORT = 55355      # RetroArch default; enabled for this image
                                # in configgen's libretroRetroarchCustom.py
                                # (network_cmd_enable/network_cmd_port).


class ModeButton:
    """Edge-detecting debouncer for the Arduino MODE button.

    Feed it raw CMD_GET_STATUS samples via update(); it returns True
    exactly once per physical press, after the level has been stable for
    debounce_s. Pure logic, no I/O -- unit-tested in
    tests/test_quickmenu_logic.py.
    """

    def __init__(self, debounce_s: float = MODE_DEBOUNCE_S):
        self.debounce_s = debounce_s
        self._stable = False
        self._candidate = False
        self._since = None

    def reset(self):
        """Forget the current press. Called after a menu session so a
        still-held MODE button cannot immediately re-open the menu."""
        self._stable = True
        self._candidate = True
        self._since = None

    def update(self, raw: bool, now: float) -> bool:
        raw = bool(raw)
        if raw != self._candidate:
            self._candidate = raw
            self._since = now
            return False
        if self._since is None:
            return False
        if (now - self._since) < self.debounce_s:
            return False
        if self._candidate != self._stable:
            self._stable = self._candidate
            self._since = None
            return self._stable   # rising edge only
        return False


def retroarch_running() -> bool:
    """True if a retroarch process exists. Scans /proc directly: busybox
    is the only ps/grep on this image and pgrep is not installed, so
    nothing external is depended on here."""
    try:
        entries = os.listdir("/proc")
    except OSError:
        return False
    for entry in entries:
        if not entry.isdigit():
            continue
        try:
            with open(f"/proc/{entry}/comm", "r") as f:
                if f.read().strip().startswith("retroarch"):
                    return True
        except OSError:
            continue
    return False


def retroarch_cmd_query(cmd: bytes, timeout_s: float = 0.5):
    """Send a RetroArch network command and wait for its reply.
    Returns the reply bytes, or None if the port did not answer.
    Used with b"GET_STATUS" as a reachability probe *before* pausing --
    plain UDP sends cannot themselves report an unreachable port."""
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout_s)
        sock.connect((RETROARCH_CMD_HOST, RETROARCH_CMD_PORT))
        sock.send(cmd)
        return sock.recv(1024)
    except (OSError, socket.timeout) as e:
        print(f"[rpi-circuitsword] retroarch cmd {cmd!r} no reply: {e}", file=sys.stderr)
        return None
    finally:
        if sock is not None:
            sock.close()


def send_pause_toggle() -> bool:
    """Fire-and-forget PAUSE_TOGGLE. Reachability must already have been
    established with retroarch_cmd_query(b"GET_STATUS")."""
    sock = None
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.5)
        sock.connect((RETROARCH_CMD_HOST, RETROARCH_CMD_PORT))
        sock.send(b"PAUSE_TOGGLE")
        return True
    except OSError as e:
        print(f"[rpi-circuitsword] PAUSE_TOGGLE failed: {e}", file=sys.stderr)
        return False
    finally:
        if sock is not None:
            sock.close()


def run_quickmenu_session():
    """One full open/close cycle. Blocks until the menu is closed. The
    overlay always launches -- from ES with no game running, or in-game
    -- regardless of whether RetroArch answers. Only pause/resume is
    conditional: RetroArch is paused on open only if its command port
    answers at entry, and resumed on close only if this session actually
    paused it. Never returns with a game left paused that this session
    itself paused."""
    # Reachability BEFORE pausing. If RetroArch's command port does not
    # answer -- no game running (e.g. MODE pressed from ES), or the port
    # is silent for some other reason -- skip pausing, but still open
    # the menu: there is nothing to pause, not a reason to refuse to open.
    paused_this_session = False
    if retroarch_cmd_query(b"GET_STATUS") is not None:
        if send_pause_toggle():
            paused_this_session = True
        else:
            print("[rpi-circuitsword] quickmenu: PAUSE_TOGGLE failed, opening unpaused",
                  file=sys.stderr)

    proc = None
    try:
        env = dict(os.environ)
        env.update(QUICKMENU_ENV)
        proc = subprocess.Popen([QUICKMENU_BIN], env=env)
    except OSError as e:
        print(f"[rpi-circuitsword] quickmenu: launch failed: {e}", file=sys.stderr)

    if proc is not None:
        button = ModeButton()
        # The MODE press that triggered this very session may still be
        # physically held down right now. Without this reset, this fresh
        # ModeButton() starts at _candidate=False and sees that still-held
        # True as a brand new rising edge ~debounce_s later -- misreading
        # the SAME press that opened the menu as the "press again to
        # close" signal, closing the menu almost immediately after it
        # opened. reset() seeds the debouncer as "already stable at
        # whatever MODE is doing right now", so only an actual
        # release-then-press-again counts as the close signal.
        button.reset()
        closing_since = None
        while True:
            rc = proc.poll()
            if rc is not None:
                if rc != 0:
                    print(f"[rpi-circuitsword] quickmenu exited rc={rc}", file=sys.stderr)
                break
            # A second MODE press is the close signal: SIGTERM the menu.
            if button.update(read_mode_button(), time.monotonic()) and closing_since is None:
                closing_since = time.monotonic()
                print("[rpi-circuitsword] quickmenu: MODE again -> closing", file=sys.stderr)
                proc.terminate()
            # Watchdog: 5s after a close signal, kill it regardless.
            if closing_since is not None and \
                    (time.monotonic() - closing_since) >= QUICKMENU_WATCHDOG_S:
                print("[rpi-circuitsword] quickmenu: watchdog expired, killing",
                      file=sys.stderr)
                proc.kill()
                try:
                    proc.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    pass
                break
            time.sleep(QUICKMENU_POLL_INTERVAL_S)

    # Resume only if this session actually paused. There is no display to
    # hand back either way: labwc composited RetroArch's surface (if any)
    # the entire time, so a paused game is already visible again the
    # moment the overlay surface is destroyed.
    if paused_this_session:
        send_pause_toggle()


def quickmenu_thread(stop_event: threading.Event):
    button = ModeButton()
    busy_lock = threading.Lock()
    busy = False

    while not stop_event.is_set():
        pressed = button.update(read_mode_button(), time.monotonic())
        if pressed:
            # Debounced press detected -- don't open yet. Watch how long
            # MODE stays held to tell a quick tap (open the menu) from a
            # hold for the firmware's own volume/brightness combo (leave
            # it alone, never open).
            press_time = time.monotonic()
            is_tap = True
            while read_mode_button() and not stop_event.is_set():
                if time.monotonic() - press_time >= MODE_TAP_MAX_S:
                    is_tap = False
                    break
                stop_event.wait(0.02)

            if not is_tap:
                print("[rpi-circuitsword] quickmenu: MODE held past tap "
                      "window -- treating as combo, not opening",
                      file=sys.stderr)
                # Wait out the rest of the hold so its eventual release
                # isn't mistaken for a fresh press next loop iteration.
                while read_mode_button() and not stop_event.is_set():
                    stop_event.wait(0.02)
                button.reset()
                stop_event.wait(QUICKMENU_POLL_INTERVAL_S)
                continue

            with busy_lock:
                already = busy
                if not already:
                    busy = True
            if already:
                # MODE arrived mid-transition: ignore, so an open can't
                # race a close.
                print("[rpi-circuitsword] quickmenu: MODE ignored (busy)",
                      file=sys.stderr)
            else:
                try:
                    run_quickmenu_session()
                finally:
                    # Swallow a still-held MODE so the menu can't
                    # immediately re-open on the same physical press.
                    button.reset()
                    with busy_lock:
                        busy = False
        stop_event.wait(QUICKMENU_POLL_INTERVAL_S)


# ============================================================
# Persistent in-game status bar (Phase 4 follow-up).
#
# Unlike the quick menu, this is passive: it never pauses RetroArch and
# never grabs input. statusbar_tick() is the single decision this thread
# makes on every poll -- start/stop/keep circuitsword-statusbar based on
# whether a game is currently running -- factored out as its own function
# (mirroring how run_quickmenu_session() is the testable unit for the
# quickmenu thread) so it is directly unit-testable without a real
# process or a real Wayland compositor. See
# docs/superpowers/specs/2026-08-10-persistent-statusbar-design.md.
# ============================================================
STATUSBAR_BIN = "/usr/bin/circuitsword-statusbar"
STATUSBAR_POLL_INTERVAL_S = 1


def statusbar_tick(proc):
    """One decision cycle. `proc` is the currently tracked Popen object, or
    None. Returns the (possibly new) tracked Popen object, or None if
    nothing should be tracked anymore."""
    running = retroarch_running()
    alive = proc is not None and proc.poll() is None

    if running and not alive:
        try:
            env = dict(os.environ)
            env.update(QUICKMENU_ENV)
            proc = subprocess.Popen([STATUSBAR_BIN], env=env)
            print("[rpi-circuitsword] statusbar: started", file=sys.stderr)
        except OSError as e:
            print(f"[rpi-circuitsword] statusbar: launch failed: {e}",
                  file=sys.stderr)
            proc = None
    elif not running and alive:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=1)
        print("[rpi-circuitsword] statusbar: stopped", file=sys.stderr)
        proc = None

    return proc


def statusbar_thread(stop_event: threading.Event):
    proc = None
    while not stop_event.is_set():
        proc = statusbar_tick(proc)
        stop_event.wait(STATUSBAR_POLL_INTERVAL_S)

    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


# ============================================================
# Joystick calibration + invert/enable toggles (Phase 6).
#
# circuitsword-quickmenu never opens /dev/ttyACM0 directly -- this
# daemon remains the sole serial owner. It exposes joystick actions to
# the menu over a small Unix-domain-socket protocol instead.
#
# Firmware protocol (Retropie_source/kite-arduino/CS_FIRMWARE/):
#   J -> calibrateJoystick(), BLOCKS ~10s (CALIBTIME), then "OK"
#   ( ) [ ] -> invert Joy1 X / Joy1 Y / Joy2 X / Joy2 Y, each "OK"
#   { } -> toggle Joy1 enabled / Joy2 enabled, each "OK"
#   j -> 1-byte status: bit0=iscalib1, bit1=iscalib2, bit2=xinvert1,
#        bit3=yinvert1, bit4=xinvert2, bit5=yinvert2
# ============================================================
JOYSTICK_SOCK_PATH = "/var/run/circuitsword-joystick.sock"
JOYSTICK_CALIBRATE_TIMEOUT_S = 12.0  # CALIBTIME (10s) + margin
JOYSTICK_CONN_TIMEOUT_S = 15.0  # generous per-connection timeout covering CALIBRATE round trip
CMD_JOY_CALIBRATE = b'J'
CMD_JOY_INVERT_J1X = b'('
CMD_JOY_INVERT_J1Y = b')'
CMD_JOY_INVERT_J2X = b'['
CMD_JOY_INVERT_J2Y = b']'
CMD_JOY_TOGGLE_J1 = b'{'
CMD_JOY_TOGGLE_J2 = b'}'
CMD_JOY_STATUS = b'j'


def joystick_calibrate() -> bool:
    """Sends 'J' and waits up to JOYSTICK_CALIBRATE_TIMEOUT_S for the
    literal 2-byte "OK" reply. Does NOT use serial_cmd()'s retry/short-
    timeout logic -- that's wrong for a command that blocks the Arduino
    for ~10s: retrying would either give up too early or re-send J while
    a calibration is already running. Holds _serial_lock for the whole
    call, same lock every other serial user shares -- acknowledged
    tradeoff (see design doc), not a bug."""
    with _serial_lock:
        try:
            conn = _get_serial()
            conn.reset_input_buffer()
            conn.write(CMD_JOY_CALIBRATE)
            conn.timeout = JOYSTICK_CALIBRATE_TIMEOUT_S
            try:
                resp = conn.read(2)
            finally:
                conn.timeout = 0.1  # always restore, even on exception
            return resp == b"OK"
        except (serial.SerialException, OSError) as e:
            print(f"[rpi-circuitsword] joystick calibrate serial error: {e}", file=sys.stderr)
            return False


def joystick_toggle(cmd: bytes) -> bool:
    """Thin wrapper around the existing serial_cmd() helper, which
    already handles retry/timeout correctly for instant (non-blocking)
    commands."""
    resp = serial_cmd(cmd, 2)
    return resp == b"OK"


def joystick_status() -> str:
    """Reads the firmware's 1-byte joystick config and decodes its low
    6 bits (bit0=iscalib1 .. bit5=yinvert2) into a 6-character "0"/"1"
    string in that same order. Returns "000000" on read failure."""
    resp = serial_cmd(CMD_JOY_STATUS, 1)
    if resp is None:
        return "000000"
    byte = resp[0]
    return "".join("1" if (byte >> bit) & 1 else "0" for bit in range(6))


JOYSTICK_COMMANDS = {
    "CALIBRATE": lambda: joystick_calibrate(),
    "INVERT_J1X": lambda: joystick_toggle(CMD_JOY_INVERT_J1X),
    "INVERT_J1Y": lambda: joystick_toggle(CMD_JOY_INVERT_J1Y),
    "INVERT_J2X": lambda: joystick_toggle(CMD_JOY_INVERT_J2X),
    "INVERT_J2Y": lambda: joystick_toggle(CMD_JOY_INVERT_J2Y),
    "TOGGLE_J1": lambda: joystick_toggle(CMD_JOY_TOGGLE_J1),
    "TOGGLE_J2": lambda: joystick_toggle(CMD_JOY_TOGGLE_J2),
}


def joystick_ipc_thread(stop_event: threading.Event):
    try:
        os.remove(JOYSTICK_SOCK_PATH)
    except OSError:
        pass  # didn't exist, or a real permission problem -- bind() below will surface that

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        srv.bind(JOYSTICK_SOCK_PATH)
    except OSError as e:
        print(f"[rpi-circuitsword] joystick socket bind failed, joystick "
              f"menu will be unavailable: {e}", file=sys.stderr)
        return
    srv.listen(1)
    srv.settimeout(1.0)  # keeps stop_event checkable, matches every other thread's shutdown shape

    while not stop_event.is_set():
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        try:
            with conn:
                conn.settimeout(JOYSTICK_CONN_TIMEOUT_S)
                data = b""
                while not data.endswith(b"\n"):
                    chunk = conn.recv(64)
                    if not chunk:
                        break
                    data += chunk
                command = data.decode("ascii", errors="replace").strip()

                if command == "STATUS":
                    reply = joystick_status()
                elif command == "GET_CONFIG":
                    cfg = get_current_config()
                    fan_on = 1 if get_fan_on() else 0
                    reply = (f"fan_on_temp={cfg['fan_on_temp']},"
                             f"fan_off_temp={cfg['fan_off_temp']},"
                             f"fan_poll_interval_s={cfg['fan_poll_interval_s']},"
                             f"switch_debounce_ms={cfg['switch_debounce_ms']},"
                             f"fan_on={fan_on},"
                             f"fan_enabled={cfg['fan_enabled']}")
                elif command == "RELOAD_CONFIG":
                    reload_config()
                    reply = "OK"
                elif command in JOYSTICK_COMMANDS:
                    ok = JOYSTICK_COMMANDS[command]()
                    reply = "OK" if ok else "ERR failed"
                else:
                    reply = "ERR unknown command"

                conn.sendall((reply + "\n").encode("ascii"))
        except OSError as e:
            print(f"[rpi-circuitsword] joystick connection error: {e}", file=sys.stderr)

    srv.close()
    try:
        os.remove(JOYSTICK_SOCK_PATH)
    except OSError:
        pass


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
        threading.Thread(target=charging_thread, args=(stop_event,), name="charging", daemon=True),
        threading.Thread(target=backlight_bridge, args=(stop_event,), name="backlight", daemon=True),
        threading.Thread(target=volume_bridge, args=(stop_event,), name="volume", daemon=True),
        threading.Thread(target=switch_monitor, args=(stop_event,), name="switch", daemon=True),
        threading.Thread(target=quickmenu_thread, args=(stop_event,), name="quickmenu", daemon=True),
        threading.Thread(target=statusbar_thread, args=(stop_event,), name="statusbar", daemon=True),
        threading.Thread(target=joystick_ipc_thread, args=(stop_event,), name="joystick", daemon=True),
    ]
    for t in threads:
        t.start()
    cfg = get_current_config()
    print(f"[rpi-circuitsword] all threads started, config={cfg}", file=sys.stderr)

    while not stop_event.is_set():
        stop_event.wait(1)

    for t in threads:
        t.join(timeout=5)


if __name__ == "__main__":
    main()
