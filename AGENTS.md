# Murin Firmware Workspace Instructions

## Project context

- This repository contains ESP-IDF 6.0 firmware for an ESP32-S3 Murin robot, plus host-side serial tools and tests.
- Work from the repository root unless a command says otherwise. The documented shell is PowerShell on Windows.
- Read the nearest relevant README before changing a subsystem. Start with `README.md`; use `protocol.md` for the wire contract, `pinmap.md` and `main/config/config.h` for hardware assignments, `tools/README.md` for serial tools, and the READMEs under `tests/` for test requirements.
- `main/app/main.cpp` is the startup/composition root. Production modules live in `main/modules/`; shared compile-time hardware and drive constants live in `main/config/config.h`; transport selection lives in `main/Kconfig.projbuild`.

## Working rules

- Inspect the implementation and its tests before editing. Preserve existing user changes and keep patches focused on the requested behavior.
- Use ESP-IDF APIs compatible with v6.0. Keep C-facing headers usable from C++ by retaining the existing `extern "C"` guards.
- Follow the repository's `.clang-format` and `.clang-tidy` configuration. Do not hand-edit generated output in `build/`, `tests/gtest/build*`, `managed_components/`, coverage output, or dependency caches.
- When adding or removing a production source module, update `main/CMakeLists.txt`. When adding a host unit-test target or mock, update `tests/gtest/CMakeLists.txt` and keep mocks under the matching `tests/gtest/mocks/<subsystem>/` directory.
- Treat GPIO, PWM, brakes, motors, flash/NVS, clocks, USB/UART selection, and telemetry timing as behaviorally significant. Check call order, failure handling, safe-state behavior, and task/timer lifetime rather than changing constants in isolation.
- Do not silently rename the existing `ros2` module: despite its name, it implements a transport-independent custom binary protocol and does not require ROS middleware.

## Source update checklist

Use this workflow whenever source code is changed. Skip an item only when it clearly does not apply, and mention material omissions in the handoff.

### Before editing

- Read this file, `README.md`, and the nearest subsystem README or contract document. For link or message changes, read `protocol.md` plus the affected firmware, host-tool, and test implementations before choosing an edit location.
- Check the working tree and preserve unrelated or in-progress user changes. Do not reformat generated files or rewrite files outside the requested scope.
- Trace the affected behavior end to end: startup/composition, task or timer ownership, module API, hardware driver, protocol encoder/decoder, host consumer, and persistent configuration as applicable.
- Identify safety-sensitive paths up front, including motor output, brakes, GPIO/PWM state, watchdogs and heartbeat timeout, flash/NVS writes, transport selection, disconnect handling, and task/timer cleanup.
- Decide how the change will be verified without hardware. Add or extend host GoogleTest coverage when logic can be isolated with existing or focused mocks; plan hardware pytest only when the behavior depends on a board or real peripheral.

### While editing

- Keep changes small and consistent with the existing C/C++, ESP-IDF, CMake, Python, and JavaScript style in the touched area.
- Update every producer and consumer when changing an API, configuration key, message ID, payload, framing rule, pin assignment, or observable behavior. Search the repository for all affected names rather than relying on one call site.
- Update build and test registration with the source change: production modules belong in `main/CMakeLists.txt`; host targets, sources, and mocks belong in `tests/gtest/CMakeLists.txt` and the matching mock directory.
- Validate external input before it reaches motors, storage, memory copies, or hardware APIs. Preserve length checks, numeric bounds, finite-value checks, safe defaults, and stop/brake behavior on errors.
- Preserve lifecycle correctness: check initialization and teardown order, release owned resources, stop timers/tasks safely, avoid duplicate registrations, and handle partial initialization and repeated reconnects.
- Keep documentation synchronized in the same patch. Update `protocol.md` for wire changes, `pinmap.md` for pin changes, Kconfig/help text for configuration changes, and the relevant README for setup, commands, prerequisites, module boundaries, or operator-visible behavior.
- Add focused regression tests for changed logic and failure paths when practical. For protocol parsing, cover fragmented input, multiple frames per chunk, malformed lengths, invalid CRC/escaping, payload limits, numeric bounds, and sequence wrap as applicable.

### Before handing off

- Review the diff for accidental changes, debug logging, credentials, machine-specific serial ports, captured data, generated artifacts, and edits outside the intended scope.
- Run the narrowest relevant host check first, then run the complete host GoogleTest suite when the change touches production C/C++ logic that it covers:

  ```powershell
  cmake -S tests/gtest -B tests/gtest/build
  cmake --build tests/gtest/build --config Release
  ctest --test-dir tests/gtest/build --output-on-failure -C Release
  ```

- Format touched source files with the appropriate configured formatter and inspect the formatting diff. Run `.\scripts\format-all.ps1` only when repository-wide formatting is intended because it rewrites files across the tree.
- Run an ESP-IDF build for firmware source, component registration, Kconfig, compile-time configuration, or integration changes when the ESP-IDF environment is available. A host-test pass does not replace this build.
- Run `python -m pytest tests/pytest -v` only when a correctly configured board is connected, serial ports are exclusive, and the user has authorized hardware interaction. Use `.\scripts\test-all.ps1` only when those hardware conditions and its coverage-tool prerequisites are satisfied.
- For hardware-affecting changes, verify safe startup, normal operation, failure/reconnect behavior, and safe shutdown on the target device when authorized. Leave motors stopped or braked on exit or failure whenever possible.
- Re-read the affected documentation and public headers after the implementation settles. Confirm commands, examples, units, defaults, limits, and compatibility notes match the code.
- In the handoff, list files changed, checks run and their results, checks not run and why, and any remaining board, serial, sensor, timing, persistence, or motor validation. Distinguish host simulation and syntax/build checks from real hardware validation.

## Protocol contract

- Treat `main/modules/link/framed_link.[ch]` as the link-layer implementation and `main/modules/ros2/ros2_msgs.[ch]` as the application message implementation. `protocol.md` is the public client contract and must remain sufficient to implement an independent client.
- Preserve these link invariants unless the task explicitly changes the protocol: byte-stream parsing, `0xAA` SOF, little-endian fields, CRC-16/CCITT-FALSE over the exact stuffed body, payload-only escaping, a 256-byte decoded limit, a 512-byte stuffed limit, and independent wrapping host/telemetry sequence counters.
- For every protocol change, update all affected surfaces in the same patch:
  - firmware constants, validation, packing, and handlers;
  - `protocol.md`, including byte sizes, offsets, units, response behavior, and examples;
  - `utils/protocol_common.py` and `utils/protocol_serial.py` when shared host behavior changes;
  - `tools/parser.py`, `tools/parser.js`, and `tools/dump_diff_drive.py` as applicable;
  - GoogleTest link/application tests and hardware pytest coverage.
- Encode/decode packed wire values explicitly. Do not rely on native structure layout, padding, host endianness, or transport read boundaries. Validate lengths before reading fields, validate finite/range-limited motor values, and preserve ACK/NACK sequence matching.
- Keep compatibility aliases only when they are intentional and tested. If implementation and documentation disagree, investigate the tests and call out the mismatch; do not guess which behavior clients depend on.

## Host tools and configuration

- Prefer the shared Python codec in `utils/protocol_common.py` over duplicating framing logic in Python. Keep Python and Node parser behavior aligned when changing user-visible protocol support.
- Serial settings are local and hardware-dependent. `tools/config.yaml` currently supplies tool defaults; `tests/pytest/test_config.yaml` supplies pytest ports, baud rates, and thresholds. Do not assume the checked-in COM port is correct for another machine.
- Never expose a serial stream as text or assume one read equals one frame. WebSocket gateways must forward binary bytes unchanged and retain stream-parser buffering.
- Avoid committing machine-specific port changes, captured binary traffic, generated reports, or credentials unless the user explicitly asks for them.

## Verification

- Choose the narrowest relevant check first, then expand when practical.
- Host GoogleTest does not require a board:

  ```powershell
  cmake -S tests/gtest -B tests/gtest/build
  cmake --build tests/gtest/build --config Release
  ctest --test-dir tests/gtest/build --output-on-failure -C Release
  ```

- Hardware pytest requires flashed firmware and correctly configured, exclusive serial ports:

  ```powershell
  python -m pytest tests/pytest -v
  ```

  Run it only when a board is connected and the user has authorized hardware interaction. Close ESP-IDF monitor and other serial applications first. Report skips separately from passes.
- The combined runner executes hardware pytest, host GoogleTest, and LLVM/LCOV coverage:

  ```powershell
  .\scripts\test-all.ps1
  ```

  It requires the board plus Clang, Ninja, LLVM coverage tools, `genhtml`, and on Windows sometimes Perl. Do not present missing-tool coverage failures as product regressions.
- Format the repository with `.\scripts\format-all.ps1` only when broad formatting is appropriate; it rewrites C/C++, Python, and CMake files across the tree. For a focused change, format only touched files with the corresponding formatter.
- For a firmware build, initialize ESP-IDF in the same PowerShell process, then build:

  ```powershell
  & 'C:\esp\v6.0\esp-idf\export.ps1'
  idf.py build
  ```

- Flashing, opening a serial monitor, running motor commands, or tests that actuate hardware can change physical state. Do not run them without explicit user authorization and confirmed port/board selection. When authorized, leave motors in a stopped/braked state on exit or failure whenever the available tool supports it.

## Documentation and handoff

- Update the relevant README when commands, prerequisites, configuration, module boundaries, or observable behavior change. Update `pinmap.md` together with pin definitions.
- In the final response, state what changed, which checks ran, what could not run, and whether any hardware-backed behavior remains unverified. Never imply a host mock validates real FreeRTOS scheduling, ESP-IDF integration, serial timing, sensors, NVS hardware, or motor actuation.
