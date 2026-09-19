const { Server } = require("socket.io");
const { createRobotLink } = require("../utils/robot_link");
const { createConsoleLink } = require("../utils/console_link");
const { createRobotSocket } = require("../utils/robot_socket");

let io;

function init(httpServer, options = {}) {
  io = new Server(httpServer, {
    cors: {
      origin: "*",
    },
  });

  const robot = io.of("/robot");
  const system = io.of("/system");

  if (options.robotTransport === "serial") {
    createRobotLink(robot);
    createConsoleLink(robot);
  } else if (options.robotTransport === "socket") {
    createRobotSocket(robot);
  }

  robot.on("connection", (socket) => {
    console.log("Robot client connected");

    socket.on("join", ({ robotId }) => {
      socket.join(`robot:${robotId}`);
    });

    socket.on("leave", ({ robotId }) => {
      socket.leave(`robot:${robotId}`);
    });

    socket.on("goal", (msg) => {
      console.log("Navigation Goal", msg);
    });

    socket.on("estop", () => {
      console.log("Emergency Stop");
    });
  });

  system.on("connection", (socket) => {
    console.log("System client connected");

    socket.on("subscribe_logs", () => {
      socket.join("logs");
    });

    socket.on("unsubscribe_logs", () => {
      socket.leave("logs");
    });

    socket.on("shutdown", () => {
      console.log("Shutdown requested");
    });

    socket.on("reboot", () => {
      console.log("Reboot requested");
    });
  });

  return io;
}

module.exports = {
  init,
};
