# Phase 3: Hardware Daemon (fan, battery, backlight, safe shutdown) — Design

## Goal

Replicate, on Batocera, the hardware integration Phase 3 of the Circuit-Sword
port needs: temperature-controlled fan, battery status feeding
EmulationStation's native UI, backlight control (both via a physical button
combo and Batocera's own brightness UI), and safe shutdown triggered by the
physical power switch — all without the in-game HUD (explicitly deferred,
see "Out of scope").

This preserves already-proven hardware behavior from the RetroPie build
(`Retropie_source/cs-hud_new/`, `Retropie_source/battery-driver/cs_battery.c`)
rather than redesigning it, and integrates with Batocera's own existing
package for this exact problem space
(`package/batocera/utils/rpigpioswitch/`) rather than building something
disconnected from how Batocera itself handles GPIO power switches on other
RPi handheld cases (RetroFlag GPi Case, Kintaro/Roshambo, Argon One, etc).

## Context: hardware behavior already known-working (RetroPie)

- **Fan**: 2-wire blower, GPIO 35, active-LOW (0 = on, 1 = off). On/off only
  — **never PWM the fan's supply** (hard project rule). Hysteresis: on at
  ≥58.0°C, off below 50.0°C (`FAN_ON_TEMP` / `FAN_OFF_TEMP` in
  `cs-hud_new/src/config.h`).
- **Power switch**: GPIO 37, assumed ON = HIGH (RetroPie flags this as an
  assumption still needing hardware confirmation — carry the same caveat
  here). The switch does **not** electrically cut power itself — that's
  gated by the on-board ATmega (Arduino Leonardo) independently of any Pi
  signal. Software's job is only to do a **safe OS shutdown** before the
  ATmega cuts the rail; there is no serial "cut power" command and no
  handshake back to the ATmega.
- **Arduino Leonardo** (`/dev/ttyACM0`): single-byte request/response serial
  protocol, one command per operation (`cs-hud_new/src/config.h`):
  - `CMD_GET_VOLT` ('c') → 2 bytes, raw battery ADC value (low byte, then
    high byte). Converted to volts/percent via the same formula as
    `hardware_read_battery_voltage()` in `cs-hud_new/src/hardware.c`.
  - `CMD_GET_BL` / `CMD_SET_BL` ('q'/'Q') → 1 byte, backlight percent.
  - `CMD_GET_STATUS` ('s') → 1 byte, status flags (bit 0 = menu/mode
    button).
  - `CMD_GET_BTN_NOW` ('b') → 1 byte, current button bitmask.
  - All serial I/O must be serialized behind a single mutex — it's one
    shared UART (see `serial_mtx` in `cs-hud_new/src/hardware.c`).
- **cs_battery.c** (`Retropie_source/battery-driver/cs_battery.c`): a tiny
  (102-line) Linux kernel module registering a virtual
  `/sys/class/power_supply/cs_battery` device with two writable module
  parameters (`capacity`, `charging`). It does not talk to the Arduino
  itself — userspace (there, `cs-hud`) writes the live values into it.
- **nixos-reference** independently confirmed the same button-combo pattern
  for brightness/volume (its README: "mode + left/right: brightness",
  "mode + up/down: volume") — implemented inside its own HUD, same as
  RetroPie. Its own `cs_shutdown.sh` is effectively a copy of RetroPie's,
  not an independently-engineered solution — not used as a reference beyond
  confirming the button-combo pattern is worth preserving.

## Context: Batocera's own existing mechanisms (use these, don't reinvent)

- **`package/batocera/utils/rpigpioswitch/`**: Batocera's own package for
  GPIO-based power switches/buttons on RPi handheld cases. Ships one Python
  3 script per supported case (`rpi-retroflag-GPiCase.py`,
  `rpi-kintaro-SafeShutdown.py`, `rpi-argonone.py`, etc.), selected via
  `rpi_gpioswitch.sh`'s menu and launched by the shared
  `/etc/init.d/S92switch`. As of v3.0 these were migrated from `RPi.GPIO` to
  **`gpiod`** (`python3-gpiod` is already a Buildroot package in this tree)
  — match this convention, don't use `pigpio` (which RetroPie used, but
  Batocera has moved away from).
  - `rpi-argonone.py` is the closest existing precedent (switch + fan
    combined): reads `/sys/class/thermal/thermal_zone0/temp` for CPU temp
    (standard Linux, no custom sensor code needed), and stores
    user-adjustable fan-curve config at
    `/userdata/system/configs/argonone.conf` — follow the same
    `/userdata/system/configs/circuitsword.conf` convention here.
- **`batocera-es-swissknife --shutdown`**: does exactly what RetroPie's
  `do_shutdown()` did by hand (quit the running emulator safely so its
  auto-save flushes, stop EmulationStation cleanly, then a real
  `shutdown -P -h now` — ignoring any configured suspend mode entirely).
  Confirmed via source
  (`package/batocera/core/batocera-scripts/scripts/batocera-es-swissknife`):
  ```sh
  emu_kill; ret=$?
  ES_PID=$(check_esrun)
  if [[ "${1,,}" == "--shutdown" && $ES_PID -ne 0 ]]; then
      /etc/init.d/S31emulationstation stop
      shutdown -P -h now; ret=11
  fi
  ```
  Our switch-monitor calls this one command after debounce — no need to
  orchestrate ES's HTTP API or a suspend-mode decision ourselves.
- **EmulationStation's native battery UI**: `BatteryIconComponent` /
  `BatteryTextComponent` / `BatteryLevelWatcher`
  (`es-core/src/utils/Platform.cpp`) read `/sys/class/power_supply`
  directly — same mechanism as the WiFi icon. No ES changes needed; a
  correctly-registered `power_supply` device is picked up automatically.
- **`batocera-brightness`**: reads/writes `/sys/class/backlight/*/brightness`
  (standard Linux backlight class). No ES/script changes needed; a
  correctly-registered `backlight` device is picked up automatically.

## Out of scope (this phase)

- The graphical in-game HUD (`cs-hud`'s SDL2/KMSDRM menu, on-screen
  volume/brightness bars, etc.) — explicitly deferred. Nothing in this
  design requires it; battery/brightness surface through Batocera's own
  native UI instead, and the button-combo brightness/volume adjustment
  works "blind" (no on-screen feedback), matching how a device without a
  screen-based OSD would behave.
- Bluetooth, WiFi-button hardware toggle, volume/mute button-combo beyond
  what's needed to prove the pattern (can extend later using the same
  mechanism once brightness is proven).
- Re-confirming Phase 0/2 features (DPI, SDIO, UART, WiFi, controllers) —
  already validated.

## Architecture

One new Batocera-native GPIO-switch profile, `CIRCUITSWORD`, added to the
existing `rpigpioswitch` package — not a standalone, disconnected daemon.
Single Python 3 process (`rpi-circuitsword.py`), started by the shared
`S92switch` init script like every other case profile. Two threads inside
that one process (matches "everything in one package"):

```
rpi-circuitsword.py  (launched by /etc/init.d/S92switch)
│
├── fan_thread
│     loop: read /sys/class/thermal/thermal_zone0/temp
│           apply hysteresis (58°C on / 50°C off)
│           gpiod: set GPIO 35 (active-LOW)
│
└── arduino_bridge_thread
      owns the /dev/ttyACM0 mutex; loop (~1s tick):
      - CMD_GET_VOLT -> battery % -> write into cs_battery
        power_supply kernel module (sysfs)
      - read circuitsword-backlight kernel module's current
        `brightness` sysfs value; if changed since last tick,
        CMD_SET_BL to push it to the Arduino
      - CMD_GET_STATUS (mode button) + GPIO button-combo state
        -> mode+left/right: adjust brightness (CMD_SET_BL directly,
           no screen feedback)
        -> (future: mode+up/down volume, same pattern)
      - GPIO 37 poll, 800ms debounce -> on OFF:
           batocera-es-swissknife --shutdown
```

Battery and backlight are exposed through **standard Linux kernel classes**
(`power_supply`, `backlight`) precisely so Batocera's own existing,
unmodified UI/tools (ES's battery icon, `batocera-brightness`) work with
zero changes on their side — the daemon's only job is to be a correct
citizen of those two sysfs interfaces, bridging them to the Arduino.

## Components

1. **`circuitsword-battery` kernel module** — near-identical port of
   `Retropie_source/battery-driver/cs_battery.c`: registers
   `/sys/class/power_supply/circuitsword-battery` with `capacity` and
   `charging` as module parameters the daemon writes into. Built as a
   Buildroot out-of-tree `kernel-module` package, same pattern as the
   `rtw88`/RTL8723BS driver from Phase 2.
2. **`circuitsword-backlight` kernel module** — new (no RetroPie/nixos
   precedent; they both went through their own HUD instead of a sysfs
   class). Registers a `backlight_device` under
   `/sys/class/backlight/circuitsword-backlight`, standard
   `brightness`/`max_brightness` files. The daemon polls `brightness` for
   changes to forward to the Arduino (a virtual backlight device can't
   itself react to a sysfs write — that has to be picked up by userspace).
3. **`rpi-circuitsword.py`** — the combined fan + Arduino-bridge + switch
   daemon described above. `gpiod` for all direct GPIO access (fan, switch,
   any button-combo GPIO lines), matching Batocera's own v3.0+ convention
   — not `pigpio` (RetroPie's choice, since deprecated in this package).
4. **`/userdata/system/configs/circuitsword.conf`** — user-adjustable fan
   thresholds, following the exact convention of `argonone.conf`.
5. **`rpigpioswitch` registration** — add `CIRCUITSWORD` to
   `rpi_gpioswitch.sh`'s `powerdevices` array and its dispatch logic, and
   install `rpi-circuitsword.py` alongside the other case scripts in
   `rpigpioswitch.mk`.

## Data flow

```
Arduino Leonardo (/dev/ttyACM0)
   │  CMD_GET_VOLT, CMD_GET_STATUS, CMD_SET_BL/CMD_GET_BL
   ▼
arduino_bridge_thread (rpi-circuitsword.py)
   │                                   │
   ▼                                   ▼
circuitsword-battery module    circuitsword-backlight module
   │                                   │
   ▼                                   ▼
/sys/class/power_supply/...    /sys/class/backlight/...
   │                                   │
   ▼                                   ▼
ES BatteryIconComponent        batocera-brightness / ES settings
(reads natively)                (reads/writes natively)
                                        ▲
                                        │ (daemon polls for changes,
                                        │  pushes to Arduino)
                          mode+left/right button combo
                          (arduino_bridge_thread, direct
                           CMD_SET_BL, no screen feedback)

GPIO 37 (power switch) ──800ms debounce──> batocera-es-swissknife --shutdown
GPIO 35 (fan) <──hysteresis── /sys/class/thermal/thermal_zone0/temp
```

## Error handling

- **Arduino unreachable** (`/dev/ttyACM0` missing or open fails): fan
  thread is unaffected (pure GPIO, independent). Bridge thread logs and
  retries every few seconds; battery icon reads as absent/unknown in ES
  (same as a device with no battery) rather than crashing anything.
- **Serial timeout / corrupt response**: retry pattern from
  `hardware_read_battery_voltage()` (2-3 attempts, 25ms apart), then hold
  last-known-good value rather than propagate a garbage reading.
- **GPIO 37 switch bounce**: 800ms debounce (RetroPie's proven value)
  before acting, to avoid a false trigger from electrical noise at the
  moment of the flip.
- **`batocera-es-swissknife --shutdown` failure/hang**: no extra fallback
  built on top — this is Batocera's own, already-exercised shutdown path;
  a failure there is a Batocera-level problem, not something to route
  around here.
- **Kernel module load failure** (name clash, etc.): daemon logs and
  continues without that sysfs bridge; fan and switch handling are
  unaffected (independent concerns, one failing must not take down the
  others).

## Testing & verification

No hardware in CI (project-wide constraint). Verifiable off-device:
kernel module compiles against Batocera's kernel headers (same Buildroot
`kernel-module` pattern validated in Phase 2), Python script passes
lint/syntax checks, `gpiod` API calls match the version Batocera ships.

Needs on-device validation, flagged as such rather than assumed:
fan hysteresis behavior, switch debounce timing, the still-unconfirmed
hardware polarity assumptions carried over from RetroPie (fan active-LOW,
switch GPIO 37 ON=HIGH — flip in code if this board reads inverted),
battery percentage accuracy against the real ADC scaling, and that the
button-combo detection doesn't interfere with normal ES navigation input.

## Open items carried into implementation

- Confirm GPIO 35 / GPIO 37 polarities on real hardware (inherited
  assumption from RetroPie, never verified even there — see
  `Retropie_source/FUTURE.md`).
- Exact battery voltage-to-percent scaling constants
  (`BATT_VOLTSCALE`/`BATT_DACRES`/`BATT_DACMAX`/`BATT_RESDIVVAL`/
  `BATT_RESDIVMUL` in `cs-hud_new/src/config.h`) need to be carried over
  verbatim during implementation, not re-derived.
- Whether `mode+up/down` (volume) and other combos get implemented now or
  deferred to a follow-up once brightness proves the pattern — not decided
  here, default to brightness-only for the first working version.
