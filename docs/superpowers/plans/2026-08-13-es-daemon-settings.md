# ES Daemon Settings Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native EmulationStation System Settings screen exposing the same 5 hardware-daemon tunables already editable via the in-game quickmenu overlay, so a user can set them up before ever launching a game.

**Architecture:** A new shell script (`circuitsword-daemon-config`) reads/writes `circuitsword.conf` and pings the daemon's existing `RELOAD_CONFIG` socket command. A new patch to EmulationStation's vendored C++ source adds two `ApiSystem` methods that shell out to that script, and a new `#if CIRCUITSWORD` block in `GuiMenu.cpp`'s System Settings screen with 5 rows using those methods — following the exact existing pattern used for GameForce/RK3326 board-specific rows.

**Tech Stack:** Bash (new script), C++ (ES source patch, CMake/Buildroot), Python 3 (existing daemon, unchanged), Buildroot/Docker build pipeline.

## Global Constraints

- Exactly these 5 config keys, no more, no fewer: `fan_enabled`, `fan_on_temp`, `fan_off_temp`, `fan_poll_interval_s`, `switch_debounce_ms` — same keys, same `circuitsword.conf` file (`/userdata/system/configs/circuitsword.conf`) the daemon and quickmenu already use.
- This screen exists **alongside** the quickmenu's Daemon Settings submenu, not instead of it. Do not touch `circuitsword-quickmenu` source in this plan.
- Range-clamping of values is **not** the shell script's job — `load_config()` in `rpi-circuitsword.py` (already built, unchanged by this plan) is the single source of truth for valid ranges, exactly as it already is for the quickmenu's writes. The new script writes exactly what it's given.
- Socket protocol (already built, do not change): connect to Unix socket `/var/run/circuitsword-joystick.sock`, send `RELOAD_CONFIG\n`, server replies `OK\n`.
- Config file format (already fixed by the daemon, do not change): plain `key=value` lines, `#`-prefixed comment lines and blank lines ignored, no quoting.
- This is this project's first EmulationStation source patch — Buildroot verification for Tasks 2 and 3 involves a FULL EmulationStation rebuild (a large C++ project), which takes significantly longer than this project's prior daemon/kernel-module tasks. Budget accordingly; this is expected, not a sign of a stuck build.
- Per CLAUDE.md Hard Rule #7: a full image build does not pick up edited source in already-built packages. This applies with extra force to a *patch-based* package like `batocera-emulationstation` — Buildroot only re-applies patches when the package is re-extracted, which a plain `-rebuild` does NOT trigger (it recompiles already-patched source in place). Adding or changing a `.patch` file requires `PKG=batocera-emulationstation-dirclean` before the next build, or the new patch content silently never gets applied.

---

### Task 1: `circuitsword-daemon-config` shell script (get/set)

**Files:**
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config`
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpigpioswitch.mk`
- Test: `/Users/bas/Circuit-Sword Batocera/tests/test_circuitsword_daemon_config.sh`

**Interfaces:**
- Consumes: `/userdata/system/configs/circuitsword.conf` (plain `key=value` lines, `#` comments, blank lines ignored — file may not exist yet, same as the daemon's own `load_config()` tolerates), Unix socket `/var/run/circuitsword-joystick.sock` (send `RELOAD_CONFIG\n`, expect `OK\n` reply).
- Produces: `/usr/bin/circuitsword-daemon-config get <key>` (prints the value to stdout, or prints nothing and exits 1 if the key isn't in the file) and `/usr/bin/circuitsword-daemon-config set <key> <value>` (rewrites the file, pings the socket, exits 0 on success). Tasks 2/3's `ApiSystem` methods call this script by these exact two invocation shapes.

- [ ] **Step 1: Read the existing `.mk` file's install pattern**

Read `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/rpigpioswitch.mk` in full before writing anything — find exactly how it currently installs `rpi-circuitsword.py` to `/usr/bin/` (the `INSTALL` command, permissions, target path) so the new script is installed the same way, in the same `define ... INSTALL_TARGET_CMDS` block (or a new one immediately following it, matching the file's existing style).

- [ ] **Step 2: Write `circuitsword-daemon-config`**

Create `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config`:

```bash
#!/bin/sh
# Shell bridge between EmulationStation's native System Settings screen
# and the rpi-circuitsword.py daemon's circuitsword.conf, for the 5
# Daemon Settings tunables also editable via circuitsword-quickmenu's
# in-game overlay. See docs/superpowers/specs/2026-08-13-es-daemon-
# settings-design.md.
#
# Usage:
#   circuitsword-daemon-config get <key>
#   circuitsword-daemon-config set <key> <value>
#
# "get" reads circuitsword.conf directly. "set" rewrites the key's line
# (or appends it if absent) then pings the daemon's RELOAD_CONFIG socket
# command so the change takes effect immediately. This script does NOT
# validate or clamp values -- rpi-circuitsword.py's load_config() is the
# single source of truth for valid ranges, exactly as it already is for
# circuitsword-quickmenu's writes.

set -e

CONFIG_FILE="/userdata/system/configs/circuitsword.conf"
SOCK_PATH="/var/run/circuitsword-joystick.sock"

usage() {
    echo "usage: circuitsword-daemon-config get <key>" >&2
    echo "       circuitsword-daemon-config set <key> <value>" >&2
    exit 2
}

cmd_get() {
    key="$1"
    [ -f "$CONFIG_FILE" ] || exit 1
    # Last matching, non-comment, non-blank line wins -- matches
    # rpi-circuitsword.py's load_config() parsing exactly.
    value=$(grep -E "^${key}=" "$CONFIG_FILE" | tail -n 1 | cut -d= -f2-)
    [ -n "$value" ] || exit 1
    echo "$value"
}

cmd_set() {
    key="$1"
    value="$2"
    mkdir -p "$(dirname "$CONFIG_FILE")"
    touch "$CONFIG_FILE"

    # Rewrite: drop every existing line for this key, then append the new
    # one. Guarantees a single line per key (load_config() would otherwise
    # apply "last line wins", which this collapses to trivially) and never
    # touches any other line, comment, or blank line.
    tmp_file="${CONFIG_FILE}.tmp.$$"
    grep -vE "^${key}=" "$CONFIG_FILE" > "$tmp_file" || true
    echo "${key}=${value}" >> "$tmp_file"
    mv "$tmp_file" "$CONFIG_FILE"

    # Ping the daemon to pick up the change immediately. python3 is
    # already a runtime dependency of rpi-circuitsword.py itself, so it's
    # guaranteed present -- avoids depending on a Unix-socket-capable
    # `nc` build, which this minimal image may not have.
    python3 -c "
import socket
import sys
try:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5.0)
    s.connect('$SOCK_PATH')
    s.sendall(b'RELOAD_CONFIG\n')
    reply = s.recv(64)
    s.close()
    sys.exit(0 if reply.strip() == b'OK' else 1)
except OSError:
    sys.exit(1)
"
}

[ $# -ge 2 ] || usage

case "$1" in
    get)
        [ $# -eq 2 ] || usage
        cmd_get "$2"
        ;;
    set)
        [ $# -eq 3 ] || usage
        cmd_set "$2" "$3"
        ;;
    *)
        usage
        ;;
esac
```

- [ ] **Step 3: Make it executable and wire it into the `.mk`**

```bash
chmod +x /Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config
```

Add an install line for it in `rpigpioswitch.mk`'s existing `INSTALL_TARGET_CMDS` (or equivalent) block, immediately after the line that installs `rpi-circuitsword.py`, using the exact same `$(INSTALL)` invocation style found in Step 1 (mode `0755`, target `$(TARGET_DIR)/usr/bin/circuitsword-daemon-config`).

- [ ] **Step 4: Write the host test script**

Check `/Users/bas/Circuit-Sword Batocera/tests/` for any existing shell-script test convention before writing this (this project has C tests via `run-c-tests.sh` and Python tests via `test_quickmenu_logic.py` — if there is no existing bash test runner, this is the first one; keep it self-contained and simple, matching this project's plain/no-framework style elsewhere).

Create `/Users/bas/Circuit-Sword Batocera/tests/test_circuitsword_daemon_config.sh`:

```bash
#!/bin/sh
# Host test for circuitsword-daemon-config's get/set logic against a
# scratch config file. Does not require a running daemon for the
# get/set-file-rewrite behavior; the socket-ping half is tested
# separately with a stub listener.
set -e

SCRIPT="$(cd "$(dirname "$0")/.." && pwd)/../batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config"
# Fall back to a direct path if the relative layout above doesn't resolve
# (this test may be run from different working directories).
if [ ! -f "$SCRIPT" ]; then
    SCRIPT="/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/utils/rpigpioswitch/circuitsword-daemon-config"
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

CONFIG_FILE="$TMPDIR/circuitsword.conf"
SOCK_PATH="$TMPDIR/circuitsword-joystick.sock"

failures=0
check() {
    if [ "$1" = "$2" ]; then
        echo "  ok   $3"
    else
        echo "  FAIL $3 (expected [$2], got [$1])"
        failures=$((failures + 1))
    fi
}

# --- get: key not present in a fresh/missing file -> exit 1, no output ---
rm -f "$CONFIG_FILE"
if CONFIG_FILE="$CONFIG_FILE" SOCK_PATH="$SOCK_PATH" "$SCRIPT" get fan_on_temp 2>/dev/null; then
    check "exit 0" "exit 1" "get on missing file fails"
else
    check "ok" "ok" "get on missing file fails"
fi

# --- set: appends a new key on first write ---
# (socket ping will fail since no daemon is listening -- test set's file
# behavior by pre-seeding the file directly instead of relying on `set`,
# which requires a live socket to succeed end-to-end; see the separate
# stub-listener test below for the socket-ping path.)
echo "unrelated_key=untouched" > "$CONFIG_FILE"
printf 'fan_on_temp=55\n' >> "$CONFIG_FILE"

got=$(CONFIG_FILE="$CONFIG_FILE" "$SCRIPT" get fan_on_temp)
check "$got" "55" "get reads an existing key"

got=$(CONFIG_FILE="$CONFIG_FILE" "$SCRIPT" get unrelated_key)
check "$got" "untouched" "get does not disturb unrelated keys"

echo ""
echo "$([ "$failures" -eq 0 ] && echo PASSED || echo FAILED) ($failures failures)"
[ "$failures" -eq 0 ]
```

Note: `CONFIG_FILE`/`SOCK_PATH` in the script above are hardcoded constants, not environment-overridable — **before writing the test**, decide whether to make them overridable via environment variables (`: "${CONFIG_FILE:=/userdata/system/configs/circuitsword.conf}"` style in the script) specifically so this test can point them at a scratch file without touching the real device path. Update Step 2's script to use that pattern (`CONFIG_FILE="${CONFIG_FILE:-/userdata/system/configs/circuitsword.conf}"` and `SOCK_PATH="${SOCK_PATH:-/var/run/circuitsword-joystick.sock}"` as the first two lines after `set -e`) so this test can run without root and without touching `/userdata`.

Also add a second test exercising the socket-ping path: start a Python stub listener on a Unix socket at a scratch path, run `circuitsword-daemon-config set <key> <value>` against that scratch socket path (via the `SOCK_PATH` override), and assert (a) the config file now contains the new key, (b) the stub listener actually received exactly `RELOAD_CONFIG\n`. A minimal stub:

```bash
# --- set: writes the key AND pings RELOAD_CONFIG on the socket ---
python3 -c "
import socket, os
sock_path = '$SOCK_PATH'
if os.path.exists(sock_path):
    os.remove(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
srv.settimeout(5.0)
conn, _ = srv.accept()
data = conn.recv(64)
conn.sendall(b'OK\n')
conn.close()
with open('$TMPDIR/received.txt', 'wb') as f:
    f.write(data)
" &
STUB_PID=$!
sleep 0.3  # let the stub listener bind before `set` connects

echo "fan_off_temp=40" > "$CONFIG_FILE"
CONFIG_FILE="$CONFIG_FILE" SOCK_PATH="$SOCK_PATH" "$SCRIPT" set fan_off_temp 42
wait "$STUB_PID"

got=$(CONFIG_FILE="$CONFIG_FILE" "$SCRIPT" get fan_off_temp)
check "$got" "42" "set rewrites the target key"

received=$(cat "$TMPDIR/received.txt")
check "$received" "RELOAD_CONFIG" "set pings RELOAD_CONFIG on the socket"
```

(Fold this into the same test file as the earlier checks, sharing `TMPDIR`/`CONFIG_FILE`/`SOCK_PATH`/`failures`/`check()`.)

- [ ] **Step 5: Run the test**

```bash
chmod +x "/Users/bas/Circuit-Sword Batocera/tests/test_circuitsword_daemon_config.sh"
"/Users/bas/Circuit-Sword Batocera/tests/test_circuitsword_daemon_config.sh"
```

Expected: `PASSED (0 failures)`.

- [ ] **Step 6: Buildroot verification (Hard Rule #7)**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=rpigpioswitch-reinstall bcm2837-pkg
```

Then verify directly against the Docker volume:

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "
ls -la /bcm2837/target/usr/bin/circuitsword-daemon-config
"
```

Expected: the file exists with executable permission bits (`-rwxr-xr-x` or similar) — not just a clean build log.

- [ ] **Step 7: Commit and capture the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/utils/rpigpioswitch/circuitsword-daemon-config package/batocera/utils/rpigpioswitch/rpigpioswitch.mk
git commit -m "rpigpioswitch: add circuitsword-daemon-config get/set script"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

(The test script at `tests/test_circuitsword_daemon_config.sh` lives in the main project directory, which is not a git repository — no commit needed for it.)

---

### Task 2: `ApiSystem` C++ methods

**Files:**
- Create (scratch, outside git, see Step 1): `/tmp/circuitsword-es-scratch/` — a local clone of the ES source, left in place for Task 3 to resume from.
- Create: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/004-circuitsword-daemon-settings.patch`

**Interfaces:**
- Consumes: `circuitsword-daemon-config get <key>` / `circuitsword-daemon-config set <key> <value>` (Task 1, already built and installed to `/usr/bin/`).
- Produces: `ApiSystem::getCircuitSwordDaemonConfig(const std::string& key)` returning `std::string` and `ApiSystem::setCircuitSwordDaemonConfig(const std::string& key, const std::string& value)` returning `bool`. Task 3's `GuiMenu.cpp` block calls both by these exact names/signatures.

- [ ] **Step 1: Clone the pinned ES source into a scratch directory**

This project's `batocera-emulationstation` package is git-fetched by Buildroot from `https://github.com/batocera-linux/batocera-emulationstation` at pinned commit `2c29a330e487210a7d51ad2650bb7b280ea44c86` — it is NOT vendored in this local tree, so there is no local checkout to edit directly. Clone it into a fixed scratch path so Task 3 (a separate, fresh implementer) can find and resume from the exact same state:

```bash
rm -rf /tmp/circuitsword-es-scratch
git clone https://github.com/batocera-linux/batocera-emulationstation.git /tmp/circuitsword-es-scratch
cd /tmp/circuitsword-es-scratch
git checkout 2c29a330e487210a7d51ad2650bb7b280ea44c86
git checkout -b circuitsword-daemon-settings
```

Do **not** delete `/tmp/circuitsword-es-scratch` at the end of this task — Task 3 depends on it still being present with Task 2's commit(s) on the `circuitsword-daemon-settings` branch.

- [ ] **Step 2: Read the existing getter/setter patterns**

Read `/tmp/circuitsword-es-scratch/es-app/src/ApiSystem.cpp` around line 659-696 (`getCurrentStorage()`, `setStorage()`, `setButtonColorGameForce()`, `setPowerLedGameForce()`) and the matching declarations in `/tmp/circuitsword-es-scratch/es-app/src/ApiSystem.h` around line 269-275. These are the exact patterns the new methods must follow:

```cpp
// ApiSystem.cpp — existing getter pattern (getCurrentStorage), for reference:
std::string ApiSystem::getCurrentStorage()
{
	LOG(LogDebug) << "ApiSystem::getCurrentStorage";

#if WIN32
	return "DEFAULT";
#endif

	std::ostringstream oss;
	oss << "batocera-config storage current";
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";

	if (fgets(line, 1024, pipe)) {
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}
	return "INTERNAL";
}

// existing setter pattern (setButtonColorGameForce), for reference:
bool ApiSystem::setButtonColorGameForce(std::string selected)
{
	return executeScript("batocera-gameforce buttonColorLed " + selected);
}
```

- [ ] **Step 3: Add the new methods to `ApiSystem.h`**

In `/tmp/circuitsword-es-scratch/es-app/src/ApiSystem.h`, immediately after the existing `setPowerLedGameForce(std::string basic_string);` line (around line 275), add:

```cpp
	std::string getCircuitSwordDaemonConfig(const std::string& key);
	bool setCircuitSwordDaemonConfig(const std::string& key, const std::string& value);
```

- [ ] **Step 4: Add the new methods to `ApiSystem.cpp`**

In `/tmp/circuitsword-es-scratch/es-app/src/ApiSystem.cpp`, immediately after the existing `setPowerLedGameForce()` implementation (around line 696), add:

```cpp
std::string ApiSystem::getCircuitSwordDaemonConfig(const std::string& key)
{
	LOG(LogDebug) << "ApiSystem::getCircuitSwordDaemonConfig";

	std::ostringstream oss;
	oss << "circuitsword-daemon-config get " << key;
	FILE *pipe = popen(oss.str().c_str(), "r");
	char line[1024];

	if (pipe == NULL)
		return "";

	if (fgets(line, 1024, pipe)) {
		strtok(line, "\n");
		pclose(pipe);
		return std::string(line);
	}

	pclose(pipe);
	return "";
}

bool ApiSystem::setCircuitSwordDaemonConfig(const std::string& key, const std::string& value)
{
	return executeScript("circuitsword-daemon-config set " + key + " " + value);
}
```

- [ ] **Step 5: Generate and place the patch file**

```bash
cd /tmp/circuitsword-es-scratch
git add es-app/src/ApiSystem.h es-app/src/ApiSystem.cpp
git commit -m "Add ApiSystem CircuitSword daemon config get/set"
git diff --no-color 2c29a330e487210a7d51ad2650bb7b280ea44c86 HEAD > \
  "/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/004-circuitsword-daemon-settings.patch"
```

This produces a standard `git diff` (`a/`/`b/` prefix) patch, matching the format of the existing `001-no-next-slot.patch`/`002-quit-add-shift.patch`/`003-fix-sleep-theme-downloader.patch` files in that same directory — Buildroot auto-applies `.patch` files found in a package's source directory before building.

- [ ] **Step 6: Buildroot verification (Hard Rule #7 — patch-based package, needs a clean re-extract)**

Adding a new `.patch` file requires the package to be re-extracted and re-patched from scratch — a plain `-rebuild` reuses the already-extracted (unpatched, for this new patch) source tree and will silently NOT apply the new patch. Force a clean re-extract first:

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=batocera-emulationstation-dirclean bcm2837-pkg
```

Then a full rebuild of the package (this is a large C++ project — expect this to take substantially longer than Task 1's daemon rebuild or any prior plan's kernel-module/daemon tasks in this project; that is expected, not a stuck build):

```bash
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=batocera-emulationstation bcm2837-pkg
```

Expected: build completes with no `Error 1`/`Error 2` lines. If the build log shows the patch failing to apply (`patch failed`/`rejected`), the patch content or line offsets are wrong relative to the pinned commit — re-derive the patch from a fresh clone at the exact pinned commit, don't hand-edit the `.patch` file.

- [ ] **Step 7: Verify directly against the Docker-volume artifact**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "
strings /bcm2837/target/usr/bin/emulationstation-* 2>/dev/null | grep -i circuitsword-daemon-config
"
```

Expected: at least one match — the literal `circuitsword-daemon-config` string (from the `popen`/`executeScript` command strings) baked into the compiled ES binary. If the binary filename differs, first run `docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine find /bcm2837/target/usr/bin -iname '*emulationstation*'` to find the exact binary name before grepping it.

- [ ] **Step 8: Commit and capture the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/emulationstation/batocera-emulationstation/004-circuitsword-daemon-settings.patch
git commit -m "batocera-emulationstation: add ApiSystem CircuitSword daemon config methods"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```

---

### Task 3: `GuiMenu.cpp` CIRCUITSWORD settings block

**Files:**
- Modify: `/tmp/circuitsword-es-scratch/es-app/src/guis/GuiMenu.cpp` (scratch clone from Task 2, must still exist)
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/004-circuitsword-daemon-settings.patch` (regenerated to include this task's changes on top of Task 2's)
- Modify: `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/batocera-emulationstation.mk`

**Interfaces:**
- Consumes: `ApiSystem::getCircuitSwordDaemonConfig(const std::string& key)` / `ApiSystem::setCircuitSwordDaemonConfig(const std::string& key, const std::string& value)` (Task 2, already added to the same patch file this task extends).
- Produces: nothing further consumed by later tasks — this is the last task in the plan.

- [ ] **Step 1: Confirm the scratch clone from Task 2 is present**

```bash
cd /tmp/circuitsword-es-scratch && git log --oneline -3
```

Expected: shows Task 2's "Add ApiSystem CircuitSword daemon config get/set" commit on top of `2c29a330e487210a7d51ad2650bb7b280ea44c86`. If this directory or commit is missing, Task 2 must be re-run first — do not attempt to reconstruct it from the `.patch` file alone in a fresh clone, since this task needs to continue committing on the same branch to regenerate one combined patch.

- [ ] **Step 2: Read the existing GameForce/RK3326 block**

Read `/tmp/circuitsword-es-scratch/es-app/src/guis/GuiMenu.cpp` around line 1813-1861 (the `#if ODROIDGOA || GAMEFORCE || RK3326` multimedia-keys block and the `#if GAMEFORCE || RK3326` LED-color blocks) — this is the exact structural pattern (an `#if` guard, `OptionListComponent`/numeric-input row construction, `addWithLabel`, `addSaveFunc` with a `changed()` check) the new block follows:

```cpp
// existing pattern, for reference (es-app/src/guis/GuiMenu.cpp, ~line 1830):
#if GAMEFORCE || RK3326
	auto buttonColor_GameForce = std::make_shared< OptionListComponent<std::string> >(mWindow, _("BUTTON LED COLOR"));
	buttonColor_GameForce->add(_("off"), "off", SystemConf::getInstance()->get("color_rgb") == "off" || SystemConf::getInstance()->get("color_rgb") == "");
	buttonColor_GameForce->add(_("red"), "red", SystemConf::getInstance()->get("color_rgb") == "red");
	// ... more ->add(...) calls ...
	s->addWithLabel(_("BUTTON LED COLOR"), buttonColor_GameForce);
	s->addSaveFunc([buttonColor_GameForce]
	{
		if (buttonColor_GameForce->changed()) {
			ApiSystem::getInstance()->setButtonColorGameForce(buttonColor_GameForce->getSelected());
			SystemConf::getInstance()->set("color_rgb", buttonColor_GameForce->getSelected());
		}
	});
#endif
```

Note this existing pattern also mirrors the value into `SystemConf` (Batocera's own `batocera.conf` key/value store) for persistence — the new CIRCUITSWORD block does **not** do this, because `circuitsword.conf` (via the daemon and `circuitsword-daemon-config`) is already the single persistent store for these 5 keys; mirroring into `SystemConf` as well would create a second, divergent copy. Read values via `getCircuitSwordDaemonConfig()`, write via `setCircuitSwordDaemonConfig()`, nothing else.

For "Fan enabled" (the one on/off row), find and read the existing `MULTIMEDIA KEYS` on/off/auto row (lines 1813-1828) as the shape reference, simplified to a plain two-state ON/OFF row (no third "auto" state — `fan_enabled` is a 0/1 config value, not a tri-state).

For the 4 numeric rows (`fan_on_temp`, `fan_off_temp`, `fan_poll_interval_s`, `switch_debounce_ms`), search `GuiMenu.cpp` for an existing row that takes free-form numeric text input (not a fixed `OptionListComponent` enum of choices) — grep for `TextComponent` combined with a keyboard-popup pattern, or a `GuiSettings`/`ComponentListRow` numeric-entry row elsewhere in this same file, and use whatever numeric-input component this codebase already uses elsewhere (do not invent a new one; if none exists other than `OptionListComponent`, use a small fixed set of reasonable value choices for each — e.g. temperatures in 5°C steps across a sane range — rather than fabricating a new free-text-entry component from scratch, since that would be a much larger, riskier patch than this plan's scope. Document whichever choice is made directly in this file's comment above the block).

- [ ] **Step 3: Add the CIRCUITSWORD block to `GuiMenu.cpp`**

Add immediately after the existing `#if GAMEFORCE || RK3326` block's closing `#endif` (i.e. right after line ~1861 from Step 2's read, adjust to the actual current line number):

```cpp
#if CIRCUITSWORD
	// Circuit-Sword hardware-daemon tunables -- same 5 settings editable
	// via circuitsword-quickmenu's in-game Daemon Settings overlay
	// (docs/superpowers/specs/2026-08-13-es-daemon-settings-design.md).
	// Values live in circuitsword.conf via the daemon, NOT in SystemConf/
	// batocera.conf -- do not mirror them into SystemConf, unlike the
	// GameForce/RK3326 rows above.
	auto fanEnabled_CircuitSword = std::make_shared< OptionListComponent<std::string> >(mWindow, _("FAN ENABLED"));
	std::string fanEnabledVal_CircuitSword = ApiSystem::getInstance()->getCircuitSwordDaemonConfig("fan_enabled");
	fanEnabled_CircuitSword->add(_("ON"), "1", fanEnabledVal_CircuitSword == "1");
	fanEnabled_CircuitSword->add(_("OFF"), "0", fanEnabledVal_CircuitSword == "0");
	s->addWithLabel(_("FAN ENABLED"), fanEnabled_CircuitSword);
	s->addSaveFunc([fanEnabled_CircuitSword]
	{
		if (fanEnabled_CircuitSword->changed())
			ApiSystem::getInstance()->setCircuitSwordDaemonConfig("fan_enabled", fanEnabled_CircuitSword->getSelected());
	});

	auto fanOnTemp_CircuitSword = std::make_shared< OptionListComponent<std::string> >(mWindow, _("FAN ON TEMP (C)"));
	std::string fanOnTempVal_CircuitSword = ApiSystem::getInstance()->getCircuitSwordDaemonConfig("fan_on_temp");
	for (int t = 45; t <= 75; t += 5)
	{
		std::string tStr = std::to_string(t);
		fanOnTemp_CircuitSword->add(tStr, tStr, fanOnTempVal_CircuitSword == tStr || fanOnTempVal_CircuitSword == (tStr + ".0"));
	}
	s->addWithLabel(_("FAN ON TEMP (C)"), fanOnTemp_CircuitSword);
	s->addSaveFunc([fanOnTemp_CircuitSword]
	{
		if (fanOnTemp_CircuitSword->changed())
			ApiSystem::getInstance()->setCircuitSwordDaemonConfig("fan_on_temp", fanOnTemp_CircuitSword->getSelected());
	});

	auto fanOffTemp_CircuitSword = std::make_shared< OptionListComponent<std::string> >(mWindow, _("FAN OFF TEMP (C)"));
	std::string fanOffTempVal_CircuitSword = ApiSystem::getInstance()->getCircuitSwordDaemonConfig("fan_off_temp");
	for (int t = 35; t <= 65; t += 5)
	{
		std::string tStr = std::to_string(t);
		fanOffTemp_CircuitSword->add(tStr, tStr, fanOffTempVal_CircuitSword == tStr || fanOffTempVal_CircuitSword == (tStr + ".0"));
	}
	s->addWithLabel(_("FAN OFF TEMP (C)"), fanOffTemp_CircuitSword);
	s->addSaveFunc([fanOffTemp_CircuitSword]
	{
		if (fanOffTemp_CircuitSword->changed())
			ApiSystem::getInstance()->setCircuitSwordDaemonConfig("fan_off_temp", fanOffTemp_CircuitSword->getSelected());
	});

	auto fanPollInterval_CircuitSword = std::make_shared< OptionListComponent<std::string> >(mWindow, _("FAN POLL INTERVAL (S)"));
	std::string fanPollIntervalVal_CircuitSword = ApiSystem::getInstance()->getCircuitSwordDaemonConfig("fan_poll_interval_s");
	for (int s_ = 1; s_ <= 10; s_++)
	{
		std::string sStr = std::to_string(s_);
		fanPollInterval_CircuitSword->add(sStr, sStr, fanPollIntervalVal_CircuitSword == sStr);
	}
	s->addWithLabel(_("FAN POLL INTERVAL (S)"), fanPollInterval_CircuitSword);
	s->addSaveFunc([fanPollInterval_CircuitSword]
	{
		if (fanPollInterval_CircuitSword->changed())
			ApiSystem::getInstance()->setCircuitSwordDaemonConfig("fan_poll_interval_s", fanPollInterval_CircuitSword->getSelected());
	});

	auto switchDebounce_CircuitSword = std::make_shared< OptionListComponent<std::string> >(mWindow, _("SWITCH DEBOUNCE (MS)"));
	std::string switchDebounceVal_CircuitSword = ApiSystem::getInstance()->getCircuitSwordDaemonConfig("switch_debounce_ms");
	for (int ms = 200; ms <= 1500; ms += 100)
	{
		std::string msStr = std::to_string(ms);
		switchDebounce_CircuitSword->add(msStr, msStr, switchDebounceVal_CircuitSword == msStr);
	}
	s->addWithLabel(_("SWITCH DEBOUNCE (MS)"), switchDebounce_CircuitSword);
	s->addSaveFunc([switchDebounce_CircuitSword]
	{
		if (switchDebounce_CircuitSword->changed())
			ApiSystem::getInstance()->setCircuitSwordDaemonConfig("switch_debounce_ms", switchDebounce_CircuitSword->getSelected());
	});
#endif
```

The numeric ranges above (45-75°C / 35-65°C / 1-10s / 200-1500ms) are reasonable bounds around this project's existing `DEFAULT_CONFIG` values (`fan_on_temp: 58.0`, `fan_off_temp: 50.0`, `fan_poll_interval_s: 3`, `switch_debounce_ms: 800`, all in `rpi-circuitsword.py`) — adjust only if a value currently in `circuitsword.conf` on a real device falls outside them (unknown at plan-writing time; flag as a concern in the implementer report if so, don't silently widen the range without noting it).

- [ ] **Step 4: Add the `CIRCUITSWORD` compile-time define**

In `/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/batocera-emulationstation.mk`, this project's tree builds exclusively the Circuit-Sword bcm2837 image (unlike upstream Batocera's shared multi-board tree, where `GAMEFORCE`/`RK3326` are gated behind board-specific `BR2_PACKAGE_BATOCERA_TARGET_*` conditionals that don't exist for this board) — so no conditional gate is needed. Add this line unconditionally, next to the existing `BATOCERA_EMULATIONSTATION_CONF_OPTS += -DBATOCERA=ON` line:

```makefile
BATOCERA_EMULATIONSTATION_CONF_OPTS += -DCIRCUITSWORD=ON
```

- [ ] **Step 5: Regenerate the combined patch**

```bash
cd /tmp/circuitsword-es-scratch
git add es-app/src/guis/GuiMenu.cpp
git commit -m "Add CIRCUITSWORD Daemon Settings block to GuiMenu"
git diff --no-color 2c29a330e487210a7d51ad2650bb7b280ea44c86 HEAD > \
  "/Users/bas/batocera-build-wifi/batocera.linux/package/batocera/emulationstation/batocera-emulationstation/004-circuitsword-daemon-settings.patch"
```

This regenerates the patch file to cover BOTH Task 2's `ApiSystem` changes and this task's `GuiMenu.cpp` changes in one combined diff (diffing all the way back to the pinned base commit, not just this task's own commit) — the file at `004-circuitsword-daemon-settings.patch` is the same file Task 2 created, now extended.

- [ ] **Step 6: Buildroot verification (same dirclean requirement as Task 2)**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
export PATH="/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/findutils/libexec/gnubin:$PATH"
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=batocera-emulationstation-dirclean bcm2837-pkg
make BR_DOCKER_VOLUMES=1 O="/Users/bas/Circuit-Sword Batocera/output/output/bcm2837" \
  BR2_EXTERNAL="/Users/bas/batocera-build-wifi/batocera.linux" \
  DL_DIR="/Users/bas/Circuit-Sword Batocera/output/dl" \
  PKG=batocera-emulationstation bcm2837-pkg
```

Expected: build completes with no `Error 1`/`Error 2` lines, both patches (Task 2's `ApiSystem` change and this task's `GuiMenu.cpp` change, now in the same file) apply cleanly.

- [ ] **Step 7: Verify directly against the Docker-volume artifact**

```bash
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "
find /bcm2837/target/usr/bin -iname '*emulationstation*'
" 
docker run --rm -v batocera-output-bcm2837:/bcm2837 alpine sh -c "
strings /bcm2837/target/usr/bin/emulationstation-standalone 2>/dev/null | grep -iE 'FAN ENABLED|FAN ON TEMP|SWITCH DEBOUNCE'
"
```

(Adjust the binary path in the second command if the first command shows a different actual filename.) Expected: at least the `FAN ENABLED`/`FAN ON TEMP`/`SWITCH DEBOUNCE` row-label strings appear in the compiled binary — confirms the new block was actually compiled in, not just that the build succeeded.

**On-device validation required and NOT verifiable from this task** (per the design doc's own Testing section — flag, don't claim): the 5 rows actually rendering correctly in ES's System Settings screen on the real DPI panel; a value changed in this ES screen is picked up by the daemon (`RELOAD_CONFIG` round trip) and is visible next time the quickmenu's Daemon Settings submenu is opened; and the reverse (a quickmenu change is visible next time this ES screen opens).

- [ ] **Step 8: Commit and capture the patch**

```bash
cd /Users/bas/batocera-build-wifi/batocera.linux
git add package/batocera/emulationstation/batocera-emulationstation/004-circuitsword-daemon-settings.patch package/batocera/emulationstation/batocera-emulationstation/batocera-emulationstation.mk
git commit -m "batocera-emulationstation: add CIRCUITSWORD Daemon Settings block to GuiMenu"
git diff --submodule=diff 155c2d8d304cbb53db52e9479dcf683392821d5c HEAD -- . ':!buildroot' > "/Users/bas/Circuit-Sword Batocera/batocera-build/patches/batocera-linux.patch"
```
