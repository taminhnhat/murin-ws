"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const http = require("node:http");
const { once } = require("node:events");
const { Server } = require("socket.io");
const { io } = require("socket.io-client");
const { RobotSocket } = require("../src/utils/robot_socket");

test("socket transport registration, validation, telemetry and stops", async (t) => {
  const server = http.createServer();
  const sockets = new Server(server);
  const transport = new RobotSocket(sockets.of("/robot"));
  server.listen(0, "127.0.0.1");
  await once(server, "listening");
  const clients = [];
  t.after(() => {
    clients.forEach((client) => client.disconnect());
    sockets.close();
    server.close();
  });
  async function connect() {
    const client = io(`http://127.0.0.1:${server.address().port}/robot`, {
      reconnection: false,
    });
    clients.push(client);
    await once(client, "connect");
    return client;
  }
  const source = await connect();
  assert.deepEqual(
    await source.emitWithAck("robot_source_register", { name: "test" }),
    { ok: true },
  );
  assert.equal(transport.status.connected, false);
  assert.deepEqual(await source.emitWithAck("robot_source_register", {}), {
    ok: true,
  });
  const browser = await connect();
  assert.equal(
    (await browser.emitWithAck("robot_source_register", {})).ok,
    false,
  );
  let commands = 0;
  source.on("cmd_vel", () => commands++);
  browser.emit("cmd_vel", { left: "0.2", right: 0 });
  browser.emit("cmd_vel", { left: 0.6, right: 0 });
  const command = once(source, "cmd_vel");
  browser.emit("cmd_vel", { left: 0.2, right: -0.2 });
  assert.deepEqual((await command)[0], { left: 0.2, right: -0.2 });
  assert.equal(commands, 1);
  const packet = { msgType: 5, seq: 3, rawPayload: "00", telemetry: null };
  const telemetry = once(browser, "robot_msg");
  source.emit("robot_msg", packet);
  assert.deepEqual((await telemetry)[0], packet);
  const status = once(browser, "imu_status");
  source.emit("robot_source_status", { connected: true, message: "ready" });
  assert.equal((await status)[0].connected, true);
  const stop = once(source, "estop");
  browser.disconnect();
  await stop;
  const observer = await connect();
  const disconnected = once(observer, "imu_status");
  source.disconnect();
  assert.equal((await disconnected)[0].connected, false);
});
