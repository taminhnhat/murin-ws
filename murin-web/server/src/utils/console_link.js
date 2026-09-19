"use strict";

const fs = require("fs");
const path = require("path");
const { SerialPort } = require("serialport");

function parseConsoleConfig() {
  try {
    const config = fs.readFileSync(path.join(__dirname, "config.yaml"), "utf8");
    return {
      port: config.match(/^console_port\s*:\s*["']?([^\s"']+)/m)?.[1],
      baudRate: Number(config.match(/^console_baudrate\s*:\s*(\d+)/m)?.[1]),
    };
  } catch (error) {
    if (error.code !== "ENOENT") {
      console.warn(`[CONSOLE] Cannot read config.yaml: ${error.message}`);
    }
    return {};
  }
}

class ConsoleLink {
  constructor(namespace, options = {}) {
    const config = parseConsoleConfig();
    this.namespace = namespace;
    this.port = options.port || process.env.CONSOLE_PORT || config.port;
    this.baudRate =
      Number(
        options.baudRate || process.env.CONSOLE_BAUDRATE || config.baudRate,
      ) || 115200;
    this.serial = null;
    this.connecting = false;
    this.retryTimer = null;
    this.stopped = false;
    this.status = {
      connected: false,
      message: this.port
        ? "Console disabled — open Terminal and press A to connect"
        : "No console serial port configured",
    };

    namespace.on("connection", (client) => {
      client.emit("console_status", this.status);
      client.on("console_status_request", () =>
        client.emit("console_status", this.status),
      );
      client.on("console_connect", () => this.start());
      client.on("console_disconnect", () => this.stop());
      client.on("console_command", (command) => this.sendCommand(command));
      client.on("console_stop_monitor", () => this.write("q"));
    });
  }

  publishStatus(connected, message) {
    this.status = { connected, message };
    this.namespace.emit("console_status", this.status);
  }

  start() {
    this.stopped = false;
    this.open();
    return this;
  }

  open() {
    if (this.stopped || this.connecting || this.serial?.isOpen) return;
    if (!this.port) {
      this.publishStatus(false, "No console serial port configured");
      return;
    }

    try {
      this.connecting = true;
      this.serial = new SerialPort({
        path: this.port,
        baudRate: this.baudRate,
      });
    } catch (error) {
      this.connecting = false;
      this.handleDisconnect(error);
      return;
    }
    const serial = this.serial;

    serial.on("open", () => {
      this.connecting = false;
      if (this.stopped) {
        serial.close();
        return;
      }
      console.log(`[CONSOLE] Connected to ${this.port} @ ${this.baudRate}`);
      this.publishStatus(true, `Connected to ${this.port}`);
      this.write("\r");
    });
    serial.on("data", (data) => {
      if (!this.stopped) {
        this.namespace.emit("console_output", data.toString("utf8"));
      }
    });
    serial.on("error", (error) => this.handleDisconnect(error));
    serial.on("close", () =>
      this.handleDisconnect(new Error("Console serial port closed")),
    );
  }

  sendCommand(command) {
    if (typeof command !== "string") return false;
    const value = command.trim();
    if (!value || value.length > 256 || /[^\x20-\x7e]/.test(value))
      return false;
    return this.write(`${value}\r`);
  }

  write(data) {
    if (!this.serial?.isOpen) return false;
    this.serial.write(data, (error) => {
      if (error) console.error(`[CONSOLE] Write failed: ${error.message}`);
    });
    return true;
  }

  handleDisconnect(error) {
    if (this.stopped) return;
    this.connecting = false;
    console.error(`[CONSOLE] ${error.message}`);
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
    this.serial = null;
    this.publishStatus(
      false,
      "Console disabled — open Terminal and press A to connect",
    );
  }
}

function createConsoleLink(namespace, options) {
  return new ConsoleLink(namespace, options);
}

module.exports = { ConsoleLink, createConsoleLink };
