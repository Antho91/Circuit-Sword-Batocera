# AC/Battery Detection Fix — Design

Fixes `batocera-battery-checker` never detecting AC power, so the CPU stays
in battery-power mode permanently — even while charging.

## Context

`batocera-battery-checker`'s `is_power_connected()` runs:

```bash
cat /sys/class/power_supply/*/online 2>/dev/null | grep -E "^1"
```

`circuitsword_battery`'s kernel module (the virtual `power_supply` device
that feeds EmulationStation's battery icon) only exposes `PRESENT`,
`STATUS`, `CAPACITY`, `TECHNOLOGY`, and `SCOPE` — no `online` property. The
glob above therefore never matches anything, `is_power_connected()` always
returns 0, and `batocera-power-mode` is called with `battery` every single
time, regardless of whether the charger is plugged in. This affects CPU
governor/EPP selection (`highperformance`/`balanced`/`powersaver`), not
battery-percentage reporting, which is unaffected and already correct.

`GPIO_PIN_POWER_GOOD = 38` ("USB power good, active HIGH") already exists
as an unused constant in `rpi-circuitsword.py` — the natural, already-wired
GPIO for this fix. GPIO 36 (`GPIO_PIN_CHARGING`) is already read the same
way, in `charging_thread()`, to drive the module's existing `charging`
parameter.

## Behavior

- `circuitsword_battery.c` gains a new `online` module parameter (int,
  default `0`, written by the daemon — same pattern as the existing
  `charging` parameter), added to `circuitsword_battery_props[]` as
  `POWER_SUPPLY_PROP_ONLINE`, handled in
  `circuitsword_battery_get_property()`.
- `charging_thread()` in `rpi-circuitsword.py` is extended to watch *both*
  GPIO 36 (charging) and GPIO 38 (power-good) in the same
  `gpiod.request_lines()` call, with edge detection on both lines. A new
  `write_online_sysfs()` function (mirrors `write_charging_sysfs()`) writes
  to `/sys/module/circuitsword_battery/parameters/online` whenever GPIO 38
  changes state. The existing plain-polling fallback path (for older
  `python3-gpiod` bindings without edge-detection support) is extended
  symmetrically to also poll GPIO 38.
- Initial state for both GPIO 36 and 38 is seeded immediately on thread
  start (matching the existing charging-seed behavior), not left at the
  parameter's stale default until the first edge fires.
- `batocera-battery-checker` and `batocera-power-mode` are **not
  modified** — once `online` reports a real value, their existing logic
  works correctly with zero changes on their side.

## No UI changes

This is a background-only fix. No new visible element, no quickmenu row,
no statusbar icon.

## Testing

- **Off-device**: kernel module compiles for the target kernel.
- **On-device (cannot be verified off-device — flag, don't claim)**:
  confirm GPIO 38 truly reflects "USB power good" as documented; confirm
  `online` sysfs value changes correctly when the charger is plugged/
  unplugged; confirm `batocera-power-mode` is actually invoked with `ac`/
  `battery` and switches the CPU governor accordingly.

## Out of scope

- Any UI surfacing AC/battery status — not requested.
- Changes to `batocera-battery-checker` or `batocera-power-mode` — not
  needed once `online` is populated correctly.
