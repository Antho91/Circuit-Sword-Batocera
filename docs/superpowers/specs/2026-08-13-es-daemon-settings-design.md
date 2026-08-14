# ES Daemon Settings Screen — Design

Adds a native EmulationStation (ES) System Settings screen exposing the
same 5 hardware-daemon tunables already editable via the in-game quickmenu
overlay, so a first-time user can find and set them up before ever
launching a game — without needing to know about the MODE-button overlay.

## Context

`circuitsword-quickmenu`'s in-game "Daemon Settings" submenu (built earlier
this project) already reads/writes 5 config keys in
`/userdata/system/configs/circuitsword.conf`, via a Unix-socket protocol
(`GET_CONFIG`/`RELOAD_CONFIG`) to the `rpi-circuitsword.py` daemon:

- `fan_enabled` (0/1)
- `fan_on_temp` / `fan_off_temp` (°C)
- `fan_poll_interval_s`
- `switch_debounce_ms`

That overlay requires a MODE-button press mid-game — good for quick
tweaks, bad for initial discovery. This design adds a second surface for
the *same* 5 settings inside ES's own native System Settings menu,
reachable without ever launching a game. Both surfaces read/write the same
`circuitsword.conf` file and stay in sync.

This is this project's **first ES source patch**. Investigation confirmed
Batocera has no config-driven/declarative mechanism for adding rows to
ES's main settings menu — every board-specific settings screen (GameForce,
RK3326, ODROID-GO Advance LED/multimedia-key rows) is hardcoded C++ in
`es-app/src/guis/GuiMenu.cpp`, gated by an `#ifdef <BOARD>` block, calling
out to shell scripts via `ApiSystem::executeScript(...)` (e.g.
`ApiSystem::setButtonColorGameForce()` → `executeScript("batocera-gameforce
buttonColorLed " + selected)`). This design follows that exact, existing
pattern rather than inventing a new one.

## Behavior / UI

- New rows in ES's System Settings screen, inside a new `#if CIRCUITSWORD`
  block placed alongside the existing `#if GAMEFORCE || RK3326` /
  `#if ODROIDGOA || GAMEFORCE || RK3326` blocks in `GuiMenu.cpp`.
- Exactly the same 5 settings as the quickmenu's Daemon Settings submenu,
  no subset, no additions:
  - "Fan enabled" — on/off row (`OptionListComponent`, mirrors the existing
    `MULTIMEDIA KEYS` auto/on/off row's shape, but binary).
  - "Fan ON temp (°C)", "Fan OFF temp (°C)" — numeric input rows.
  - "Fan poll interval (s)" — numeric input row.
  - "Switch debounce (ms)" — numeric input row.
- Values are read fresh from `circuitsword.conf` (via the new
  `ApiSystem::getCircuitSwordDaemonConfig()`, see Architecture) each time
  the System Settings screen opens — no caching across screen opens.
- Writes commit immediately via each row's `addSaveFunc`, matching the
  existing pattern every other row in this screen already uses (no
  separate "Apply"/"Save" button — same UX as the rest of ES's settings).
- This screen exists **alongside**, not instead of, the quickmenu's
  Daemon Settings submenu — both remain usable, both operate on the same
  config file, and a change made in one is visible in the other (subject
  to each surface's own refresh timing: the quickmenu re-queries on submenu
  entry, this ES screen re-queries on screen entry).

## Architecture

**New shell script** `circuitsword-daemon-config` (bash), installed to
`/usr/bin/`, living alongside `rpi-circuitsword.py`'s own package
(`package/batocera/utils/rpigpioswitch/`) since that package already owns
`circuitsword.conf`'s schema:

- `circuitsword-daemon-config get <key>` — reads `circuitsword.conf`
  directly and prints the value for `<key>` (same file the daemon's own
  `load_config()` reads).
- `circuitsword-daemon-config set <key> <value>` — rewrites the `<key>=`
  line in `circuitsword.conf` (create it if absent, matching the file's
  existing simple `key=value` format), then pings the daemon's
  `RELOAD_CONFIG` command over its existing Unix socket
  (`/var/run/circuitsword-joystick.sock`) so the change takes effect
  immediately — the exact same two-step (file write + socket ping) the
  quickmenu's C code already performs on every Daemon Settings write.
  Range-clamping of the value is **not** this script's job — it writes
  what it's given; `load_config()`'s existing clamp-on-reload logic (run
  as part of handling `RELOAD_CONFIG`) is the single source of truth for
  valid ranges, exactly as it already is for the quickmenu's writes.

**New `ApiSystem` methods** (`es-app/src/ApiSystem.h` /
`es-app/src/ApiSystem.cpp`):

- `std::string getCircuitSwordDaemonConfig(const std::string& key)`
- `bool setCircuitSwordDaemonConfig(const std::string& key, const
  std::string& value)`

Both thin wrappers around `executeScript(...)`/the equivalent
output-capturing helper ES already uses elsewhere for `get`-style calls,
mirroring `setButtonColorGameForce()`'s exact shape — no socket-protocol
code lives in ES itself.

**New `GuiMenu.cpp` block**: 5 rows as described in Behavior/UI, each
populated via `getCircuitSwordDaemonConfig()` on screen build and written
via `setCircuitSwordDaemonConfig()` in its `addSaveFunc`.

**Build plumbing**: a new `.patch` file in
`package/batocera/emulationstation/batocera-emulationstation/` (Buildroot
auto-applies `.patch` files in a package directory before the build,
matching this project's other C++ package patches), touching `GuiMenu.cpp`
and `ApiSystem.{h,cpp}`. The `CIRCUITSWORD` compile-time define's exact
plumbing (most likely a `-DCIRCUITSWORD=ON` flag added to
`BATOCERA_EMULATIONSTATION_CONF_OPTS` in this project's local
`batocera-emulationstation.mk` override, following the existing pattern of
`-DBCM=ON -DRPI=ON` for Raspberry Pi builds) is confirmed during
implementation, not blocking this design.

## Data flow

```
ES GuiMenu.cpp row (on screen open)
  -> ApiSystem::getCircuitSwordDaemonConfig("fan_on_temp")
  -> executeScript("circuitsword-daemon-config get fan_on_temp")
  -> reads circuitsword.conf directly, prints value

ES GuiMenu.cpp row (on save)
  -> ApiSystem::setCircuitSwordDaemonConfig("fan_on_temp", "55")
  -> executeScript("circuitsword-daemon-config set fan_on_temp 55")
  -> rewrites circuitsword.conf's fan_on_temp= line
  -> pings RELOAD_CONFIG on /var/run/circuitsword-joystick.sock
  -> rpi-circuitsword.py's joystick_ipc_thread() calls reload_config()
  -> load_config() re-reads the file, clamps, daemon now uses new value
```

## Testing

- **Off-device**: `circuitsword-daemon-config`'s `get`/`set` shell logic
  can be exercised against a scratch config file on the host (no daemon
  needed for the file-rewrite half; the socket-ping half needs a running
  daemon or a stub listener to verify the ping is actually sent).
- **Needs on-device validation** (flag, don't claim): the real ES source
  patch compiles and links against this project's vendored ES version; the
  `CIRCUITSWORD` define actually reaches the compiled binary; the 5 rows
  render correctly in ES's System Settings screen on the real DPI panel;
  a value changed in ES is picked up by the daemon (RELOAD_CONFIG round
  trip) and is visible when the quickmenu's Daemon Settings submenu is
  opened next; and the reverse (a quickmenu change is visible next time
  the ES screen opens).

## Out of scope

- Any settings beyond the existing 5 — no new tunables introduced.
- Removing or hiding the quickmenu's Daemon Settings submenu — both
  surfaces coexist permanently, per the user's explicit choice.
- Any generic/declarative "board settings from config" mechanism in ES
  itself — investigated and confirmed not to exist; out of scope to build
  one for this project alone.
- Live two-way sync while both surfaces are open simultaneously (e.g. ES
  screen open in one session while quickmenu is also open) — not a
  realistic use case on this single-user, single-display device.
