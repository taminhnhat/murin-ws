import contextlib
import fcntl
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "run_control", Path(__file__).resolve().parents[1] / "tools/run-control.py"
)
RUN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUN)


class RunControlTests(unittest.TestCase):
    def test_default_is_simulation_and_bridge_uses_9091(self):
        with patch.dict(RUN.os.environ, {}, clear=True):
            cmd = RUN.command(RUN.arguments([]))
        self.assertIn("transport:=simulation", cmd)
        self.assertIn("serial_port:=", cmd)
        self.assertIn("server_url:=http://localhost:9091", cmd)
        self.assertIn("require_bridge:=true", cmd)

    def test_hardware_and_external_bridge(self):
        args = RUN.arguments(
            [
                "--hardware",
                "--serial-port",
                "/dev/test robot",
                "--external-bridge",
                "--wheel-radius",
                ".12",
            ]
        )
        cmd = RUN.command(args)
        self.assertIn("serial_port:=/dev/test robot", cmd)
        self.assertIn("transport:=serial", cmd)
        self.assertIn("enable_socket_bridge:=false", cmd)
        self.assertIn("require_bridge:=true", cmd)
        self.assertIn("wheel_radius:=0.12", cmd)

    def test_invalid_geometry_and_modes(self):
        for args in [
            ["--wheel-radius", "nan"],
            ["--wheel-separation", "0"],
            ["--no-bridge", "--external-bridge"],
            ["--server-url", "localhost:9091"],
        ]:
            with (
                self.subTest(args=args),
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit),
            ):
                RUN.arguments(args)

    def test_environment_override(self):
        with patch.dict(
            RUN.os.environ, {"MURIN_SERVER_URL": "http://robot.local:9999"}
        ):
            self.assertEqual(RUN.arguments([]).server_url, "http://robot.local:9999")
            self.assertEqual(
                RUN.arguments(["--server-url", "http://localhost:9091"]).server_url,
                "http://localhost:9091",
            )

    def test_duplicate_launch_detected_from_argv(self):
        with tempfile.TemporaryDirectory() as temp:
            proc = Path(temp)
            (proc / "123").mkdir()
            (proc / "123/cmdline").write_bytes(
                b"python3\0ros2\0launch\0murin_control\0murin.launch.py\0"
            )
            self.assertEqual(RUN.existing_stacks(proc), ["123"])
        with (
            patch.object(RUN, "existing_stacks", return_value=["123"]),
            self.assertRaisesRegex(RuntimeError, "already running"),
        ):
            RUN.preflight(RUN.arguments([]))

    def test_lock_blocks_second_launcher(self):
        with (
            tempfile.TemporaryDirectory() as temp,
            patch.object(RUN, "ROOT", Path(temp)),
        ):
            (Path(temp) / "log").mkdir()
            with (Path(temp) / "log/murin-control.lock").open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with self.assertRaisesRegex(RuntimeError, "Another run-all"):
                    RUN.run_locked(RUN.arguments(["--check"]))

    def test_dry_run_does_not_run_preflight(self):
        with (
            patch.object(RUN, "preflight") as check,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            self.assertEqual(RUN.main(["--dry-run", "--hardware"]), 0)
            check.assert_not_called()


if __name__ == "__main__":
    unittest.main()
