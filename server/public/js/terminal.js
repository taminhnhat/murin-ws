(function () {
  const socket = window.Murin?.robotSocket;
  const output = document.getElementById("terminal-output");
  const terminalWindow = document.getElementById("terminal-window");
  const form = document.getElementById("terminal-form");
  const input = document.getElementById("terminal-input");
  const status = document.getElementById("terminal-status");
  const history = [];
  const usesTouchInput = window.matchMedia("(pointer: coarse)").matches;
  let historyIndex = 0;
  let consoleConnected = false;
  let selectedShortcutIndex = 0;
  let activeMonitor = null;
  const shortcuts = Array.from(
    document.querySelectorAll(".terminal-toolbar button"),
  );
  const maxOutputCharacters = 100000;

  if (!socket || !output || !form || !input || !status) return;

  function append(text) {
    let value = text.replace(/\x1b\[2J(?:\x1b\[H)?/g, "");
    if (value !== text) output.textContent = "";
    value = value.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "");
    value = value.replace(/shell> ?/g, "");
    output.textContent += value;
    if (output.textContent.length > maxOutputCharacters) {
      output.textContent = output.textContent.slice(-maxOutputCharacters);
    }
    terminalWindow.scrollTop = terminalWindow.scrollHeight;
  }

  function send(command) {
    const value = command.trim();
    if (!value || !socket.connected) return;
    socket.emit("console_command", value);
    history.push(value);
    historyIndex = history.length;
    input.value = "";
  }

  function selectShortcut(index) {
    if (!shortcuts.length) return;
    selectedShortcutIndex = (index + shortcuts.length) % shortcuts.length;
    shortcuts.forEach((shortcut, shortcutIndex) => {
      shortcut.classList.toggle(
        "gamepad-selected",
        shortcutIndex === selectedShortcutIndex,
      );
    });
    shortcuts[selectedShortcutIndex].scrollIntoView({
      behavior: "smooth",
      block: "nearest",
      inline: "nearest",
    });
  }

  function setActiveMonitor(monitor) {
    activeMonitor = monitor;
    shortcuts.forEach((shortcut) => {
      shortcut.classList.toggle(
        "monitor-active",
        shortcut.dataset.monitor === activeMonitor,
      );
    });
  }

  function stopMonitor() {
    if (!activeMonitor) return;
    socket.emit("console_stop_monitor");
    setActiveMonitor(null);
  }

  function activateShortcut(shortcut) {
    if (!shortcut) return;
    switch (shortcut.dataset.shortcut) {
      case "connect":
        if (!consoleConnected) {
          status.textContent = "Connecting…";
          socket.emit("console_connect");
        }
        break;
      case "command":
        if (consoleConnected) send(shortcut.dataset.command);
        break;
      case "monitor": {
        if (!consoleConnected || shortcut.dataset.monitor === activeMonitor)
          return;
        const monitor = shortcut.dataset.monitor;
        if (activeMonitor) {
          socket.emit("console_stop_monitor");
          setTimeout(() => send(`monitor ${monitor}`), 75);
        } else {
          send(`monitor ${monitor}`);
        }
        setActiveMonitor(monitor);
        break;
      }
      case "clear":
        output.textContent = "";
        if (!usesTouchInput) input.focus();
        break;
    }
  }

  function deactivateShortcut(shortcut) {
    if (!shortcut) return;
    if (shortcut.dataset.shortcut === "connect") {
      stopMonitor();
      socket.emit("console_disconnect");
    } else if (shortcut.dataset.shortcut === "monitor") {
      stopMonitor();
    }
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    send(input.value);
  });

  input.addEventListener("keydown", (event) => {
    if (event.ctrlKey && event.key.toLowerCase() === "c") {
      event.preventDefault();
      stopMonitor();
      append("^C\n");
      return;
    }
    if (event.ctrlKey && event.key.toLowerCase() === "l") {
      event.preventDefault();
      output.textContent = "";
      return;
    }
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault();
    historyIndex += event.key === "ArrowUp" ? -1 : 1;
    historyIndex = Math.max(0, Math.min(history.length, historyIndex));
    input.value = history[historyIndex] || "";
  });

  shortcuts.forEach((shortcut, index) => {
    shortcut.addEventListener("click", () => {
      selectShortcut(index);
      activateShortcut(shortcut);
    });
  });

  terminalWindow.addEventListener("click", () => {
    if (!usesTouchInput) input.focus();
  });

  socket.on("console_output", (text) => append(String(text)));
  socket.on("console_status", (state) => {
    consoleConnected = Boolean(state.connected);
    if (!consoleConnected) setActiveMonitor(null);
    status.textContent = consoleConnected
      ? "Connected"
      : state.message === "No console serial port configured"
        ? "Unavailable"
        : "Connect";
    status.title = state.message || status.textContent;
    status.classList.toggle("connected", consoleConnected);
    input.disabled = !consoleConnected;
    if (consoleConnected && !usesTouchInput) input.focus();
  });
  socket.on("connect", () => socket.emit("console_status_request"));
  window.addEventListener("gamepad-action", (event) => {
    switch (event.detail?.action) {
      case "terminal-primary":
        activateShortcut(shortcuts[selectedShortcutIndex]);
        break;
      case "disconnect-terminal":
        deactivateShortcut(shortcuts[selectedShortcutIndex]);
        break;
      case "terminal-shortcut-previous":
        selectShortcut(selectedShortcutIndex - 1);
        break;
      case "terminal-shortcut-next":
        selectShortcut(selectedShortcutIndex + 1);
        break;
    }
  });
  selectShortcut(0);
  socket.emit("console_status_request");
})();
