let haveEvents = "GamepadEvent" in window;
let haveWebkitEvents = "WebKitGamepadEvent" in window;
let controllers = [];
let rAF =
  window.mozRequestAnimationFrame ||
  window.webkitRequestAnimationFrame ||
  window.requestAnimationFrame;

let digitalTrigger = [];
let analogTrigger = [];
let linear_vel_x = 0;
let linear_vel_y = 0;
let angular_vel = 0;
const joyDeadzone = 0.01;
const GAMEPAD_BUTTON_NAMES = Object.freeze({
  0: "A",
  1: "B",
  2: "X",
  3: "Y",
  4: "LB",
  5: "RB",
  6: "LT",
  7: "RT",
  8: "Back",
  9: "Start",
  10: "LS",
  11: "RS",
  12: "D-pad Up",
  13: "D-pad Down",
  14: "D-pad Left",
  15: "D-pad Right",
  16: "Home",
});
const maxMotorVelocity = 0.5;
const motorCommandIntervalMs = 50;
let lastMotorCommandTime = 0;
let zeroMotorCommandSent = false;

let vibrationEnabled = false;
let buttonDebounceEnabled = false;

const gamepadState = {
  axes: [],
  buttons: [],
  buttonCounters: [],
  buttonTriggers: [],
};

const HAPTIC_STATE = {
  IDLE: 0,
  ACTIVE: 1,
};

let hapticState = HAPTIC_STATE.IDLE;
let lastLT = 0;
let lastRT = 0;
let lastHapticTime = 0;

function updateHaptics(gp) {
  const lt = gp.buttons[6].value;
  const rt = gp.buttons[7].value;
  const now = performance.now();
  const active = lt > 0.01 || rt > 0.01;
  switch (hapticState) {
    case HAPTIC_STATE.IDLE:
      if (active) {
        hapticState = HAPTIC_STATE.ACTIVE;
        sendHaptic(gp, lt, rt);
        lastLT = lt;
        lastRT = rt;
        lastHapticTime = now;
      }
      break;

    case HAPTIC_STATE.ACTIVE:
      if (!active) {
        gp.vibrationActuator?.reset();
        hapticState = HAPTIC_STATE.IDLE;
        lastLT = 0;
        lastRT = 0;
        break;
      }
      const changed =
        Math.abs(lt - lastLT) > 0.02 || Math.abs(rt - lastRT) > 0.02;
      const timeout = now - lastHapticTime >= 50;
      // if (changed && timeout) {
      if (timeout) {
        sendHaptic(gp, lt, rt);
        lastLT = lt;
        lastRT = rt;
        lastHapticTime = now;
      }
      break;
  }
}

function sendHaptic(gp, lt, rt) {
  if (!gp.vibrationActuator) return;

  gp.vibrationActuator.playEffect("trigger-rumble", {
    duration: 100,
    startDelay: 0,
    leftTrigger: lt,
    rightTrigger: rt,
    strongMagnitude: (lt + rt) / 2,
    weakMagnitude: (lt + rt) / 2,
  });
}

function setTelemetry(prefix, value) {
  const abs =
    Math.abs(value) < joyDeadzone
      ? "0.00"
      : Math.abs(value).toFixed(2).split(".");

  document.getElementById(`${prefix}_sign`).textContent =
    value >= 0 ? "  " : "-";

  document.getElementById(`${prefix}_int`).textContent = abs[0].padStart(
    1,
    "0",
  );

  document.getElementById(`${prefix}_frac`).textContent = abs[1];
}

function createGamepadUI(gamepad) {
  const axesContainer = document.getElementById("gamepad-axes");
  const buttonsContainer = document.getElementById("gamepad-buttons");

  axesContainer.innerHTML = "";
  buttonsContainer.innerHTML = "";

  // Axes
  gamepad.axes.forEach((_, index) => {
    const item = document.createElement("div");

    item.className = "gamepad-item";

    item.innerHTML = `
            <div class="axis-item">
                <div class="axis-bar">
                    <div class="axis-fill" id="axis${index}-bar"></div>
                </div>

                <div class="axis-info">
                    <div class="axis-name">AXIS ${index}</div>
                    <div class="axis-value" id="axis${index}-value">---</div>
                </div>
            </div>
        `;

    axesContainer.appendChild(item);
  });

  // Buttons
  gamepad.buttons.forEach((_, index) => {
    const item = document.createElement("div");

    item.className = "gamepad-item";

    item.innerHTML = `
            <div class="axis-item">
                <div class="axis-bar">
                    <div class="button-fill" id="button${index}-bar"></div>
                </div>

                <div class="axis-info">
                    <div class="axis-name">${GAMEPAD_BUTTON_NAMES[index] || `B${index}`}</div>
                    <div class="axis-value" id="button${index}-value">---</div>
                </div>
            </div>
        `;

    buttonsContainer.appendChild(item);
  });
}

function toggleVibration() {
  vibrationEnabled = !vibrationEnabled;
  document.getElementById("os-gamepad-btn").style.backgroundColor =
    vibrationEnabled ? "#33b887" : "#ff9028";
}

function updateState() {
  document.getElementById("gamepad-timestamp").textContent =
    gamepadState.timestamp.toFixed(5);
  gamepadState.buttons.forEach((button, index) => {
    if (button.pressed) {
      if (gamepadState.buttonCounters[index] !== Infinity) {
        gamepadState.buttonCounters[index]++;
      }
    } else {
      gamepadState.buttonCounters[index] = 0;
      gamepadState.buttonTriggers[index] = false;
    }
    if (
      gamepadState.buttonCounters[index] !== Infinity &&
      gamepadState.buttonCounters[index] >= (buttonDebounceEnabled ? 5 : 1)
    ) {
      gamepadState.buttonTriggers[index] = true;
      gamepadState.buttonCounters[index] = Infinity;
    }
    document.getElementById(`button${index}-value`).textContent =
      button.value.toFixed(3);
    document.getElementById(`button${index}-bar`).style.height =
      `${button.value * 100}%`;
  });
  gamepadState.axes.forEach((axis, index) => {
    document.getElementById(`axis${index}-value`).textContent = axis.toFixed(3);
    document.getElementById(`axis${index}-bar`).style.height =
      `${((axis + 1) / 2) * 100}%`;
  });
  document.getElementById("left-trigger-value").textContent =
    gamepadState.buttons[6].value.toFixed(2);
  document.getElementById("right-trigger-value").textContent =
    gamepadState.buttons[7].value.toFixed(2);
  document.getElementById("left-trigger-bar").style.height =
    `${gamepadState.buttons[6].value * 100}%`;
  document.getElementById("right-trigger-bar").style.height =
    `${gamepadState.buttons[7].value * 100}%`;
  document.getElementById("right_joystick_x").textContent =
    gamepadState.axes[3].toFixed(2);
  document.getElementById("right_joystick_y").textContent =
    gamepadState.axes[2].toFixed(2);
  setTelemetry("angular", gamepadState.axes[0]);
  setTelemetry("linear", -gamepadState.axes[1]);
  setTelemetry("velx", gamepadState.axes[0]);
  setTelemetry("vely", -gamepadState.axes[1]);

  const now = performance.now();
  if (now - lastMotorCommandTime >= motorCommandIntervalMs) {
    const leftTrigger = gamepadState.buttons[6]?.value || 0;
    const rightTrigger = gamepadState.buttons[7]?.value || 0;
    const leftVelocity =
      leftTrigger > joyDeadzone ? leftTrigger * maxMotorVelocity : 0;
    const rightVelocity =
      rightTrigger > joyDeadzone ? rightTrigger * maxMotorVelocity : 0;
    const socket = window.Murin?.robotSocket;

    const stopped = leftVelocity === 0 && rightVelocity === 0;
    if (socket?.connected && (!stopped || !zeroMotorCommandSent)) {
      socket.emit("cmd_vel", { left: leftVelocity, right: rightVelocity });
      zeroMotorCommandSent = stopped;
    }
    lastMotorCommandTime = now;
  }

  if (
    gamepadState.buttonTriggers[0] &&
    document.getElementById("page-terminal")?.classList.contains("active")
  ) {
    gamepadState.buttonTriggers[0] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "terminal-primary", button: "A", index: 0 },
      }),
    );
  }
  if (
    gamepadState.buttonTriggers[1] &&
    document.getElementById("page-terminal")?.classList.contains("active")
  ) {
    gamepadState.buttonTriggers[1] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "disconnect-terminal", button: "B", index: 1 },
      }),
    );
  }
  const terminalPageActive = document
    .getElementById("page-terminal")
    ?.classList.contains("active");
  if (
    terminalPageActive &&
    (gamepadState.buttonTriggers[12] || gamepadState.buttonTriggers[14])
  ) {
    gamepadState.buttonTriggers[12] = false;
    gamepadState.buttonTriggers[14] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "terminal-shortcut-previous" },
      }),
    );
  }
  if (
    terminalPageActive &&
    (gamepadState.buttonTriggers[13] || gamepadState.buttonTriggers[15])
  ) {
    gamepadState.buttonTriggers[13] = false;
    gamepadState.buttonTriggers[15] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "terminal-shortcut-next" },
      }),
    );
  }
  if (gamepadState.buttonTriggers[4]) {
    gamepadState.buttonTriggers[4] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "switch-page-left" },
      }),
    );
  }
  if (gamepadState.buttonTriggers[5]) {
    gamepadState.buttonTriggers[5] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "switch-page-right" },
      }),
    );
  }
  if (gamepadState.buttonTriggers[8]) {
    gamepadState.buttonTriggers[8] = false;
    toggleVibration();
  }
  if (gamepadState.buttonTriggers[9]) {
    gamepadState.buttonTriggers[9] = false;
    window.dispatchEvent(
      new CustomEvent("gamepad-action", {
        detail: { action: "switch-page-settings" },
      }),
    );
  }
}

let gamepadIndex = null;

function connectHandler(e) {
  const gamepad = e.gamepad;
  gamepadIndex = e.gamepad.index;
  document.getElementById("gamepad-name").textContent = e.gamepad.id;
  document.getElementById("connectIcon").textContent = "link";

  document.getElementById("gamepad-name").textContent = gamepad.id;
  document.getElementById("gamepad-index").textContent = gamepad.index;
  document.getElementById("gamepad-connected").textContent = "Yes";
  document.getElementById("gamepad-mapping").textContent =
    gamepad.mapping || "none";
  document.getElementById("gamepad-vibration").textContent =
    gamepad.vibrationActuator ? "Yes" : "No";
  createGamepadUI(gamepad);
}

function disconnectHandler(e) {
  if (gamepadIndex === e.gamepad.index) {
    const socket = window.Murin?.robotSocket;
    if (socket?.connected && !zeroMotorCommandSent) {
      socket.emit("cmd_vel", { left: 0, right: 0 });
      zeroMotorCommandSent = true;
    }
    gamepadIndex = null;
    document.getElementById("gamepad-name").textContent = "Disconnected";
    document.getElementById("connectIcon").textContent = "link_off";
  }
}

function gamepadLoop() {
  if (gamepadIndex !== null) {
    const gp = navigator.getGamepads()[gamepadIndex];

    if (gp) {
      gamepadState.axes = gp.axes;
      gamepadState.buttons = gp.buttons;
      if (gamepadState.buttonCounters.length !== gp.buttons.length) {
        gamepadState.buttonCounters = new Array(gp.buttons.length).fill(0);
        gamepadState.buttonTriggers = new Array(gp.buttons.length).fill(false);
      }
      gamepadState.timestamp = gp.timestamp;
      // if (gp.vibrationActuator) updateTriggerVibration(gp);
      // Haptics
      if (vibrationEnabled) updateHaptics(gp);
    }
    updateState();
  }
  requestAnimationFrame(gamepadLoop);
}

requestAnimationFrame(gamepadLoop);
window.addEventListener("gamepadconnected", connectHandler);
window.addEventListener("gamepaddisconnected", disconnectHandler);
document
  .getElementById("os-gamepad-btn")
  .addEventListener("click", toggleVibration);
