"use strict";

const fs = require("fs");
const path = require("path");
const { SerialPort } = require("serialport");

const SOF = 0xaa;
const ESCAPE = 0x1b;
const ESCAPE_XOR = 0x20;
const MAX_STUFFED_PAYLOAD = 4096;
const MSG = Object.freeze({
  HEARTBEAT: 0x00,
  CMD_MOTOR: 0x01,
  CMD_SERVO: 0x02,
  TELEMETRY_BATTERY: 0x03,
  TELEMETRY_IMU: 0x04,
  CMD_CONFIG: 0x10,
  SET_TIME: 0x11,
  DATA_IMU: 0x20,
  DATA_ENC: 0x21,
  ACK: 0x7e,
  NACK: 0x7f,
});

const TYPE_NAMES = new Map(
  Object.entries(MSG).map(([name, value]) => [value, name]),
);

function crc16(data) {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
      crc &= 0xffff;
    }
  }
  return crc;
}

function unstuff(data) {
  const payload = [];
  for (let index = 0; index < data.length; index += 1) {
    let value = data[index];
    if (value === ESCAPE && index + 1 < data.length)
      value = data[++index] ^ ESCAPE_XOR;
    payload.push(value);
  }
  return Buffer.from(payload);
}

function stuff(data) {
  const payload = [];
  for (const byte of data) {
    if (byte === SOF || byte === ESCAPE) {
      payload.push(ESCAPE, byte ^ ESCAPE_XOR);
    } else {
      payload.push(byte);
    }
  }
  return Buffer.from(payload);
}

function buildFrame(msgType, seq, payload = Buffer.alloc(0)) {
  const stuffedPayload = stuff(payload);
  const crcData = Buffer.alloc(4 + stuffedPayload.length);
  crcData.writeUInt8(msgType, 0);
  crcData.writeUInt8(seq, 1);
  crcData.writeUInt16LE(stuffedPayload.length, 2);
  stuffedPayload.copy(crcData, 4);

  const frame = Buffer.alloc(1 + crcData.length + 2);
  frame.writeUInt8(SOF, 0);
  crcData.copy(frame, 1);
  frame.writeUInt16LE(crc16(crcData), 1 + crcData.length);
  return frame;
}

function encodeMotorCommand(left, right) {
  const payload = Buffer.alloc(8);
  payload.writeFloatLE(left, 0);
  payload.writeFloatLE(right, 4);
  return payload;
}

function encodeSetTime(unixSeconds = BigInt(Math.floor(Date.now() / 1000))) {
  const timestamp = BigInt(unixSeconds);
  if (timestamp < 0n || timestamp > 0x7fffffffffffffffn)
    throw new RangeError("Unix timestamp must fit in a signed 64-bit integer");
  const payload = Buffer.alloc(8);
  payload.writeBigUInt64LE(timestamp);
  return payload;
}

class FrameParser {
  constructor() {
    this.buffer = Buffer.alloc(0);
  }

  feed(data) {
    this.buffer = Buffer.concat([this.buffer, data]);
    const frames = [];
    while (true) {
      const start = this.buffer.indexOf(SOF);
      if (start < 0) {
        this.buffer = Buffer.alloc(0);
        break;
      }
      if (start > 0) this.buffer = this.buffer.subarray(start);
      if (this.buffer.length < 7) break;

      const length = this.buffer.readUInt16LE(3);
      if (length > MAX_STUFFED_PAYLOAD) {
        this.buffer = this.buffer.subarray(1);
        continue;
      }
      const total = 7 + length;
      if (this.buffer.length < total) break;

      const crcData = this.buffer.subarray(1, 5 + length);
      const expectedCrc = this.buffer.readUInt16LE(5 + length);
      const msgType = this.buffer.readUInt8(1);
      const seq = this.buffer.readUInt8(2);
      const payload = unstuff(this.buffer.subarray(5, 5 + length));
      this.buffer = this.buffer.subarray(total);
      if (crc16(crcData) === expectedCrc)
        frames.push({ msgType, seq, payload });
    }
    return frames;
  }
}

function parseBattery(payload) {
  if (payload.length !== 22) return null;
  return {
    valid: payload.readUInt8(0) !== 0,
    status: payload.readUInt8(1),
    timestampMs: payload.readUInt32LE(2),
    voltage: payload.readFloatLE(6),
    current: payload.readFloatLE(10),
    power: payload.readFloatLE(14),
    energy: payload.readFloatLE(18),
  };
}

function parseImu(payload) {
  if (payload.length !== 62) return null;
  return {
    valid: payload.readUInt8(0) !== 0,
    status: payload.readUInt8(1),
    timestampUs: payload.readBigInt64LE(2).toString(),
    acceleration: [
      payload.readFloatLE(10),
      payload.readFloatLE(14),
      payload.readFloatLE(18),
    ],
    angularVelocity: [
      payload.readFloatLE(22),
      payload.readFloatLE(26),
      payload.readFloatLE(30),
    ],
    magneticField: [
      payload.readFloatLE(34),
      payload.readFloatLE(38),
      payload.readFloatLE(42),
    ],
    quaternion: [
      payload.readFloatLE(46),
      payload.readFloatLE(50),
      payload.readFloatLE(54),
      payload.readFloatLE(58),
    ],
  };
}

function parserConfigPort() {
  try {
    const config = fs.readFileSync(path.join(__dirname, "config.yaml"), "utf8");
    return config.match(/^port\s*:\s*["']?([^\s"']+)/m)?.[1];
  } catch (error) {
    if (error.code !== "ENOENT")
      console.warn(`[ROBOT] Cannot read config.yaml: ${error.message}`);
    return undefined;
  }
}

class RobotLink {
  constructor(namespace, options = {}) {
    this.namespace = namespace;
    this.port = options.port || process.env.USB_PORT || parserConfigPort();
    this.baudRate =
      Number(options.baudRate || process.env.USB_BAUDRATE) || 2000000;
    this.parser = new FrameParser();
    this.sequence = 0;
    this.serial = null;
    this.retryTimer = null;
    this.stopped = false;
    this.status = {
      connected: false,
      message: "Waiting for robot serial connection…",
    };
    namespace.on("connection", (client) => {
      client.emit("imu_status", this.status);
      client.on("imu_status_request", () =>
        client.emit("imu_status", this.status),
      );
      client.on("cmd_vel", (command) => this.handleMotorCommand(command));
      client.on("disconnect", () => this.sendMotorCommand(0, 0));
    });
  }

  handleMotorCommand(command) {
    const left = Number(command?.left);
    const right = Number(command?.right);
    if (
      !Number.isFinite(left) ||
      !Number.isFinite(right) ||
      Math.abs(left) > 0.5 ||
      Math.abs(right) > 0.5
    ) {
      console.warn("[ROBOT] Ignoring invalid cmd_vel", command);
      return false;
    }
    return this.sendMotorCommand(left, right);
  }

  sendMotorCommand(left, right) {
    if (!this.serial?.isOpen) return false;
    const frame = buildFrame(
      MSG.CMD_MOTOR,
      this.sequence,
      encodeMotorCommand(left, right),
    );
    this.sequence = (this.sequence + 1) & 0xff;
    this.serial.write(frame, (error) => {
      if (error)
        console.error(`[ROBOT] Motor command failed: ${error.message}`);
    });
    return true;
  }

  syncTime() {
    if (!this.serial?.isOpen) return false;
    const frame = buildFrame(MSG.SET_TIME, this.sequence, encodeSetTime());
    this.sequence = (this.sequence + 1) & 0xff;
    this.serial.write(frame, (error) => {
      if (error) console.error(`[ROBOT] Time sync failed: ${error.message}`);
    });
    return true;
  }

  publishStatus(connected, message) {
    this.status = { connected, message };
    this.namespace.emit("imu_status", this.status);
  }

  start() {
    this.stopped = false;
    this.open();
    return this;
  }

  open() {
    if (this.stopped || this.serial?.isOpen) return;
    if (!this.port) {
      this.publishStatus(false, "No robot serial port configured");
      return;
    }
    try {
      this.serial = new SerialPort({
        path: this.port,
        baudRate: this.baudRate,
      });
    } catch (error) {
      this.handleDisconnect(error);
      return;
    }
    this.serial.on("open", () => {
      console.log(`[ROBOT] Listening on ${this.port} @ ${this.baudRate}`);
      this.publishStatus(true, `Connected to ${this.port}`);
      this.syncTime();
    });
    this.serial.on("data", (data) => this.handleData(data));
    this.serial.on("error", (error) => this.handleDisconnect(error));
    this.serial.on("close", () =>
      this.handleDisconnect(new Error("Serial port closed")),
    );
  }

  handleData(data) {
    for (const frame of this.parser.feed(data)) {
      const msgName =
        TYPE_NAMES.get(frame.msgType) ||
        `0x${frame.msgType.toString(16).padStart(2, "0")}`;
      if (frame.msgType === MSG.TELEMETRY_IMU) {
        const imu = parseImu(frame.payload);
        if (imu) this.namespace.emit("imu", imu);
      }
      const telemetry =
        frame.msgType === MSG.TELEMETRY_BATTERY
          ? parseBattery(frame.payload)
          : null;
      this.namespace.emit("robot_msg", {
        msgType: frame.msgType,
        msgName,
        seq: frame.seq,
        rawPayload: frame.payload.toString("hex"),
        telemetry,
      });
    }
  }

  handleDisconnect(error) {
    if (this.stopped) return;
    console.error(`[ROBOT] ${error.message}`);
    this.publishStatus(false, error.message);
    if (!this.retryTimer) {
      this.retryTimer = setTimeout(() => {
        this.retryTimer = null;
        this.serial = null;
        this.open();
      }, 2000);
    }
  }

  stop() {
    this.stopped = true;
    if (this.retryTimer) clearTimeout(this.retryTimer);
    if (this.serial?.isOpen) this.serial.close();
  }
}

function createRobotLink(namespace, options) {
  return new RobotLink(namespace, options).start();
}

module.exports = {
  MSG,
  FrameParser,
  parseBattery,
  parseImu,
  buildFrame,
  encodeMotorCommand,
  encodeSetTime,
  RobotLink,
  createRobotLink,
};
