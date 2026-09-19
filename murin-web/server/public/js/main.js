const buttons = document.querySelectorAll("header button");
const gamepadButton = document.getElementById("gamepad-button");
const pages = document.querySelectorAll(".page-item");

let activePageIndex = 0;

function reloadPage() {
  buttons.forEach((btn, idx) => {
    if (idx === activePageIndex) {
      btn.style.backgroundColor = "#efefef";
      btn.style.color = "#0e263d";
    } else {
      btn.style.backgroundColor = "#0e263d";
      btn.style.color = "#efefef";
    }
  });
  pages.forEach((page, idx) => {
    if (idx === activePageIndex) {
      page.classList.add("active");
    } else {
      page.classList.remove("active");
    }
  });
}

buttons.forEach((button, buttonIdx) => {
  button.addEventListener("click", () => {
    activePageIndex = buttonIdx;
    reloadPage();
  });
});

function formatTelemetry(value) {
  const sign = value >= 0 ? "+" : "-";
  const abs = Math.abs(value);

  return sign + abs.toFixed(2).padStart(5, "0");
}

const robotImg = document.getElementById("robot-img");
const imageContainer = document.getElementById("contorl-panel-center");

function scheduleCanvasUpdate() {
  if (window.requestAnimationFrame) {
    window.requestAnimationFrame(updateCanvas);
  } else {
    updateCanvas();
  }
}

// update image size and position
function updateCanvas() {
  if (!robotImg || !imageContainer) return;

  const canvas = document.getElementById("map-canvas");
  const overlayTopLeft = document.getElementById("overlay-top-left");
  const overlayTopRight = document.getElementById("overlay-top-right");
  const overlayBottomLeft = document.getElementById("overlay-bottom-left");
  const overlayBottomRight = document.getElementById("overlay-bottom-right");

  const containerRect = imageContainer.getBoundingClientRect();

  if (!containerRect.width || !containerRect.height) {
    overlayTopLeft.style.display = "none";
    overlayTopRight.style.display = "none";
    overlayBottomLeft.style.display = "none";
    overlayBottomRight.style.display = "none";
    return;
  }

  const naturalWidth = robotImg.naturalWidth || robotImg.width;
  const naturalHeight = robotImg.naturalHeight || robotImg.height;

  if (!naturalWidth || !naturalHeight) {
    overlayTopLeft.style.display = "none";
    overlayTopRight.style.display = "none";
    overlayBottomLeft.style.display = "none";
    overlayBottomRight.style.display = "none";
    return;
  }

  const scale = Math.min(
    containerRect.width / naturalWidth,
    containerRect.height / naturalHeight,
  );

  const canvasW = naturalWidth * scale;
  const canvasH = naturalHeight * scale;
  const displayLeft = (containerRect.width - canvasW) / 2;
  const displayTop = (containerRect.height - canvasH) / 2;

  if (canvasW < 300 || canvasH < 300) {
    overlayTopLeft.style.display = "none";
    overlayTopRight.style.display = "none";
    overlayBottomLeft.style.display = "none";
    overlayBottomRight.style.display = "none";
    return;
  }

  overlayTopLeft.style.display = "block";
  overlayTopRight.style.display = "block";
  overlayBottomLeft.style.display = "block";
  overlayBottomRight.style.display = "block";

  canvas.style.left = `${displayLeft}px`;
  canvas.style.top = `${displayTop}px`;

  canvas.width = Math.round(canvasW);
  canvas.height = Math.round(canvasH);

  canvas.style.width = `${canvasW}px`;
  canvas.style.height = `${canvasH}px`;

  overlayTopLeft.style.left = `${displayLeft + 10}px`;
  overlayTopLeft.style.top = `${displayTop + 10}px`;

  overlayTopRight.style.right = `${containerRect.width - (displayLeft + canvasW) + 10}px`;
  overlayTopRight.style.top = `${displayTop + 10}px`;

  overlayBottomLeft.style.left = `${displayLeft + 10}px`;
  overlayBottomLeft.style.bottom = `${containerRect.height - (displayTop + canvasH) + 10}px`;

  overlayBottomRight.style.right = `${containerRect.width - (displayLeft + canvasW) + 10}px`;
  overlayBottomRight.style.bottom = `${containerRect.height - (displayTop + canvasH) + 10}px`;
}

// update canvas size and position when the image is loaded
if (robotImg.complete) {
  scheduleCanvasUpdate();
} else {
  robotImg.addEventListener("load", scheduleCanvasUpdate, { once: true });
}

window.addEventListener("resize", scheduleCanvasUpdate);
window.addEventListener("load", () => {
  reloadPage();
  scheduleCanvasUpdate();
});

if (typeof ResizeObserver !== "undefined") {
  const resizeObserver = new ResizeObserver(() => scheduleCanvasUpdate());
  resizeObserver.observe(robotImg);
  resizeObserver.observe(imageContainer);
}

var fullscreenFlag = false;

function fullscreenToggle() {
  var elem = document.documentElement;

  fullscreenFlag = !fullscreenFlag;
  if (fullscreenFlag) {
    if (elem.requestFullscreen) {
      elem.requestFullscreen();
    } else if (elem.webkitRequestFullscreen) {
      elem.webkitRequestFullscreen();
    } else if (elem.msRequestFullscreen) {
      elem.msRequestFullscreen();
    }
  } else {
    if (document.exitFullscreen) {
      document.exitFullscreen();
    } else if (document.webkitExitFullscreen) {
      document.webkitExitFullscreen();
    } else if (document.msExitFullscreen) {
      document.msExitFullscreen();
    }
  }
}

document.getElementById("fullscreen-btn").addEventListener("click", () => {
  fullscreenToggle();
});

document.getElementById("reload-btn").addEventListener("click", () => {
  window.location.reload();
  fullscreenToggle();
});

const popup = document.getElementById("fullscreen-popup");
const btn = document.getElementById("btn-fullscreen");

function isFullscreen() {
  return document.fullscreenElement !== null;
}

function updatePopup() {
  popup.style.display = isFullscreen() ? "none" : "flex";
}

btn.addEventListener("click", async () => {
  try {
    await document.documentElement.requestFullscreen();
  } catch (err) {
    console.error(err);
  }

  updatePopup();
});

function isAndroid() {
  return /Android/i.test(navigator.userAgent);
}

function isFullscreen() {
  return document.fullscreenElement !== null;
}

function updateFullscreenPopup() {
  // Only show on Android
  if (!isAndroid()) {
    popup.style.display = "none";
    return;
  }

  popup.style.display = isFullscreen() ? "none" : "flex";
}

window.addEventListener("load", updateFullscreenPopup);
// document.addEventListener("fullscreenchange", updateFullscreenPopup);
window.addEventListener("gamepad-action", (e) => {
  switch (e.detail.action) {
    case "switch-page-left":
      activePageIndex = activePageIndex <= 0 ? 3 : activePageIndex - 1;
      reloadPage();
      break;
    case "switch-page-right":
      activePageIndex = activePageIndex >= 3 ? 0 : activePageIndex + 1;
      reloadPage();
      break;
    case "switch-page-settings":
      activePageIndex = 4;
      reloadPage();
      break;
  }
});
