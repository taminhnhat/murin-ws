import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "formatter", Path(__file__).resolve().parents[1] / "tools/format-all.py"
)
FORMAT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FORMAT)


class FormatterTests(unittest.TestCase):
    def test_scope_and_generated_file_exclusions(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            names = [
                "src/murin_control/hardware/system.cpp",
                "src/murin_control/CMakeLists.txt",
                "scripts/tool.py",
                "src/murin_control/scripts/robot_socket_bridge",
                "src/ros2_control_demos/example_2/demo.cpp",
                "src/murin_control/build-debug/generated.cpp",
                "src/murin_control/install/generated.py",
                "scripts/.venv/helper.py",
                "scripts/__pycache__/cache.py",
                "build/generated.cpp",
            ]
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    "#!/usr/bin/env python3\n"
                    if name.endswith("robot_socket_bridge")
                    else ""
                )
            (root / "scripts/linked.py").symlink_to(root / "scripts/tool.py")
            groups = FORMAT.source_files(root)
            self.assertEqual([p.name for p in groups["cpp"]], ["system.cpp"])
            self.assertEqual(
                {p.name for p in groups["python"]}, {"tool.py", "robot_socket_bridge"}
            )
            self.assertEqual([p.name for p in groups["cmake"]], ["CMakeLists.txt"])

    def test_check_flags_never_write(self):
        commands = list(
            FORMAT.commands(
                {
                    "cpp": [Path("x.cpp")],
                    "python": [Path("x.py")],
                    "cmake": [Path("CMakeLists.txt")],
                },
                True,
            )
        )
        self.assertIn("--dry-run", commands[0])
        self.assertIn("--Werror", commands[0])
        self.assertIn("--check", commands[1])
        self.assertIn("--check", commands[2])
        self.assertTrue(all("-i" not in command for command in commands))

    def test_missing_tools_prevent_partial_formatting(self):
        groups = {"cpp": [Path("x.cpp")], "python": [Path("x.py")], "cmake": []}
        with (
            patch.object(FORMAT, "source_files", return_value=groups),
            patch.object(
                FORMAT.shutil,
                "which",
                side_effect=lambda tool: (
                    None if tool == "ruff" else "/bin/clang-format"
                ),
            ),
            patch.object(FORMAT.subprocess, "run") as run,
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(FORMAT.main([]), 1)
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
