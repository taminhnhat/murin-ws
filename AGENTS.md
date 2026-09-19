# Murin Web Workspace Instructions

## Project context

- This repository contains the Node.js web dashboard and serial gateway for the Murin robot. It uses CommonJS, Express, Socket.IO, browser-native JavaScript, and plain HTML/CSS; there is no frontend build step.
- Work from the repository root. `server/index.js` is the process entry point, `server/src/app.js` configures HTTP routes and static assets, and `server/src/socket/index.js` composes the Socket.IO namespaces.
- Browser assets live under `server/public/`. Treat files named `*_old.*` as legacy references unless the requested change explicitly targets them.
- Read `server/README.md` before changing Socket.IO namespaces, rooms, or events. The firmware repository at `../murin-firmware` owns the robot wire protocol; consult its `protocol.md` and implementation before changing serial framing or payloads.

## Working rules

- Inspect the relevant server and browser call sites before editing. Preserve existing user changes and keep patches focused on the requested behavior.
- Keep server code compatible with the Node.js/CommonJS style already used by the repository. Do not introduce a frontend framework, transpiler, bundler, or TypeScript without an explicit request.
- Keep shared Socket.IO event names and payload shapes synchronized between `server/src/socket/`, the link utilities, browser scripts, and `server/README.md`.
- Preserve graceful behavior when optional DOM elements, serial devices, or configuration values are absent. Do not make normal development or syntax checks require attached hardware.
- Do not edit dependencies under `node_modules/` or generated caches. Update `package.json` and `package-lock.json` together when dependencies change.
- Keep secrets and machine-local settings out of source control. Runtime environment variables are loaded from `server/.env`; serial ports and baud rates are hardware-dependent, so do not assume checked-in values are valid on another machine.

## Source update checklist

Use this workflow whenever source code is changed. Skip an item only when it clearly does not apply, and mention material omissions in the handoff.

### Before editing

- Read this file, `server/README.md`, and the nearest relevant implementation files. For serial or protocol work, also read `../murin-firmware/protocol.md` and the corresponding firmware implementation.
- Check the working tree and preserve unrelated or in-progress user changes. Do not reformat or rewrite files outside the requested scope.
- Trace the behavior end to end before choosing an edit location: browser event/DOM handler, Socket.IO boundary, server handler, serial link, and firmware contract as applicable.
- Identify safety-sensitive paths up front, including motor commands, emergency stop, disconnect handling, console commands, retry timers, and serial-port ownership.
- Decide how the change will be verified without hardware. If meaningful automated coverage is practical, add it with the change.

### While editing

- Keep changes small and consistent with the existing CommonJS and browser-native style.
- Update both producers and consumers when an event name, payload, route, configuration key, or protocol field changes. Search the repository for every affected name rather than relying on one call site.
- Validate untrusted input at the server boundary before it reaches serial or system-facing code. Preserve bounded motor values and fail-safe stop behavior.
- Preserve cleanup and lifecycle behavior: remove listeners when needed, clear timers, avoid duplicate reconnect loops, and handle partial initialization and disconnects.
- Do not hide configuration or compatibility changes behind silent fallbacks. Prefer explicit status messages and actionable errors.
- Update comments only when they explain current non-obvious behavior. Remove or revise comments that become stale.

### Before handing off

- Review the diff for accidental changes, debug logging, credentials, machine-specific ports, generated files, and edits to legacy `*_old.*` files.
- Run the narrowest relevant checks first, then the standard repository checks:

  ```powershell
  npm test
  npm run format:check
  ```

- For browser changes, manually verify the affected UI when practical, including initial load, reconnect/disconnect, missing data, resizing, and the relevant keyboard or gamepad path. Do not start the server against configured real serial ports without authorization.
- For HTTP or Socket.IO changes, verify success and invalid-input paths and confirm the browser and server still agree on payload shape.
- For serial or protocol changes, add or run parser/encoder tests for fragmented input, multiple frames per chunk, malformed lengths, bad CRC, escaping, numeric bounds, and sequence wrap as applicable. Coordinate the matching firmware and protocol-document updates.
- Update `server/README.md` for changes to setup, environment variables, routes, namespaces, rooms, events, payloads, or operator-visible behavior.
- Report the files changed, checks run and their results, checks not run and why, and any remaining browser, serial, firmware, or hardware validation.

## Robot and console links

- Treat `server/src/utils/robot_link.js` as protocol-sensitive code. Preserve byte-stream buffering, framing, escaping, CRC coverage, little-endian encoding, payload-length validation, and wrapping sequence behavior unless the task explicitly changes the protocol.
- For protocol changes, update every affected encoder, decoder, constant, validation path, browser consumer, test/check, and relevant documentation in both this repository and `../murin-firmware` when that repository is in scope. Call out any cross-repository work that remains.
- Validate all browser-originated robot commands at the server boundary. Motor values must remain finite and bounded, and disconnect/error paths must retain their safe-stop behavior.
- Console input is an external command surface. Preserve length and character validation and do not interpolate commands into a shell.
- Serial ports are exclusive hardware resources. Do not start the server against real ports, open a monitor, transmit commands, or run anything that may actuate the robot without explicit user authorization and confirmed port selection. When hardware use is authorized, leave motion stopped on exit or failure whenever possible.

## Browser changes

- Keep the dashboard usable as directly served static files. Maintain the script ordering and globals established in `server/public/index.html`, including the `window.Murin` Socket.IO clients.
- When changing UI behavior, check missing-element guards, reconnect/disconnect states, keyboard and gamepad controls, fullscreen behavior, and resize handling as applicable.
- Avoid broad edits to legacy files. If active and legacy pages intentionally diverge, document which surface was changed.

## Verification

- Run the repository checks after code changes:

  ```powershell
  npm test
  npm run format:check
  ```

- `npm test` currently performs JavaScript syntax checks; it is not behavioral, browser, serial, or hardware coverage. Add focused tests when introducing logic that can be exercised without a device.
- Use `npm run format` only when formatting the repository's supported web files is appropriate. Review the resulting diff because it can rewrite many files.
- Starting `npm run dev` or `npm start` may open configured serial ports and can send a time-sync frame or motor stop. Do not use either command as a harmless verification step when real ports are configured.

## Documentation and handoff

- Update `server/README.md` when Socket.IO namespaces, rooms, events, payloads, configuration, or operational behavior changes.
- In the final response, state what changed, which checks ran, what could not run, and whether browser, serial, or robot hardware behavior remains unverified. Do not describe syntax checks or mocked behavior as end-to-end hardware validation.

## ROS bridge and current operating modes

- `ROBOT_TRANSPORT=serial` uses `robot_link.js` and the console link. `ROBOT_TRANSPORT=socket` uses `robot_socket.js` and opens neither serial device. Keep this selection exclusive.
- The socket backend accepts one robot source on `/robot`. C++ ROS hardware owns the serial device; a separate Python ROS service registers and forwards telemetry. Read `../murin-ros2/AGENTS.md` for cross-repository edits.
- Default HTTP/bridge port is 9091; verify the effective `HTTP_PORT` and `server_url` rather than assuming localhost or a port. Never read or print unrelated `.env` secrets during diagnosis.
- Preserve source registration acknowledgement, hardware-connected status, finite ±0.5 m/s wheel commands, and disconnect/estop forwarding. A dashboard disconnect latches the ROS bridge stop; passive diagnostics should use ordinary HTTP or ROS subscriptions rather than temporary dashboard clients.
- Do not start direct serial mode while ROS owns `/dev/murin-cdc`. ESP32 resets leave the current ROS hardware driver inactive until a clean restart. Starting a second controller manager is not recovery.
- Run `scripts/start-server.sh --check` (or the PowerShell wrapper) for configuration validation without starting the server. Socket mode requires no USB/console settings. `--check` does not prove the selected device or robot is healthy.
- In addition to `npm test` and `npm run format:check`, run `node --test server/test/robot_socket.test.js` for socket changes and `python3 -m unittest discover -s scripts/tests -v` for launcher changes. Use the ROS PTY integration test for changes to both sides of the bridge.

## Script platform parity

Keep `.sh` and `.ps1` wrappers for portable development workflows synchronized by delegating to the same Python implementation. Forward all arguments and preserve exit codes. Test the shared implementation and shell wrappers; report when PowerShell is unavailable. Firmware udev setup is Linux-only. `murin-ros2` intentionally uses only `.sh` wrappers; do not add PowerShell versions there.
