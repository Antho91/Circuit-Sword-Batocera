#!/usr/bin/env python3
"""Host-side unit tests for the Phase 4 quick-menu logic inside
rpi-circuitsword.py. No hardware, no serial port, no device: `serial`
is stubbed before the module is loaded, and every syscall the tests
touch is monkeypatched.

There are deliberately NO VT tests: the v2 (Wayland overlay) design has
no vt_current()/vt_activate() at all.

Run:  python3 tests/test_quickmenu_logic.py
"""
import importlib.util
import os
import sys
import types
import unittest

BUILD_TREE = os.environ.get(
    "BATOCERA_SRC", "/Users/bas/batocera-build-wifi/batocera.linux")
DAEMON_PATH = os.path.join(
    BUILD_TREE, "package/batocera/utils/rpigpioswitch/rpi-circuitsword.py")


def load_daemon():
    """Import rpi-circuitsword.py by path, with `serial` stubbed out.
    The filename has a hyphen, so a plain `import` cannot reach it."""
    if "serial" not in sys.modules:
        stub = types.ModuleType("serial")

        class SerialException(Exception):
            pass

        class Serial:
            def __init__(self, *a, **kw):
                self.is_open = True

            def reset_input_buffer(self):
                pass

            def write(self, data):
                return len(data)

            def read(self, n):
                return b"\x00" * n

        stub.SerialException = SerialException
        stub.Serial = Serial
        sys.modules["serial"] = stub

    spec = importlib.util.spec_from_file_location("rpi_circuitsword", DAEMON_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


cs = load_daemon()


class TestConstants(unittest.TestCase):
    def test_debounce_is_in_the_designed_range(self):
        # Design doc: ~50-100ms for the plain MODE pushbutton, distinct
        # from the 800ms mechanical power switch.
        self.assertGreaterEqual(cs.MODE_DEBOUNCE_S, 0.050)
        self.assertLessEqual(cs.MODE_DEBOUNCE_S, 0.100)

    def test_watchdog_matches_the_phase3_join_timeout_convention(self):
        self.assertEqual(cs.QUICKMENU_WATCHDOG_S, 5)

    def test_retroarch_command_port(self):
        self.assertEqual(cs.RETROARCH_CMD_PORT, 55355)

    def test_quickmenu_binary_path(self):
        self.assertEqual(cs.QUICKMENU_BIN, "/usr/bin/circuitsword-quickmenu")

    def test_no_vt_helpers_remain(self):
        # The Wayland overlay design has no VT switching at all. If these
        # ever come back, the design has silently regressed.
        self.assertFalse(hasattr(cs, "vt_activate"))
        self.assertFalse(hasattr(cs, "vt_current"))


class TestModeButton(unittest.TestCase):
    def setUp(self):
        self.btn = cs.ModeButton(debounce_s=0.08)

    def test_idle_never_fires(self):
        for i in range(20):
            self.assertFalse(self.btn.update(False, i * 0.01))

    def test_press_fires_once_after_debounce(self):
        self.assertFalse(self.btn.update(True, 1.00))     # edge seen
        self.assertFalse(self.btn.update(True, 1.05))     # not stable yet
        self.assertTrue(self.btn.update(True, 1.10))      # stable -> fire
        self.assertFalse(self.btn.update(True, 1.20))     # held: no repeat
        self.assertFalse(self.btn.update(True, 5.00))     # still held

    def test_release_does_not_fire(self):
        self.btn.update(True, 1.00)
        self.btn.update(True, 1.10)
        self.assertFalse(self.btn.update(False, 2.00))
        self.assertFalse(self.btn.update(False, 2.10))

    def test_bounce_shorter_than_debounce_is_rejected(self):
        self.assertFalse(self.btn.update(True, 1.00))
        self.assertFalse(self.btn.update(False, 1.02))
        self.assertFalse(self.btn.update(True, 1.04))
        self.assertFalse(self.btn.update(False, 1.06))
        # settled low for longer than debounce: still no press
        self.assertFalse(self.btn.update(False, 1.30))

    def test_two_separate_presses_fire_twice(self):
        self.btn.update(True, 1.00)
        self.assertTrue(self.btn.update(True, 1.10))
        self.btn.update(False, 2.00)
        self.btn.update(False, 2.10)
        self.btn.update(True, 3.00)
        self.assertTrue(self.btn.update(True, 3.10))

    def test_reset_swallows_a_still_held_button(self):
        self.btn.update(True, 1.00)
        self.assertTrue(self.btn.update(True, 1.10))
        self.btn.reset()
        # Button still physically held after reset: must not re-fire.
        self.assertFalse(self.btn.update(True, 1.20))
        self.assertFalse(self.btn.update(True, 1.90))


class TestRetroarchRunning(unittest.TestCase):
    def setUp(self):
        self.real_listdir = os.listdir

    def _patch_proc(self, procs):
        """procs: {pid_str: comm_str}"""
        import builtins
        real_open = builtins.open

        def fake_listdir(path):
            if path == "/proc":
                return list(procs.keys()) + ["self", "cpuinfo"]
            return self.real_listdir(path)

        def fake_open(path, *a, **kw):
            if isinstance(path, str) and path.startswith("/proc/") \
                    and path.endswith("/comm"):
                pid = path.split("/")[2]
                if pid not in procs:
                    raise OSError("no such process")
                import io
                return io.StringIO(procs[pid] + "\n")
            return real_open(path, *a, **kw)

        os.listdir = fake_listdir
        builtins.open = fake_open
        self.addCleanup(setattr, os, "listdir", self.real_listdir)
        self.addCleanup(setattr, builtins, "open", real_open)

    def test_true_when_retroarch_present(self):
        self._patch_proc({"101": "sh", "202": "retroarch"})
        self.assertTrue(cs.retroarch_running())

    def test_true_for_truncated_comm(self):
        # /proc/<pid>/comm is capped at 15 chars.
        self._patch_proc({"303": "retroarch-core"})
        self.assertTrue(cs.retroarch_running())

    def test_false_when_only_es_running(self):
        self._patch_proc({"101": "emulationstatio", "102": "connmand"})
        self.assertFalse(cs.retroarch_running())

    def test_false_on_empty_proc(self):
        self._patch_proc({})
        self.assertFalse(cs.retroarch_running())


class TestSessionOpensWithoutPausingWhenUnreachable(unittest.TestCase):
    """MODE pressed with RetroArch's command port silent -- no game
    running (e.g. pressed from EmulationStation), or the port just isn't
    answering -- must still open the overlay. It must NOT send
    PAUSE_TOGGLE: there is nothing running to pause, and pausing here
    with no matching resume signal would be a bug."""

    def test_launches_but_does_not_pause_when_retroarch_does_not_answer(self):
        pause_calls = []

        real_query = cs.retroarch_cmd_query
        real_pause = cs.send_pause_toggle
        real_popen = cs.subprocess.Popen
        self.addCleanup(setattr, cs, "retroarch_cmd_query", real_query)
        self.addCleanup(setattr, cs, "send_pause_toggle", real_pause)
        self.addCleanup(setattr, cs.subprocess, "Popen", real_popen)

        cs.retroarch_cmd_query = lambda cmd, timeout_s=0.5: None
        cs.send_pause_toggle = lambda: pause_calls.append("pause") or True

        class FakeProc:
            def poll(self):
                return 0  # already exited: loop breaks on first check

            def terminate(self):
                pass

            def kill(self):
                pass

            def wait(self, timeout=None):
                pass

        launched = []

        def fake_popen(args, env=None):
            launched.append(args)
            return FakeProc()

        cs.subprocess.Popen = fake_popen
        cs.run_quickmenu_session()

        self.assertEqual(launched, [[cs.QUICKMENU_BIN]],
                          "the overlay must launch even with no game running")
        self.assertEqual(pause_calls, [],
                          "no PAUSE_TOGGLE may fire when nothing is running to pause")


class TestSessionPausesAndResumesWhenReachable(unittest.TestCase):
    """When RetroArch's command port does answer, behavior is unchanged
    from before this task: pause once on open, resume once on close."""

    def test_pauses_once_and_resumes_once_when_retroarch_reachable(self):
        pause_calls = []

        real_query = cs.retroarch_cmd_query
        real_pause = cs.send_pause_toggle
        real_popen = cs.subprocess.Popen
        real_read_mode = cs.read_mode_button
        self.addCleanup(setattr, cs, "retroarch_cmd_query", real_query)
        self.addCleanup(setattr, cs, "send_pause_toggle", real_pause)
        self.addCleanup(setattr, cs.subprocess, "Popen", real_popen)
        self.addCleanup(setattr, cs, "read_mode_button", real_read_mode)

        cs.retroarch_cmd_query = lambda cmd, timeout_s=0.5: b"GET_STATUS ok"
        cs.send_pause_toggle = lambda: pause_calls.append("pause") or True
        cs.read_mode_button = lambda: False

        class FakeProc:
            def __init__(self):
                self._polled_once = False

            def poll(self):
                if not self._polled_once:
                    self._polled_once = True
                    return None  # still running on first check
                return 0  # exited on second check

            def terminate(self):
                pass

            def kill(self):
                pass

            def wait(self, timeout=None):
                pass

        cs.subprocess.Popen = lambda args, env=None: FakeProc()
        cs.run_quickmenu_session()

        self.assertEqual(pause_calls, ["pause", "pause"],
                          "exactly one pause (open) and one resume (close) "
                          "when RetroArch is reachable")


class TestStatusbarTick(unittest.TestCase):
    """statusbar_tick() is the single-decision core statusbar_thread loops
    on: start the bar if a game just started and nothing is tracked, stop
    it if the game just exited, self-heal (relaunch) if the tracked
    process is found dead while a game is still running, and otherwise
    leave an already-running bar untouched."""

    def setUp(self):
        self.real_running = cs.retroarch_running
        self.real_popen = cs.subprocess.Popen
        self.addCleanup(setattr, cs, "retroarch_running", self.real_running)
        self.addCleanup(setattr, cs.subprocess, "Popen", self.real_popen)

    def test_launches_when_game_starts_and_nothing_tracked(self):
        cs.retroarch_running = lambda: True
        launched = []

        class FakeProc:
            def poll(self):
                return None   # still running

        def fake_popen(args, env=None):
            launched.append(args)
            return FakeProc()

        cs.subprocess.Popen = fake_popen
        proc = cs.statusbar_tick(None)
        self.assertEqual(launched, [[cs.STATUSBAR_BIN]])
        self.assertIsNotNone(proc)

    def test_keeps_running_process_untouched_while_game_still_running(self):
        cs.retroarch_running = lambda: True

        class FakeProc:
            def __init__(self):
                self.terminated = False
            def poll(self):
                return None   # still alive
            def terminate(self):
                self.terminated = True

        def must_not_launch(*a, **kw):
            raise AssertionError("must not relaunch an already-running process")

        cs.subprocess.Popen = must_not_launch
        existing = FakeProc()
        proc = cs.statusbar_tick(existing)
        self.assertIs(proc, existing)
        self.assertFalse(existing.terminated)

    def test_stops_when_game_exits(self):
        cs.retroarch_running = lambda: False

        class FakeProc:
            def __init__(self):
                self.terminated = False
                self.killed = False
            def poll(self):
                return None   # still alive until terminated
            def terminate(self):
                self.terminated = True
            def wait(self, timeout=None):
                pass
            def kill(self):
                self.killed = True

        existing = FakeProc()
        proc = cs.statusbar_tick(existing)
        self.assertIsNone(proc)
        self.assertTrue(existing.terminated)
        self.assertFalse(existing.killed, "graceful terminate should be enough here")

    def test_relaunches_if_process_found_dead_while_game_still_running(self):
        cs.retroarch_running = lambda: True

        class DeadProc:
            def poll(self):
                return 1   # exited with an error

        launched = []

        class FakeProc:
            def poll(self):
                return None

        def fake_popen(args, env=None):
            launched.append(args)
            return FakeProc()

        cs.subprocess.Popen = fake_popen
        proc = cs.statusbar_tick(DeadProc())
        self.assertEqual(launched, [[cs.STATUSBAR_BIN]],
                          "a dead process while the game is still running must self-heal")
        self.assertIsNotNone(proc)

    def test_stays_none_when_no_game_and_nothing_tracked(self):
        cs.retroarch_running = lambda: False

        def must_not_launch(*a, **kw):
            raise AssertionError("must not launch when no game is running")

        cs.subprocess.Popen = must_not_launch
        proc = cs.statusbar_tick(None)
        self.assertIsNone(proc)


class TestVolumeBridgeTick(unittest.TestCase):
    """volume_bridge_tick() is the single-decision core volume_bridge() loops
    on: mirror the Arduino's hardware-combo volume into ALSA, mirror ALSA's
    volume back into the Arduino when something else changed it, prefer the
    board on a same-tick conflict, and do nothing when neither changed or
    both reads failed."""

    def test_board_changed_only_applies_to_alsa(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 60, 50
        )
        self.assertEqual(last_board, 60)
        self.assertEqual(last_system, 60)
        self.assertEqual(action, ("apply_to_alsa", 60))

    def test_system_changed_only_applies_to_board(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 50, 70
        )
        self.assertEqual(last_board, 50)
        self.assertEqual(last_system, 70)
        self.assertEqual(action, ("apply_to_board", 70))

    def test_both_changed_same_tick_board_wins(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 80, 20
        )
        self.assertEqual(last_board, 80)
        self.assertEqual(last_system, 80)
        self.assertEqual(action, ("apply_to_alsa", 80))

    def test_neither_changed_does_nothing(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, 50, 50
        )
        self.assertEqual(last_board, 50)
        self.assertEqual(last_system, 50)
        self.assertIsNone(action)

    def test_both_reads_failed_preserves_last_values(self):
        last_board, last_system, action = cs.volume_bridge_tick(
            50, 50, None, None
        )
        self.assertEqual(last_board, 50)
        self.assertEqual(last_system, 50)
        self.assertIsNone(action)


class TestFanDecision(unittest.TestCase):
    def setUp(self):
        self.daemon = load_daemon()

    def _cfg(self, fan_enabled=1, fan_on_temp=58.0, fan_off_temp=50.0):
        return {
            "fan_on_temp": fan_on_temp,
            "fan_off_temp": fan_off_temp,
            "fan_poll_interval_s": 3,
            "switch_debounce_ms": 800,
            "fan_enabled": fan_enabled,
        }

    def test_enabled_normal_hysteresis_turns_on(self):
        cfg = self._cfg(fan_enabled=1)
        self.assertTrue(self.daemon.fan_decision(False, 60.0, cfg))

    def test_enabled_normal_hysteresis_turns_off(self):
        cfg = self._cfg(fan_enabled=1)
        self.assertFalse(self.daemon.fan_decision(True, 45.0, cfg))

    def test_enabled_normal_hysteresis_dead_zone_keeps_state(self):
        cfg = self._cfg(fan_enabled=1)
        self.assertTrue(self.daemon.fan_decision(True, 54.0, cfg))
        self.assertFalse(self.daemon.fan_decision(False, 54.0, cfg))

    def test_disabled_stays_off_below_safety_ceiling(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertFalse(self.daemon.fan_decision(False, 69.9, cfg))

    def test_disabled_forces_on_at_safety_ceiling(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertTrue(self.daemon.fan_decision(False, 70.0, cfg))

    def test_disabled_forced_on_turns_back_off_below_safety_floor(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertFalse(self.daemon.fan_decision(True, 64.9, cfg))

    def test_disabled_forced_on_stays_on_in_safety_dead_zone(self):
        cfg = self._cfg(fan_enabled=0)
        self.assertTrue(self.daemon.fan_decision(True, 67.0, cfg))

    def test_disabled_ignores_user_configured_thresholds(self):
        # Even with fan_on_temp set very low, a disabled fan must not
        # turn on below the hard-coded safety ceiling.
        cfg = self._cfg(fan_enabled=0, fan_on_temp=10.0, fan_off_temp=5.0)
        self.assertFalse(self.daemon.fan_decision(False, 50.0, cfg))


if __name__ == "__main__":
    unittest.main(verbosity=2)
