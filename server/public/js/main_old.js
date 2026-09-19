const socket = io();
// console.log('script start')

// document.getElementById("agentType").textContent = navigator.userAgent;
if (navigator.userAgent.includes("Android")) {
  document.getElementById("leftPanel").style.display = "block";
  document.getElementById("rightPanel").style.display = "block";
  document.getElementById("footer").style.height = "7vh";
  document.getElementById("tabHeader").style.height = "7vh";
} else {
  document.getElementById("leftPanel").style.display = "none";
  document.getElementById("rightPanel").style.display = "none";
  document.getElementById("footer").style.height = "5vh";
  document.getElementById("tabHeader").style.height = "5vh";
}

// Create JoyStick object into the DIV 'joy1Div'
var Joy1 = new JoyStick("joy1Div", {
  title: "joystick1",
  autoReturnToCenter: true,
  x: 0.4,
  y: 0.4,
});

// Create JoyStick object into the DIV 'joy2Div'
var Joy2 = new JoyStick("joy2Div", {
  title: "joystick2",
  autoReturnToCenter: true,
  x: 3.9,
  y: 3.9,
});

const img = document.getElementById("robot_view");
const imgCon = document.getElementById("videoContainer");
img.addEventListener("load", (event) => {
  const imgRatio = img.offsetWidth / img.offsetHeight;
  const imgConRatio = imgCon.offsetWidth / imgCon.offsetHeight;
  console.log("imgRatio:", imgRatio);
  console.log("blockRatio:", imgConRatio);
  if (imgRatio > imgConRatio) {
    img.style.width = "100%";
    img.style.height = "auto";
    imgCon.style.height = img.offsetHeight + "px";
  } else {
    img.style.width = "auto";
    img.style.height = "100%";
    imgCon.style.width = img.offsetWidth + "px";
    const otherWidth =
      (document.getElementById("Home").offsetWidth - imgCon.offsetWidth) / 2;
    document.getElementById("leftVideoContainer").style.width =
      otherWidth + "px";
    document.getElementById("rightVideoContainer").style.width =
      otherWidth - 50 + "px";
  }
  // img.style.aspectRatio = "auto"
});
img.addEventListener("load", () => {
  document.getElementById("startCameraIcon").textContent = "stop_circle";
});

document.getElementById("rightPanel").style.display = "none";
document.getElementById("leftPanel").style.display = "none";

socket.on("ros:topic", (d) => {
  var item = document.createElement("li");
  item.textContent = JSON.stringify(d.data);
  var messages = document.getElementById("messages");
  messages.appendChild(item);
  var c = document.getElementById("messageBox");
  c.scrollTo(0, c.scrollHeight);
  const vel = d.data.vel;
  chartData_front_right_wheel.push(vel[0]);
  while (chartData_front_right_wheel.length > chartSize)
    chartData_front_right_wheel.shift();
  chartData_rear_right_wheel.push(vel[1]);
  while (chartData_rear_right_wheel.length > chartSize)
    chartData_rear_right_wheel.shift();
  chartData_rear_left_wheel.push(vel[2]);
  while (chartData_rear_left_wheel.length > chartSize)
    chartData_rear_left_wheel.shift();
  chartData_front_left_wheel.push(vel[3]);
  while (chartData_front_left_wheel.length > chartSize)
    chartData_front_left_wheel.shift();
  // update chart
  wheelVelocityChart.data.datasets[0].data = JSON.parse(
    JSON.stringify(chartData_front_right_wheel),
  );
  wheelVelocityChart.data.datasets[1].data = JSON.parse(
    JSON.stringify(chartData_rear_right_wheel),
  );
  wheelVelocityChart.data.datasets[2].data = JSON.parse(
    JSON.stringify(chartData_rear_left_wheel),
  );
  wheelVelocityChart.data.datasets[3].data = JSON.parse(
    JSON.stringify(chartData_front_left_wheel),
  );
  wheelVelocityChart.update();
  // update bottom row
  linear_vel_average = 0;
  vel.forEach((v) => (linear_vel_average += v / 4));
  angular_vel_average = 0;
  vel.forEach((a) => (angular_vel_average += a / 4));

  document.getElementById("battery_voltage").value =
    d.data.bat.toFixed(2) + " V";
  // document.getElementById('battery_mA').value = d.data.mA.toFixed() + " mA";
  // document.getElementById('battery_mAh').value = d.data.mAh.toFixed() + " mAh";
  document.getElementById("battery_Wh").value = d.data.Wh.toFixed(2) + " Wh";

  chartData_mA.push(Math.abs(d.data.mA) / 1000);
  while (chartData_mA.length > chartSize) chartData_mA.shift();
  mAChart.data.datasets[0].data = chartData_mA;
  mAChart.update();

  linearMiniChart.data.datasets[0].data[0] =
    2 + Math.abs(linear_vel_average) * 3;
  linearMiniChart.data.datasets[0].data[1] =
    28 - Math.abs(linear_vel_average) * 3;
  linearMiniChart.update();
});

let tempTable = [];
socket.on("ros:monitor", (d) => {
  var nodeTable = document.getElementById("nodelist");
  // remove all rows
  while (nodeTable.rows.length > 1) nodeTable.deleteRow(1);
  const nodes = d.nodes;
  // create new rows
  let isRobotActivated = false;
  nodes.forEach((node) => {
    let row = nodeTable.insertRow(1);
    let cell1 = row.insertCell(0);
    let cell2 = row.insertCell(0);
    cell1.innerHTML = "---";
    cell2.innerHTML = node;
    if (node == "/controller_manager") isRobotActivated = true;
  });

  var topicTable = document.getElementById("topiclist");
  // remove all rows
  while (topicTable.rows.length > 1) topicTable.deleteRow(1);
  const topics = d.topics;
  topics.forEach((topic) => {
    let row = topicTable.insertRow(1);
    let cell1 = row.insertCell(0);
    let cell2 = row.insertCell(0);
    cell1.innerHTML = topic.type || "---";
    cell2.innerHTML = topic.name;
  });

  var serviceTable = document.getElementById("serviceTable");
  // remove all rows
  while (serviceTable.rows.length > 1) serviceTable.deleteRow(1);
  const services = d.services;
  services.forEach((service) => {
    let row = serviceTable.insertRow(1);
    let cell1 = row.insertCell(0);
    let cell2 = row.insertCell(0);
    let cell3 = row.insertCell(0);
    cell1.innerHTML = service.active || "---";
    cell2.innerHTML = service.loaded || "---";
    cell3.innerHTML = service.service;
  });

  if (isRobotActivated) {
    document.getElementById("connectShortcut").style.backgroundColor =
      "#23D160";
    document.getElementById("connectIcon").textContent = "link";
  } else {
    document.getElementById("connectShortcut").style.backgroundColor =
      "#f0f0f0";
    document.getElementById("connectIcon").textContent = "link_off";
  }
});
