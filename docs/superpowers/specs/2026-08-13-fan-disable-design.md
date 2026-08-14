# Fan Disable Toggle — Design

Adds a way to fully disable the built-in fan, with a hard-coded safety
ceiling that overrides the disable if the CPU gets dangerously hot.

## Context

`fan_thread()` in `rpi-circuitsword.py` currently runs an always-on
temperature hysteresis loop (`fan_on_temp`/`fan_off_temp`, both
user-configurable via the Daemon Settings submenu). There's no way to
turn the fan off entirely — only to raise its thresholds so high it
rarely triggers. This does not conflict with CLAUDE.md's Hard Rule #1
("never PWM the fan") — that rule is about not doing variable-speed
control on this 2-wire on/off-only blower; a full disable switch is
still strictly on/off.

Because the CM3 sits in a sealed shell with the fan as its only active
cooling, a disable switch is a real thermal-safety feature, not a
cosmetic one. The design includes a hard safety ceiling so a forgotten
"disabled" setting can't cook the board.

## Behavior

- New config key `fan_enabled` (bool, default `1`/on), same file
  (`/userdata/system/circuitsword.conf`) and load/reload/range-clamp
  machinery as the four existing tunables.
- When `fan_enabled` is true: unchanged, existing `fan_on_temp`/
  `fan_off_temp` hysteresis exactly as today.
- When `fan_enabled` is false: the normal hysteresis is skipped (fan
  never turns on via `fan_on_temp`/`fan_off_temp`) **except** a
  hard-coded safety ceiling: if CPU temp reaches 70.0°C, the fan is
  forced on regardless of the disable, and turns back off once temp
  drops to 65.0°C (its own fixed 5°C hysteresis margin, independent of
  the user-configurable thresholds, so it can't be configured away).
  70°C is comfortably below the CM3's thermal throttle point (soft
  throttle begins around 80°C, hard limit ~85°C).
- The safety ceiling is unconditional — it is not itself configurable,
  by design (a hard-coded escape hatch is the point).

## UI

- New row in the Daemon Settings submenu, placed first (above "Fan ON
  temp" / "Fan OFF temp", the two settings it gates): "Fan enabled",
  rendered with the same toggle-switch icon used for WiFi/joystick
  rows.
- The two temperature rows stay visible and editable regardless of
  `fan_enabled`'s state — no conditional hiding. Simpler, and editing
  them while disabled is harmless (they just aren't consulted for
  hysteresis while disabled, per the Behavior section above).
- The read-only status row's live fan-on/off indicator is unaffected —
  it already just reflects `_set_fan_on()`'s current output, which will
  correctly show ON during a safety-ceiling override.

## Data flow

Follows the exact existing pattern for the other four tunables:
`GET_CONFIG`'s reply gains `fan_enabled=<0|1>`; the C client's
`qm_daemon_config_get()` parses one more field; pressing A on the new
row sends a toggle write through the same `circuitsword.conf`
whole-file rewrite `qm_daemon_config_write()` already does, then
`RELOAD_CONFIG` as today.

## Testing

- Host-testable: the safety-ceiling hysteresis logic (70°C on / 65°C
  off, independent of `fan_enabled`) is pure decision logic, same shape
  as the existing on/off hysteresis — testable the same way once
  extracted into its own decision function, mirroring how
  `volume_bridge_tick()` was factored out for host testing earlier this
  project.
- Needs on-device validation: real thermal behavior is not verifiable
  off-device — flag, don't claim.

## Out of scope

- Making the 70/65°C safety ceiling user-configurable — the whole point
  is a fixed floor that can't be turned off by mistake.
- Hiding/graying out the temperature rows when disabled.
