const robotSocket = io("/robot");

window.Murin = {};

Murin.robotSocket = io("/robot");
Murin.systemSocket = io("/system");

(function () {
  const connectIcon = document.getElementById("connectIcon");
  const connectStatusText = document.getElementById("connectStatusText");
  const batteryInput = document.getElementById("battery_voltage");
  const battery_Wh = document.getElementById("battery_Wh");

  const setStatus = (connected, text) => {
    if (connectIcon) {
      connectIcon.textContent = connected ? "link" : "link_off";
    }
    if (connectStatusText) {
      connectStatusText.textContent =
        text || (connected ? "connected" : "offline");
    }
  };

  const robotSocket = window.Murin.robotSocket;
  if (robotSocket) {
    robotSocket.on("connect", () => {
      setStatus(true, "connected");
    });
    robotSocket.on("disconnect", () => setStatus(false, "offline"));
    robotSocket.on("connect_error", () => setStatus(false, "connection error"));
    robotSocket.on("robot_msg", (msg) => {
      const label =
        msg && (msg.msgName || msg.decoded || msg.rawPayload)
          ? msg.msgName || msg.decoded || msg.rawPayload
          : "robot message";
      setStatus(true, label);

      if (
        batteryInput &&
        msg &&
        msg.telemetry &&
        Number.isFinite(msg.telemetry.voltage)
      ) {
        batteryInput.value = `${msg.telemetry.voltage.toFixed(2)} V`;
      }
      if (
        battery_Wh &&
        msg &&
        msg.telemetry &&
        Number.isFinite(msg.telemetry.energy)
      ) {
        battery_Wh.value = `${msg.telemetry.energy.toFixed(2)} Wh`;
      }
    });
    setStatus(false, "offline");
  } else {
    setStatus(false, "socket unavailable");
  }
})();
