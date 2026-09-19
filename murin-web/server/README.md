# Robot transports

See [the main README](../README.md) for server setup. `ROBOT_TRANSPORT` selects exactly one backend:

- `serial`: `robot_link.js` owns the robot serial port and `console_link.js` handles the console. Existing direct dashboard operation is unchanged.
- `socket`: `robot_socket.js` accepts a ROS robot source on the Socket.IO `/robot` namespace. The server opens no robot or console serial ports. C++ ROS hardware owns the serial device; a separate Python ROS node connects it to this server.

The ROS package's [README](../../murin-ros2/src/murin_control/README.md) describes installation, geometry configuration, and launch commands.

## Startup

The default HTTP port is 9091; ROS `server_url` must match `HTTP_PORT`. Use `scripts/start-server.sh --check` (or the PowerShell wrapper) to validate configuration without opening ports. For ROS mode, start the server with `ROBOT_TRANSPORT=socket`, then use `murin-ros2/scripts/run-all.sh`. That launcher prevents duplicate ROS stacks. After an ESP32 serial fault, stop and restart the single ROS stack; resetting the board alone does not reactivate its driver.

## Socket events

| Event                   | Direction                      | Payload                                                                                                 |
| ----------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------- |
| `robot_source_register` | ROS source → server            | `{name}`; acknowledgement `{ok: true}` or `{ok: false, message}`                                        |
| `robot_source_status`   | Registered source → server     | `{connected: boolean, message: string}`                                                                 |
| `imu_status`            | Server → dashboards            | Latest source hardware status; also sent on connection                                                  |
| `imu_status_request`    | Dashboard → server             | Requests current `imu_status`                                                                           |
| `cmd_vel`               | Dashboard → source             | `{left: number, right: number}` in m/s, finite and within ±0.5                                          |
| `estop`                 | Dashboard → source             | No payload; ROS bridge latches a stop                                                                   |
| `goal`                  | Dashboard → source             | JSON object up to 4096 serialized characters; current ROS bridge logs it as unsupported                 |
| `imu`                   | Registered source → dashboards | Existing `{valid, status, timestampUs, acceleration, angularVelocity, magneticField, quaternion}` shape |
| `robot_msg`             | Registered source → dashboards | `{msgType, msgName, seq, rawPayload, telemetry}`; drive frames also contain `driveState`                |

Only one robot source may register at a time. Repeat registration by the same source is acknowledged. Registration itself reports hardware disconnected until the source publishes its actual status. A source disconnect reports disconnected; a dashboard disconnect sends `estop` to the registered source.

The bridge maps battery state (0x03, 22 bytes) to `robot_msg.telemetry`, IMU state (0x04, 62 bytes) to `imu`, and drive state (0x05, 20 bytes) to `robot_msg.driveState`. Drive fields are `timestampMs`, `linearVelocity`, `angularVelocity`, `leftVelocity`, and `rightVelocity`. IMU timestamps are decimal strings and quaternion order is w,x,y,z. Raw unstuffed payloads remain hexadecimal strings.

A stopped ROS bridge requires `/murin_socket_bridge/reset_estop` and a fresh dashboard command. Dashboard commands expire after 0.5 seconds; reconnecting does not resume old motion. These rules apply to the ROS socket backend; direct serial behavior remains in `robot_link.js`.

## Checks without hardware

```bash
node --test server/test/robot_socket.test.js
npm test
npm run format:check
```

The socket test creates an ephemeral server and clients without loading `.env` or opening serial ports. It checks source ownership, validation, telemetry forwarding, and disconnect stops.
