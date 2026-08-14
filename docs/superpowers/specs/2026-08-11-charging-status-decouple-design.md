# Charging-Status Decoupling — Design

First sub-project of Phase 6 (hardware-daemon refinements). Makes the
charging LED/icon state (battery/WiFi/volume/brightness status bar,
`power_supply` sysfs) update immediately when the charger is plugged or
unplugged, instead of waiting up to 30 seconds for the next battery
voltage poll.

## Context

`rpigpioswitch/rpi-circuitsword.py`'s `battery_bridge()` thread currently
reads both battery percentage (`CMD_GET_VOLT` over the Arduino serial
link — a round trip, hence the 30s `BATTERY_POLL_INTERVAL_S` interval) and
charging state (`GPIO_PIN_CHARGING`, GPIO 36, a direct active-HIGH digital
read — no serial round trip needed) in the same loop tick, then writes
both to `/sys/module/circuitsword_battery/parameters/{capacity,charging}`
together via `write_battery_sysfs()`. This means charging state is only
as fresh as the slow voltage poll, even though the GPIO read itself is
effectively instant — matching the gap already flagged in the master
design doc's Phase 6 scoping (`docs/superpowers/specs/2026-07-28-batocera-port-design.md`).

Confirmed by reading the current daemon source: no existing thread in
this file uses libgpiod's edge-detection API — every GPIO-driven thread
(fan, power switch, this one) polls on a timer today. This project
introduces the first edge-driven thread.

## Architecture

```
main() registers charging_thread(stop_event) as an 8th daemon thread,
alongside fan/battery/backlight/volume/switch/quickmenu/statusbar.

charging_thread:
  gpiod.request_lines(GPIO_PIN_CHARGING, edge_detection=Edge.BOTH)
    |
    v
  loop: request.wait_edge_events(timeout=1.0s)   # timeout keeps stop_event checkable
    |                                              # matches every other thread's
    | event fires (or first iteration, to seed     # stop_event.wait()-based shutdown
    | initial state)                                pattern
    v
  read current line value -> write_charging_sysfs(charging)  (new, writes
                                                    ONLY the `charging`
                                                    sysfs attribute)

battery_bridge (modified): drops GPIO_PIN_CHARGING entirely -- no gpiod
request for it, no read_charging() call. Keeps its existing 30s serial
voltage poll, now writes ONLY `capacity` via a trimmed
write_battery_sysfs() (renamed/reduced to write just that one attribute,
or kept as a percent-only helper -- exact naming decided at implementation).
```

Two threads, two independent sysfs attribute writers, no shared state
between them — `capacity` and `charging` are different files under
`/sys/module/circuitsword_battery/parameters/`, so there's no race to
guard against.

## Components

### `rpi-circuitsword.py` (modified)

- **New**: `charging_thread(stop_event)` — owns `GPIO_PIN_CHARGING`
  exclusively via `gpiod.request_lines(..., config={GPIO_PIN_CHARGING:
  gpiod.LineSettings(direction=Direction.INPUT, edge_detection=Edge.BOTH)})`.
  Loop: `request.wait_edge_events(timeout=1.0)` (a real gpiod v2 API,
  returns after either an edge or the timeout elapses); on a true return
  (or on the very first loop iteration, unconditionally, to establish
  initial state before any real edge has fired) read the line's current
  value and write it via the new `write_charging_sysfs()`. Loop condition
  checks `stop_event.is_set()`, same shutdown shape as every other thread
  here.
- **New**: `write_charging_sysfs(charging: bool)` — writes only
  `.../charging`, mirrors the existing try/except OSError-and-log pattern
  in the current `write_battery_sysfs()`.
- **Modified**: `battery_bridge(stop_event)` — removes its
  `gpiod.request_lines(...)` call for `GPIO_PIN_CHARGING` and its
  `read_charging(request)` call entirely. Keeps `read_battery_percent()`
  and its 30s poll loop unchanged. Writes only `capacity` now (via a
  percent-only sysfs writer — either trim the existing
  `write_battery_sysfs()` to take just `percent`, or split it; the
  charging half moves to the new function above).
- **Removed**: `read_charging(request)` — folded into `charging_thread`'s
  own body (it's a two-line GPIO value read; a standalone helper isn't
  worth keeping once only one caller remains).
- **Modified**: `main()` — adds `threading.Thread(target=charging_thread,
  args=(stop_event,), name="charging", daemon=True)` to the existing
  thread list.

## Data Flow

Unchanged from today except which thread produces `charging`: consumers
(circuitsword-battery kernel module's `power_supply` class, read by both
`circuitsword-statusbar` and `circuitsword-quickmenu` via the existing
battery-icon logic) still just read
`/sys/module/circuitsword_battery/parameters/charging` — they don't know
or care which daemon thread last wrote it. The only visible change is
latency: an edge event fires within ~1s of the physical plug/unplug
(bounded by the `wait_edge_events` timeout, not by any polling interval),
instead of up to 30s.

## Error Handling

If `edge_detection=Edge.BOTH` fails at request time (e.g. kernel/gpiochip
doesn't support edge events on this line, or the GPIO is somehow already
claimed), `charging_thread` catches the exception, logs it once
(`"[rpi-circuitsword] charging edge-detect unavailable, falling back to
1s polling: {e}"`), and falls back to a plain `while not
stop_event.is_set(): write_charging_sysfs(read GPIO value);
stop_event.wait(1.0)` loop in the same thread — same function, same
sysfs writer, just a different wait strategy. The daemon does not crash
or leave charging state unmanaged either way.

## Testing

**Off-device**: none of this is host-testable — it needs a real
`gpiod`/kernel GPIO chip. Verification is a Buildroot rebuild
(`PKG=rpigpioswitch-reinstall`, plain-copy Python package, per CLAUDE.md
Hard Rule #7) confirming the daemon still starts and the thread list
includes `charging`.

**On-device (not yet done)**: SSH in, `logread | grep rpi-circuitsword`
while plugging/unplugging the charger, confirm the log line appears
within ~1s and `/sys/module/circuitsword_battery/parameters/charging`
flips promptly; confirm the status bar's charging icon (already built,
Phase 4) updates at the same speed; confirm battery percentage still
updates normally on its own 30s cadence, unaffected by this change.

## Out of Scope (this sub-project)

- Low-battery warning / auto-shutdown behavior — next Phase 6 sub-project,
  not touched here.
- Any change to how `capacity` is read or its 30s cadence — untouched.
- Settings surface for daemon tunables, joystick calibration — separate
  Phase 6 sub-projects.
