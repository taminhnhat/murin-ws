# ROS2 message unit tests

This directory contains a standalone CMake project that builds the `ros2_msgs`
C source and runs its GoogleTest-based C++ test harness.

## Run the GoogleTest suite

From the repository root:

```powershell
cmake -S tests/gtest -B tests/gtest/build
cmake --build tests/gtest/build --config Release
ctest --test-dir tests/gtest/build --output-on-failure -C Release
```

The project uses CMake `FetchContent` to download GoogleTest into
`tests/gtest/build/_deps` on the first configure.

The suite also builds `flash_storage.c` against a host NVS mock backed by the
text file `flash_storage_gtest.txt`. The file is created in the test working
directory and cleaned up by the flash-storage test fixture.

Host-only stubs live under their subsystem directories in `mocks/`. Shared
ESP-IDF compatibility headers live in `mocks/include/`; the GTest project root
contains only test entry files and project metadata.

The ROS2 host stubs retain registered task and timer callbacks and model the
single-item telemetry queues. Tests run a task through one processing cycle,
returning at its next notification wait via a C-only `setjmp`/`longjmp` boundary.
This exercises command reception, telemetry delivery, masks, and timer controls
without starting threads. Each fixture resets the host state and telemetry
configuration so tests can run in any order.

ROS2 tests also inject sensor, motor, task-creation, and timer-creation failures.
The flash-storage mock supports NVS initialization, erase, open, write, and
commit failures, with counters to verify recovery, skipped commits, and handle
cleanup. These fault controls are reset between tests.

Coverage describes the `UNIT_TEST` build of `ros2_msgs.c`. The firmware-only
settings persistence and runtime-saving task are excluded by that build flag;
host tests do not measure those functions or real FreeRTOS scheduling behavior.
Unreachable defensive branches remain in the coverage report, including the
flash-storage type-switch defaults for the fixed item table and ROS2's constant
loop conditions and payload-size check.

## Run coverage analysis

LLVM source-based coverage requires Clang, `llvm-profdata`, and `llvm-cov`.
The combined runner, `python scripts/test-all.py`, also requires LCOV's
`genhtml` on `PATH` (and Perl on Windows when `genhtml` is a Perl script).
It exports `build-coverage/coverage.info` and generates a report titled
"Murin Firmware Coverage" in `build-coverage/html`, including branch coverage.
The runner prints per-module covered/total/missed branch counts from the LCOV
branch records to match `genhtml`. LLVM's own summary can differ because it
omits some constant-condition branches from its totals.

From the repository root in PowerShell, configure a separate instrumented
Debug build:

```powershell
cmake -S tests/gtest -B tests/gtest/build-coverage -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  "-DCMAKE_C_FLAGS=-fprofile-instr-generate -fcoverage-mapping" `
  "-DCMAKE_CXX_FLAGS=-fprofile-instr-generate -fcoverage-mapping"

cmake --build tests/gtest/build-coverage
```

Run both test executables and merge their raw profiles:

```powershell
$coverageDir = (Resolve-Path tests/gtest/build-coverage).Path

Get-ChildItem -LiteralPath $coverageDir -Filter "*.profraw" -File |
  Remove-Item -Force

$env:LLVM_PROFILE_FILE = "$coverageDir/%p-%m.profraw"
ctest --test-dir $coverageDir --output-on-failure
$env:LLVM_PROFILE_FILE = $null

$profiles = Get-ChildItem -LiteralPath $coverageDir -Filter "*.profraw" -File |
  Select-Object -ExpandProperty FullName

llvm-profdata merge -sparse $profiles `
  -o "$coverageDir/coverage.profdata"
```

Print a summary for the two production modules:

```powershell
$ros2Source = (Resolve-Path main/modules/ros2/ros2_msgs.c).Path
$flashSource = (Resolve-Path main/modules/nvs/flash_storage.c).Path

llvm-cov report "$coverageDir/ros2_msgs_unittest.exe" `
  "-object=$coverageDir/flash_storage_unittest.exe" `
  "-instr-profile=$coverageDir/coverage.profdata" `
  $ros2Source $flashSource
```

Generate a line-by-line HTML report:

```powershell
llvm-cov show "$coverageDir/ros2_msgs_unittest.exe" `
  "-object=$coverageDir/flash_storage_unittest.exe" `
  "-instr-profile=$coverageDir/coverage.profdata" `
  -format=html `
  "-output-dir=$coverageDir/html" `
  -show-line-counts-or-regions `
  -show-branches=count `
  $ros2Source $flashSource
```

Open `tests/gtest/build-coverage/html/index.html` in a browser to inspect the
annotated source report.
