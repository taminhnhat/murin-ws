"""Run pytest, GoogleTest, and LLVM coverage with genhtml HTML reports."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GTEST_ROOT = ROOT / "tests" / "gtest"
GTEST_BUILD = GTEST_ROOT / "build"
GTEST_COVERAGE_BUILD = GTEST_ROOT / "build-coverage"
ROS2_MSGS_SOURCE = ROOT / "main" / "modules" / "ros2" / "ros2_msgs.c"
FLASH_STORAGE_SOURCE = ROOT / "main" / "modules" / "nvs" / "flash_storage.c"
FRAMED_LINK_SOURCE = ROOT / "main" / "modules" / "link" / "framed_link.c"
DIFF_DRIVE_SOURCE = ROOT / "main" / "modules" / "motor" / "diff_drive.c"
DIAG_SOURCE = ROOT / "main" / "modules" / "diag" / "diag.c"


def run(command: list[str], env: dict[str, str] | None = None) -> tuple[int, str]:
    """Run a command with live native output and retain it for summaries."""
    sys.stdout.flush()
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=False,
    )
    output = bytearray()
    assert process.stdout is not None
    # Read raw chunks instead of lines. Pytest uses carriage returns for
    # progress updates, so readline() can wait until the entire run ends.
    for chunk in iter(lambda: os.read(process.stdout.fileno(), 4096), b""):
        output.extend(chunk)
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
    return process.wait(), output.decode("utf-8", errors="replace")


def number(text: str, pattern: str) -> int:
    match = re.search(pattern, text)
    return int(match.group(1)) if match else 0


def executable(build_dir: Path, name: str) -> Path:
    """Return the executable path produced by the single-config Ninja build."""
    suffix = ".exe" if os.name == "nt" else ""
    return build_dir / f"{name}{suffix}"


def print_branch_coverage(lcov: str) -> None:
    """Show the same per-module branch totals used by genhtml."""
    print("\n=== Branch Coverage (LCOV) ===")
    for record in lcov.split("end_of_record"):
        fields = dict(line.split(":", 1) for line in record.splitlines() if ":" in line)
        if "SF" not in fields:
            continue
        # genhtml counts BRDA records, including constant loop branches that
        # LLVM can omit from its BRF/BRH summary counters.
        branches = [line.rsplit(",", 1)[-1] for line in record.splitlines() if line.startswith("BRDA:")]
        total = len(branches)
        covered = sum(taken != "-" and int(taken) > 0 for taken in branches)
        percent = f"{100 * covered / total:.2f}%" if total else "N/A"
        name = fields["SF"].replace("\\", "/").rsplit("/", 1)[-1]
        print(f"{name:<18}: {percent} (covered={covered}, total={total}, missed={total - covered})")


def run_coverage() -> tuple[int, str]:
    """Build coverage and return its exit code plus report path or failure reason."""
    required_tools = ("clang", "clang++", "ninja", "llvm-profdata", "llvm-cov", "genhtml")
    tools = {name: shutil.which(name) for name in required_tools}
    # Windows cannot launch LCOV's extensionless Perl script directly.
    if os.name == "nt" and tools["genhtml"] is None:
        tools["genhtml"] = next(
            (str(Path(directory) / "genhtml") for directory in os.get_exec_path()
             if (Path(directory) / "genhtml").is_file()),
            None,
        )
    genhtml_command = [tools["genhtml"]]
    if os.name == "nt" and tools["genhtml"] is not None and Path(tools["genhtml"]).suffix.lower() not in (
        ".exe", ".com", ".bat", ".cmd"
    ):
        tools["perl"] = shutil.which("perl")
        genhtml_command.insert(0, tools["perl"])
    missing = [name for name, path in tools.items() if path is None]
    if missing:
        reason = f"missing tools: {', '.join(missing)}"
        print(f"Coverage unavailable: {reason}")
        return 1, f"FAILED ({reason})"

    configure_command = [
        "cmake",
        "-S",
        str(GTEST_ROOT),
        "-B",
        str(GTEST_COVERAGE_BUILD),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_FLAGS=-fprofile-instr-generate -fcoverage-mapping",
        "-DCMAKE_CXX_FLAGS=-fprofile-instr-generate -fcoverage-mapping",
    ]
    googletest_source = GTEST_BUILD / "_deps" / "googletest-src"
    if googletest_source.is_dir():
        configure_command.append(
            f"-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST={googletest_source}"
        )

    configure_env = os.environ.copy()
    configure_env["CC"] = tools["clang"]
    configure_env["CXX"] = tools["clang++"]
    code, _ = run(configure_command, env=configure_env)
    if code != 0:
        return code, "FAILED (coverage build configuration failed)"
    code, _ = run(["cmake", "--build", str(GTEST_COVERAGE_BUILD)])
    if code != 0:
        return code, "FAILED (coverage build failed)"

    for profile in GTEST_COVERAGE_BUILD.glob("*.profraw"):
        profile.unlink()

    coverage_env = os.environ.copy()
    coverage_env["LLVM_PROFILE_FILE"] = str(
        GTEST_COVERAGE_BUILD / "%p-%m.profraw"
    )
    code, _ = run(
        [
            "ctest",
            "--test-dir",
            str(GTEST_COVERAGE_BUILD),
            "--output-on-failure",
        ],
        env=coverage_env,
    )
    if code != 0:
        return code, "FAILED (instrumented tests failed)"

    profiles = sorted(GTEST_COVERAGE_BUILD.glob("*.profraw"))
    if not profiles:
        print("Coverage failed: no raw profiles were generated")
        return 1, "FAILED (no raw profiles were generated)"

    profile_data = GTEST_COVERAGE_BUILD / "coverage.profdata"
    code, _ = run(
        [
            tools["llvm-profdata"],
            "merge",
            "-sparse",
            *(str(profile) for profile in profiles),
            "-o",
            str(profile_data),
        ]
    )
    if code != 0:
        return code, "FAILED (profile merge failed)"

    ros2_executable = executable(GTEST_COVERAGE_BUILD, "ros2_msgs_unittest")
    flash_executable = executable(GTEST_COVERAGE_BUILD, "flash_storage_unittest")
    framed_link_executable = executable(GTEST_COVERAGE_BUILD, "framed_link_unittest")
    diff_drive_executable = executable(GTEST_COVERAGE_BUILD, "diff_drive_unittest")
    diag_executable = executable(GTEST_COVERAGE_BUILD, "diag_unittest")
    common_coverage_args = [
        str(ros2_executable),
        f"-object={flash_executable}",
        f"-object={framed_link_executable}",
        f"-object={diff_drive_executable}",
        f"-object={diag_executable}",
        f"-instr-profile={profile_data}",
    ]
    sources = [
        str(ROS2_MSGS_SOURCE),
        str(FLASH_STORAGE_SOURCE),
        str(FRAMED_LINK_SOURCE),
        str(DIFF_DRIVE_SOURCE),
        str(DIAG_SOURCE),
    ]

    print("\n=== LLVM Coverage ===")
    code, _ = run(
        [tools["llvm-cov"], "report", *common_coverage_args, *sources]
    )
    if code != 0:
        return code, "FAILED (LLVM coverage report failed)"

    # Keep diagnostics on stderr so they cannot corrupt the LCOV tracefile.
    export = subprocess.run(
        [tools["llvm-cov"], "export", *common_coverage_args, "-format=lcov", *sources],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    if export.returncode != 0:
        return export.returncode, "FAILED (LCOV export failed)"

    coverage_info = GTEST_COVERAGE_BUILD / "coverage.info"
    html_dir = GTEST_COVERAGE_BUILD / "html"
    # MSYS2 genhtml treats CR from Windows CRLF as part of source filenames.
    coverage_info.write_text(export.stdout, encoding="utf-8", newline="\n")
    print_branch_coverage(export.stdout)
    code, _ = run(
        [
            *genhtml_command,
            str(coverage_info),
            "--output-directory",
            str(html_dir),
            "--branch-coverage",
            "--title",
            "Murin Firmware Coverage",
        ]
    )
    if code != 0:
        return code, "FAILED (genhtml report generation failed; see output above)"
    report = f"PASSED ({html_dir / "index.html"})"
    return 0, report


def main() -> int:
    pytest_code, pytest_output = run(
        [sys.executable, "-m", "pytest", "tests/pytest", *sys.argv[1:]]
    )

    gtest_output = ""
    gtest_code, _ = run(["cmake", "-S", str(GTEST_ROOT), "-B", str(GTEST_BUILD)])
    if gtest_code == 0:
        gtest_code, _ = run(["cmake", "--build", str(GTEST_BUILD), "--config", "Release"])
    if gtest_code == 0:
        gtest_code, gtest_output = run(
            ["ctest", "--test-dir", str(GTEST_BUILD), "--output-on-failure", "-C", "Release"]
        )

    coverage_code, coverage_result = run_coverage()

    pytest_counts = {
        "passed": number(pytest_output, r"(\d+)\s+passed"),
        "failed": number(pytest_output, r"(\d+)\s+failed"),
        "skipped": number(pytest_output, r"(\d+)\s+skipped"),
        "errors": number(pytest_output, r"(\d+)\s+errors?"),
    }
    pytest_counts["total"] = sum(pytest_counts.values())

    gtest_counts = {"passed": 0, "failed": 0, "skipped": 0, "errors": 0, "total": 0}
    gtest_summary = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", gtest_output)
    if gtest_summary:
        gtest_counts["failed"] = int(gtest_summary.group(2))
        gtest_counts["total"] = int(gtest_summary.group(3))
        gtest_counts["passed"] = gtest_counts["total"] - gtest_counts["failed"]
    gtest_counts["skipped"] = number(gtest_output, r"(\d+) tests? not run")

    print("\n=== Test Summary ===")
    for name, counts, code in (("pytest", pytest_counts, pytest_code), ("gtest", gtest_counts, gtest_code)):
        result = "PASSED" if code == 0 else "FAILED"
        print(
            f"{name:<10}: {result} (total={counts['total']}, passed={counts['passed']}, "
            f"failed={counts['failed']}, skipped={counts['skipped']}, errors={counts['errors']})"
        )
    print(f"{'coverage':<10}: {coverage_result}")

    return int(pytest_code != 0 or gtest_code != 0 or coverage_code != 0)


if __name__ == "__main__":
    raise SystemExit(main())
