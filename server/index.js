"use strict";

const path = require("path");
require("dotenv").config({ path: path.join(__dirname, ".env") });

const robotTransport = process.env.ROBOT_TRANSPORT?.trim().toLowerCase();
if (!["serial", "socket"].includes(robotTransport)) {
  console.error("[STARTUP] ROBOT_TRANSPORT must be serial or socket.");
  process.exit(1);
}

const missingPorts = [];
if (robotTransport === "serial" && !process.env.USB_PORT?.trim()) {
  missingPorts.push("USB_PORT");
}
if (robotTransport === "serial" && !process.env.CONSOLE_PORT?.trim()) {
  missingPorts.push("CONSOLE_PORT");
}
if (missingPorts.length) {
  console.error(
    `[STARTUP] Set ${missingPorts.join(" and ")} in server/.env before starting the server.`,
  );
  process.exit(1);
}

const http = require("http");
const app = require("./src/app");
const socket = require("./src/socket");

const port = Number(process.env.HTTP_PORT) || 9091;
const server = http.createServer(app);

socket.init(server, { robotTransport });

server.listen(port, () => {
  console.log(`Robot server running at http://localhost:${port}/`);
  console.log(
    robotTransport === "serial"
      ? "Robot transport: direct ESP32 serial"
      : "Robot transport: Socket.IO",
  );
});
