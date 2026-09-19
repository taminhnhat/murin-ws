import * as THREE from "https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js";

const canvas = document.getElementById("imu-canvas");
const status = document.getElementById("imu-status");
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x101722);

const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100);
camera.position.set(3.2, 2.4, 4.5);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
scene.add(new THREE.HemisphereLight(0xffffff, 0x24364d, 2.5));

const keyLight = new THREE.DirectionalLight(0xffffff, 2.5);
keyLight.position.set(3, 4, 5);
scene.add(keyLight);

const body = new THREE.Mesh(
  new THREE.BoxGeometry(2.2, 0.65, 1.35),
  new THREE.MeshStandardMaterial({ color: 0x2f8cff, roughness: 0.35 }),
);
scene.add(body, new THREE.AxesHelper(2.3));

const basis = new THREE.Quaternion().setFromRotationMatrix(
  new THREE.Matrix4().setFromMatrix3(
    new THREE.Matrix3().set(0, 1, 0, 0, 0, 1, 1, 0, 0),
  ),
);
const inverseBasis = basis.clone().invert();
let latest = null;
let reference = null;

function resizeCanvas() {
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  renderer.setSize(rect.width, rect.height, false);
  camera.aspect = rect.width / rect.height;
  camera.updateProjectionMatrix();
}

new ResizeObserver(resizeCanvas).observe(canvas);
resizeCanvas();

document.getElementById("imu-reset").addEventListener("click", () => {
  if (latest) reference = latest.clone();
});

const format = (values) =>
  values.map((value) => Number(value).toFixed(2)).join("  ");

window.Murin.robotSocket.on("imu", (imu) => {
  if (!imu.valid) {
    status.textContent = `IMU invalid (status ${imu.status})`;
    return;
  }

  const quaternion = imu.quaternion;
  latest = new THREE.Quaternion(
    quaternion[1],
    quaternion[2],
    quaternion[3],
    quaternion[0],
  ).normalize();
  const relative = reference
    ? reference.clone().invert().multiply(latest)
    : latest.clone();
  body.quaternion
    .copy(basis.clone().multiply(relative).multiply(inverseBasis))
    .normalize();

  status.textContent = `Live · ${imu.timestampUs} µs`;
  document.getElementById("imu-accel").textContent = format(imu.acceleration);
  document.getElementById("imu-gyro").textContent = format(imu.angularVelocity);
  document.getElementById("imu-mag").textContent = format(imu.magneticField);
  document.getElementById("imu-quaternion").textContent = quaternion
    .map((value) => Number(value).toFixed(3))
    .join("  ");
});

window.Murin.robotSocket.on("imu_status", (state) => {
  status.textContent =
    state.message || (state.connected ? "IMU connected" : "IMU disconnected");
});
window.Murin.robotSocket.emit("imu_status_request");

function animate() {
  requestAnimationFrame(animate);
  renderer.render(scene, camera);
}
animate();
