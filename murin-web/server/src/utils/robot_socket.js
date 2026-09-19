"use strict";

const ROBOT_SOURCE_ROOM = "robot-source";

function finiteArray(value, length) {
  return (
    Array.isArray(value) &&
    value.length === length &&
    value.every(Number.isFinite)
  );
}

function validImu(imu) {
  return (
    imu &&
    typeof imu === "object" &&
    typeof imu.valid === "boolean" &&
    Number.isInteger(imu.status) &&
    finiteArray(imu.acceleration, 3) &&
    finiteArray(imu.angularVelocity, 3) &&
    finiteArray(imu.magneticField, 3) &&
    finiteArray(imu.quaternion, 4) &&
    ["string", "number"].includes(typeof imu.timestampUs)
  );
}

function validRobotMessage(message) {
  if (!message || typeof message !== "object") return false;
  if (message.msgType !== undefined && !Number.isInteger(message.msgType))
    return false;
  if (message.seq !== undefined && !Number.isInteger(message.seq)) return false;
  if (message.msgName !== undefined && typeof message.msgName !== "string")
    return false;
  if (
    message.rawPayload !== undefined &&
    (typeof message.rawPayload !== "string" ||
      !/^[0-9a-f]*$/i.test(message.rawPayload))
  )
    return false;
  const telemetry = message.telemetry;
  if (telemetry !== undefined && telemetry !== null) {
    if (typeof telemetry !== "object" || typeof telemetry.valid !== "boolean")
      return false;
    for (const key of ["voltage", "current", "power", "energy"]) {
      if (telemetry[key] !== undefined && !Number.isFinite(telemetry[key]))
        return false;
    }
  }
  try {
    return JSON.stringify(message).length <= 65536;
  } catch {
    return false;
  }
}

function validMotorCommand(command) {
  return (
    command &&
    Number.isFinite(command.left) &&
    Number.isFinite(command.right) &&
    Math.abs(command.left) <= 0.5 &&
    Math.abs(command.right) <= 0.5
  );
}

function validGoal(goal) {
  if (!goal || typeof goal !== "object" || Array.isArray(goal)) return false;
  try {
    return JSON.stringify(goal).length <= 4096;
  } catch {
    return false;
  }
}

class RobotSocket {
  constructor(namespace) {
    this.namespace = namespace;
    this.sources = new Set();
    this.status = { connected: false, message: "Waiting for robot source…" };
    namespace.on("connection", (socket) => this.attach(socket));
  }

  publishStatus(connected, message) {
    this.status = { connected, message };
    this.namespace.emit("imu_status", this.status);
  }

  attach(socket) {
    socket.emit("imu_status", this.status);
    socket.on("robot_source_register", (details = {}, acknowledge) => {
      if (this.sources.has(socket.id)) {
        if (typeof acknowledge === "function") acknowledge({ ok: true });
        return;
      }
      if (this.sources.size !== 0) {
        if (typeof acknowledge === "function")
          acknowledge({
            ok: false,
            message: "A robot source is already registered",
          });
        return;
      }
      this.sources.add(socket.id);
      socket.join(ROBOT_SOURCE_ROOM);
      this.publishStatus(
        false,
        "Robot source registered; waiting for hardware status",
      );
      if (typeof acknowledge === "function") acknowledge({ ok: true });
    });

    socket.on("robot_source_status", (status) => {
      if (!this.sources.has(socket.id) || !status) return;
      if (typeof status.connected !== "boolean") return;
      const message =
        typeof status.message === "string" && status.message.length <= 256
          ? status.message
          : status.connected
            ? "Robot source connected"
            : "Robot source disconnected";
      this.publishStatus(status.connected, message);
    });

    socket.on("imu", (imu) => {
      if (this.sources.has(socket.id) && validImu(imu))
        socket.broadcast.emit("imu", imu);
    });
    socket.on("robot_msg", (message) => {
      if (this.sources.has(socket.id) && validRobotMessage(message))
        socket.broadcast.emit("robot_msg", message);
    });
    socket.on("imu_status_request", () =>
      socket.emit("imu_status", this.status),
    );

    socket.on("cmd_vel", (command) => {
      if (!this.sources.has(socket.id) && validMotorCommand(command))
        this.namespace.to(ROBOT_SOURCE_ROOM).emit("cmd_vel", command);
    });
    socket.on("goal", (goal) => {
      if (!this.sources.has(socket.id) && validGoal(goal))
        this.namespace.to(ROBOT_SOURCE_ROOM).emit("goal", goal);
    });
    socket.on("estop", () => {
      if (!this.sources.has(socket.id))
        this.namespace.to(ROBOT_SOURCE_ROOM).emit("estop");
    });

    socket.on("disconnect", () => {
      if (!this.sources.delete(socket.id)) {
        this.namespace.to(ROBOT_SOURCE_ROOM).emit("estop");
        return;
      }
      if (this.sources.size === 0)
        this.publishStatus(false, "Robot source disconnected");
    });
  }
}

function createRobotSocket(namespace) {
  return new RobotSocket(namespace);
}

module.exports = {
  RobotSocket,
  createRobotSocket,
  validImu,
  validRobotMessage,
  validMotorCommand,
};
