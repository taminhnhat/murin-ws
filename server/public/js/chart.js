const chartSize = 20;
var chartData_front_right_wheel = new Array(chartSize).fill(0);
var chartData_rear_right_wheel = new Array(chartSize).fill(0);
var chartData_rear_left_wheel = new Array(chartSize).fill(0);
var chartData_front_left_wheel = new Array(chartSize).fill(0);

var chartData_mA = new Array(chartSize).fill(0);

var linearMiniChart = new Chart("linearMiniChart", {
  type: "doughnut",
  data: {
    datasets: [
      {
        backgroundColor: ["#23D160", "rgba(75, 192, 192, 0.2)", "transparent"],
        data: [2, 28, 70],
        borderWidth: [0, 0],
        borderColor: "transparent",
      },
    ],
  },
  options: {
    events: [],
    legend: { display: false },
    cutoutPercentage: 50,
    tooltip: { enabled: false },
    rotation: -0.8 * Math.PI,
  },
});

var wheelVelocityChart = new Chart("wheelVelocityChart", {
  type: "line",
  data: {
    labels: new Array(chartSize).fill(0),
    datasets: [
      {
        fill: false,
        lineTension: 0,
        backgroundColor: "rgba(254, 119, 123, 0.5)",
        borderColor: "rgba(254, 119, 123, 0.5)",
        borderWidth: 1,
        data: new Array(chartSize).fill(0),
      },
      {
        fill: false,
        lineTension: 0,
        backgroundColor: "rgba(255, 206, 86, 0.5)",
        borderColor: "rgba(255, 206, 86, 0.5)",
        borderWidth: 1,
        data: new Array(chartSize).fill(0),
      },
      {
        fill: false,
        lineTension: 0,
        backgroundColor: "rgba(54, 162, 235, 0.5)",
        borderColor: "rgba(54, 162, 235, 0.5)",
        borderWidth: 1,
        data: new Array(chartSize).fill(0),
      },
      {
        fill: false,
        lineTension: 0,
        backgroundColor: "rgba(75, 192, 192, 0.5)",
        borderColor: "rgba(75, 192, 192, 0.5)",
        borderWidth: 1,
        data: new Array(chartSize).fill(0),
      },
    ],
  },
  options: {
    events: [],
    legend: { display: false },
    scales: {
      yAxes: [
        {
          ticks: { min: -8.0, max: 8.0, display: false },
          // gridLines: { display: false }
        },
      ],
      xAxes: [
        {
          ticks: { display: false },
          gridLines: { display: false },
        },
      ],
    },
    overrides: {
      scales: true,
    },
    elements: {
      point: {
        radius: 0,
      },
    },
  },
});

var mAChart = new Chart("mAChart", {
  type: "line",
  data: {
    labels: new Array(chartSize).fill(0),
    datasets: [
      {
        fill: false,
        lineTension: 0,
        backgroundColor: "rgba(254, 119, 123, 0.5)",
        borderColor: "rgba(254, 119, 123, 0.5)",
        borderWidth: 1,
        data: new Array(chartSize).fill(0),
      },
    ],
  },
  options: {
    events: [],
    legend: { display: false },
    scales: {
      yAxes: [
        {
          ticks: { min: -0.5, max: 8.0, display: false },
          // gridLines: { display: false }
        },
      ],
      xAxes: [
        {
          ticks: { display: false },
          gridLines: { display: false },
        },
      ],
    },
    overrides: {
      scales: true,
    },
    elements: {
      point: {
        radius: 0,
      },
    },
  },
});
