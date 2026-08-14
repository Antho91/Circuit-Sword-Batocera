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
- **Calibrate on-device latency expectations**: the design doc's claim
  that latency is "bounded by the `wait_edge_events` timeout" is
  slightly imprecise -- the `CHARGING_EDGE_TIMEOUT_S` (1.0s) only bounds
  how promptly the thread notices `stop_event` during shutdown; a real
  GPIO edge wakes `wait_edge_events()` immediately, well under 1s. The
  actual end-to-end latency the user sees on the status bar icon is
  gated by `circuitsword-statusbar`'s own poll interval
  (`SB_POLL_INTERVAL_MS 2000` in statusbar.c, ~2s), not by anything in
  this daemon change. When validating on-device, a status-bar icon
  update landing around ~2s after plug/unplug is the expected success
  case, not a sign edge-detection failed -- only a delay approaching the
  old ~30s ceiling would indicate a regression.
