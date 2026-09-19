# Murin Robot Binary Protocol

This document specifies the binary protocol used to control the Murin robot and receive telemetry. It is intended to be sufficient for an independent client in Node.js, a browser, Python, ROS 2, or another environment. No ROS middleware is required: despite the firmware module name, this is a transport-independent binary protocol.

## 1. Transport

The protocol is a continuous, ordered byte stream. A frame may be split across reads, and one read may contain multiple frames. Clients must keep a persistent receive buffer and parse complete frames from it.

The firmware normally exposes the stream through USB CDC ACM (a virtual serial port). A build can instead select UART. The default UART settings are 115200 baud, 8 data bits, no parity, and 1 stop bit; use the firmware build settings as the authority if changed.

A Node.js server can open the port with a package such as `serialport`. A browser can use Web Serial where supported. WebSocket is not a native firmware transport: a web application using WebSocket needs a gateway that forwards binary bytes between WebSocket and serial. The gateway must not convert frames to text or assume that one transport chunk is one frame.

## 2. Numeric representation

- Multi-byte integers and IEEE-754 `float32` values are little-endian.
- Integers are unsigned except `CMD_CONFIG.value` and the IMU timestamp.
- Payload structures are packed with no alignment bytes.
- Sequence arithmetic wraps modulo 256.

## 3. Link-layer frame

| Offset | Field | Size | Description |
| ---: | --- | ---: | --- |
| 0 | `SOF` | 1 | Always `0xAA` |
| 1 | `Type` | 1 | Application message type |
| 2 | `Seq` | 1 | Sender-owned sequence number |
| 3 | `Length` | 2 | Length of the **stuffed** payload, little-endian |
| 5 | `Payload` | `Length` | Stuffed payload bytes |
| `5 + Length` | `CRC16` | 2 | CRC, little-endian |

Total wire length is `7 + Length`. `Length` is not the decoded application-payload length.

### 3.1 Limits

| Item | Maximum |
| --- | ---: |
| Decoded payload | 256 bytes |
| Stuffed payload (`Length`) | 512 bytes |
| Complete valid frame | 519 bytes |

### 3.2 Payload byte-stuffing

Only payload bytes are escaped. The header and CRC are never escaped.

| Decoded byte | Bytes on the wire |
| --- | --- |
| `AA` | `1B 8A` |
| `1B` | `1B 3B` |
| Other | Unchanged |

To decode, replace every `1B X` pair with `X ^ 0x20`. A trailing `1B`, or a decoded payload exceeding 256 bytes, is invalid.

Because CRC bytes are not escaped, a CRC byte may equal `0xAA`. Use the declared length to find a candidate frame's end; do not restart at every `0xAA` inside an incomplete candidate.

### 3.3 CRC

CRC-16/CCITT-FALSE parameters:

| Parameter | Value |
| --- | --- |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input/output reflection | No |
| Final XOR | `0x0000` |
| Wire byte order | Little-endian |

CRC input is the exact wire bytes:

```text
Type || Seq || LengthLow || LengthHigh || StuffedPayload
```

`SOF` and the CRC bytes are excluded.

### 3.4 Encoding examples

Heartbeat, sequence 0, empty payload:

```text
AA 00 00 00 00 C0 84
```

Motor command for `left_mps = 0.25`, `right_mps = -0.25`, sequence 0:

```text
decoded payload = 00 00 80 3E 00 00 80 BE
frame           = AA 01 00 08 00 00 00 80 3E 00 00 80 BE 2C 87
```

### 3.5 Stream parsing

1. Discard bytes before the first `0xAA`.
2. Wait for the 5-byte prefix, then read `Length`.
3. If `Length > 512`, resume scanning one byte after that `SOF`.
4. Wait for all `7 + Length` bytes.
5. Validate CRC before unescaping.
6. Unescape and validate decoded length.
7. Deliver `{ type, seq, payload }`, remove the complete frame, and continue.

The firmware NACKs complete frames with bad CRC or escaping. Malformed streams where no trustworthy sequence can be recovered may be silently discarded; hosts need response timeouts.

## 4. Sequences, ACKs, and NACKs

The host owns one shared 8-bit sequence counter for all commands. Increment it for each request and do not reuse a value while it is in flight. The firmware owns a separate sequence counter shared by all telemetry types. Telemetry is unsolicited and is not acknowledged. Gaps can indicate skipped or lost telemetry, although wraparound is normal.

| Response | Type | Decoded payload |
| --- | ---: | --- |
| ACK | `0x7E` | request `Seq` (1 byte) |
| NACK | `0x7F` | request `Seq`, error code (2 bytes) |

The response header `Seq` also equals the request sequence. Require header and payload sequences to agree, then match by sequence. Responses do not contain the original command type. Commands are not specified as idempotent, so automatic retries require care.

### 4.1 Error codes

| Value | Name | Meaning |
| ---: | --- | --- |
| `0x01` | `ERR_CRC` | Link CRC mismatch |
| `0x02` | `ERR_LEN` | Link or application payload length invalid |
| `0x03` | `ERR_FORMAT` / `ERR_TYPE` | Bad escaping, or unknown application type |
| `0x04` | `ERR_CFG` | Unknown configuration key |
| `0x05` | `ERR_RANGE` | Value rejected as out of range |

Value `0x03` has two names in the firmware layers. For a CRC-valid host frame it normally means unknown message type; the link parser uses it for malformed escaping.

## 5. Message summary

| Direction | Type | Name | Payload bytes | Response |
| --- | ---: | --- | ---: | --- |
| Host → robot | `0x00` | `HEARTBEAT` | 0 | ACK |
| Host → robot | `0x01` | `CMD_MOTOR` | 8 | ACK/NACK |
| Host → robot | `0x02` | `CMD_SERVO` | 3 | ACK/NACK |
| Robot → host | `0x03` | `TELEMETRY_BATTERY` | 22 | None |
| Robot → host | `0x04` | `TELEMETRY_IMU` | 62 | None |
| Robot → host | `0x05` | `TELEMETRY_DRIVE_STATE` | 20 | None |
| Host → robot | `0x10` | `CMD_CONFIG` | 5 | ACK/NACK |
| Host → robot | `0x11` | `SET_TIME` | 8 | ACK/NACK |
| Response | `0x7E` | `ACK` | 1 | None |
| Response | `0x7F` | `NACK` | 2 | None |

Sending any non-command type to the firmware application handler produces `ERR_TYPE`.

## 6. Host commands

### 6.1 `HEARTBEAT` (`0x00`)

Payload is empty. Current firmware ACKs without checking its length, but clients must send an empty payload for forward compatibility.

### 6.2 `CMD_MOTOR` (`0x01`)

| Offset | Field | Type | Unit |
| ---: | --- | --- | --- |
| 0 | `left_mps` | `float32` | m/s |
| 4 | `right_mps` | `float32` | m/s |

Both values must be finite and in `-0.5..+0.5 m/s` with the current build; otherwise the response is `ERR_RANGE`. This limit is the compile-time `DIFF_DRIVE_MAX_SPEED_MPS` setting, and `0.5 m/s` maps to 100% PWM. Clients should treat the advertised range as firmware-version/configuration dependent.

### 6.3 `CMD_SERVO` (`0x02`)

| Offset | Field | Type | Unit |
| ---: | --- | --- | --- |
| 0 | `channel` | `uint8` | channel number |
| 1 | `pulse_us` | `uint16` | microseconds |

Current firmware validates only the 3-byte length and ACKs; it does not actuate servo hardware or validate values. ACK is not proof of physical movement.

### 6.4 `CMD_CONFIG` (`0x10`)

| Offset | Field | Type |
| ---: | --- | --- |
| 0 | `key` | `uint8` |
| 1 | `value` | `int32` |

| Key | Name | Accepted value/effect |
| ---: | --- | --- |
| `0x01` | `TELEM_ENABLE` | `0` disables all telemetry; nonzero enables |
| `0x02` | `TELEM_RATE_MS` | `10..5000`; battery and drive period in ms |
| `0x03` | `TELEM_MASK` | 32-bit mask below |
| `0x04` | `TELEM_TIMEOUT_MS` | Reserved; currently returns `ERR_CFG` |

Although encoded signed, `TELEM_MASK` uses the same 32 bits as `uint32`. Thus `-1` enables all defined bits.

| Bit | Value | Stream |
| ---: | ---: | --- |
| 0 | `0x01` | Battery |
| 1 | `0x02` | IMU |
| 2 | `0x04` | Reserved robot state (not emitted) |
| 3 | `0x08` | Drive state |

Both global enable and the relevant mask bit must be set. Defaults are enabled, 20 ms periodic interval, and all mask bits set. Global enable is persisted; rate and mask currently are not.

Enable telemetry, sequence 7:

```text
payload = 01 01 00 00 00
frame   = AA 10 07 05 00 01 01 00 00 00 D6 29
```

### 6.5 `SET_TIME` (`0x11`)

The payload is one little-endian `uint64` Unix timestamp in whole UTC seconds.
The firmware sets its system clock with zero fractional microseconds, calculates
`UTC milliseconds - ESP32 uptime milliseconds`, retains that offset in RAM and
NVS flash, and returns ACK on success. Telemetry
timestamps remain uptime/source-relative and do not change to UTC. An invalid payload length returns `ERR_LEN`; a value above
`INT64_MAX / 1000` or a platform clock-setting failure returns `ERR_RANGE`.

## 7. Robot telemetry

Accept telemetry interleaved with command responses. Battery and drive state use `TELEM_RATE_MS`. IMU is event-driven (currently near 100 Hz). Queues retain only the newest pending sample, so values may be coalesced under load. If several kinds are ready, order is IMU, battery, drive. Do not ACK telemetry.

### 7.1 `TELEMETRY_BATTERY` (`0x03`, 22 bytes)

| Offset | Field | Type | Description |
| ---: | --- | --- | --- |
| 0 | `valid` | `uint8` | 1 valid, 0 invalid |
| 1 | `status` | `uint8` | low byte of sensor result code |
| 2 | `timestamp` | `uint32` | sensor timestamp |
| 6 | `voltage` | `float32` | volts |
| 10 | `current` | `float32` | amperes |
| 14 | `power` | `float32` | watts |
| 18 | `energy` | `float32` | sensor-reported energy |

### 7.2 `TELEMETRY_IMU` (`0x04`, 62 bytes)

| Offset | Field | Type | Unit/order |
| ---: | --- | --- | --- |
| 0 | `valid` | `uint8` | 1 valid, 0 invalid |
| 1 | `status` | `uint8` | currently 0 valid, 1 invalid |
| 2 | `timestamp_us` | `int64` | microseconds |
| 10 | `acceleration_mps2` | `float32[3]` | x, y, z; m/s² |
| 22 | `angular_velocity_rad_s` | `float32[3]` | x, y, z; rad/s |
| 34 | `magnetic_field_uT` | `float32[3]` | x, y, z; µT |
| 46 | `quaternion_wxyz` | `float32[4]` | w, x, y, z |

Use `Buffer.readBigInt64LE()` for the timestamp when exact 64-bit values matter; JavaScript `Number` cannot represent every `int64` exactly.

### 7.3 `TELEMETRY_DRIVE_STATE` (`0x05`, 20 bytes)

| Offset | Field | Type | Description |
| ---: | --- | --- | --- |
| 0 | `timestamp_ms` | `uint32` | low 32 bits of total runtime, ms |
| 4 | `linear_velocity` | `float32` | `(left + right) / 2`, m/s |
| 8 | `angular_velocity` | `float32` | `right - left`, not divided by wheel track |
| 12 | `left_velocity` | `float32` | m/s |
| 16 | `right_velocity` | `float32` | m/s |

A client requiring angular velocity in rad/s must divide the wheel-velocity difference by the robot wheel track.

## 8. Node.js reference codec

This dependency-free codec uses Node.js `Buffer`. It works above serial or binary WebSocket messages and preserves partial frames.

```js
const SOF = 0xaa, ESC = 0x1b;
const MAX_STUFFED = 512, MAX_PAYLOAD = 256;

function crc16(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let i = 0; i < 8; i++) {
      crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
      crc &= 0xffff;
    }
  }
  return crc;
}

function stuff(payload) {
  const out = [];
  for (const b of payload)
    b === SOF || b === ESC ? out.push(ESC, b ^ 0x20) : out.push(b);
  return Buffer.from(out);
}

function unstuff(input) {
  const out = [];
  for (let i = 0; i < input.length; i++) {
    let b = input[i];
    if (b === ESC) {
      if (++i === input.length) throw new Error("trailing escape");
      b = input[i] ^ 0x20;
    }
    if (out.push(b) > MAX_PAYLOAD) throw new Error("payload too long");
  }
  return Buffer.from(out);
}

function encodeFrame(type, seq, payload = Buffer.alloc(0)) {
  payload = Buffer.from(payload);
  if (payload.length > MAX_PAYLOAD) throw new RangeError("payload too long");
  const wirePayload = stuff(payload);
  const body = Buffer.alloc(4 + wirePayload.length);
  body[0] = type & 0xff;
  body[1] = seq & 0xff;
  body.writeUInt16LE(wirePayload.length, 2);
  wirePayload.copy(body, 4);
  const frame = Buffer.alloc(1 + body.length + 2);
  frame[0] = SOF;
  body.copy(frame, 1);
  frame.writeUInt16LE(crc16(body), 1 + body.length);
  return frame;
}

class FrameParser {
  buffer = Buffer.alloc(0);
  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, Buffer.from(chunk)]);
    const frames = [];
    for (;;) {
      const start = this.buffer.indexOf(SOF);
      if (start < 0) { this.buffer = Buffer.alloc(0); break; }
      if (start) this.buffer = this.buffer.subarray(start);
      if (this.buffer.length < 5) break;
      const length = this.buffer.readUInt16LE(3);
      if (length > MAX_STUFFED) { this.buffer = this.buffer.subarray(1); continue; }
      const total = 7 + length;
      if (this.buffer.length < total) break;
      const frame = this.buffer.subarray(0, total);
      this.buffer = this.buffer.subarray(total);
      if (frame.readUInt16LE(5 + length) !== crc16(frame.subarray(1, 5 + length))) continue;
      try {
        frames.push({ type: frame[1], seq: frame[2], payload: unstuff(frame.subarray(5, 5 + length)) });
      } catch { /* discard malformed complete frame */ }
    }
    return frames;
  }
}

function motorPayload(left, right) {
  const b = Buffer.alloc(8); b.writeFloatLE(left, 0); b.writeFloatLE(right, 4); return b;
}
function servoPayload(channel, pulseUs) {
  const b = Buffer.alloc(3); b.writeUInt8(channel, 0); b.writeUInt16LE(pulseUs, 1); return b;
}
function configPayload(key, value) {
  const b = Buffer.alloc(5); b.writeUInt8(key, 0); b.writeInt32LE(value, 1); return b;
}

// port.write(encodeFrame(0x01, seq, motorPayload(0.25, -0.25)));
```

In a browser use `Uint8Array`/`DataView`, passing `true` for little-endian multi-byte reads and writes. For WebSocket set `socket.binaryType = "arraybuffer"`.

## 9. Compatibility and source of truth

Receivers should ignore unknown telemetry types after frame validation and require the documented payload size before decoding known types.

Matching implementations are:

- `main/modules/link/framed_link.c` — framing, escaping, limits, CRC;
- `main/modules/ros2/ros2_msgs.c` — commands and telemetry;
- `utils/protocol_common.py` — Python host reference.

Protocol changes should update those implementations, their tests, and this document together.
