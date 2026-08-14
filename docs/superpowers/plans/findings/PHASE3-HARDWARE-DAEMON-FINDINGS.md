# Phase 3 Hardware Daemon — Findings Log

Companion to docs/superpowers/specs/2026-08-04-phase3-hardware-daemon-design.md
and docs/superpowers/plans/2026-08-04-phase3-hardware-daemon.md.

Real build tree: /Users/bas/batocera-build-wifi/batocera.linux
Reproducible patches: /Users/bas/Circuit-Sword Batocera/batocera-build/patches/

## Task log

### Task 1: build tree confirmed clean (only Phase 2's known changes present)

### Task 2: circuitsword-battery kernel module ported
Near-verbatim port of Retropie_source/battery-driver/cs_battery.c.
Registers /sys/class/power_supply/circuitsword-battery. Not yet build-
verified against the real kernel headers (needs Task 9's full build).

### Task 3: circuitsword-backlight kernel module written (new, no direct precedent)
Standard Linux backlight_device API. max_brightness=100 matches the
Arduino's CMD_GET_BL/CMD_SET_BL percent protocol directly, no rescaling
needed. Not yet build-verified (needs Task 9).

### Task 4: rpi-circuitsword.py started -- serial helper + fan thread
serial_cmd() retry contract: 3 attempts, 25ms apart, matching hardware.c.
Fan thread: GPIO 35 active-LOW via gpiod, hysteresis from config.h defaults,
overridable via /userdata/system/configs/circuitsword.conf. py_compile
clean. Not yet runnable end-to-end (needs Tasks 5/6's main()).

### Task 5: battery bridge added
CMD_GET_VOLT decode + BATT_* formula ported verbatim from hardware.c/
config.h. Charging read via direct GPIO 36 (active HIGH), NOT serial --
corrects an earlier design-doc simplification that implied charging came
over the Arduino link. 

Initial formula from brief was missing final `/100.0` division (found in
hardware.c line 190); fixed in review and re-verified. Corrected formula
sanity-check output:
```
raw=1 -> voltage=0.014V -> percent=0%
raw=512 -> voltage=4.174V -> percent=100%
raw=1023 -> voltage=8.333V -> percent=100%
```
No exceptions, monotonic behavior, edges clamp correctly (0%/100%). Voltages
now plausible for Li-ion battery: raw=512 yields ~4.17V (within/above
3.2-4.0V range, clamps to 100%), raw=1023 yields ~8.33V (above max, clamps
to 100%). Real ADC accuracy still needs Task 9 on-device testing.

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

### Task 7: main() wired, full daemon file complete
4 daemon threads (fan, battery, backlight, switch), SIGTERM/SIGINT handled
for clean shutdown. py_compile + function-presence check both pass. File
is executable. Still entirely unverified on real hardware (Task 9).

### Task 8: CIRCUITSWORD registered in rpigpioswitch
Added to powerdevices array, circuitsword_start/stop/config functions
(mirrors kintaro_* exactly), dispatch case, and rpigpioswitch.mk install
rule. This makes CIRCUITSWORD selectable via
`batocera-settings-set system.power.switch CIRCUITSWORD` on-device (or via
the rpi_gpioswitch dialog menu) -- not yet tested end-to-end (Task 9).
