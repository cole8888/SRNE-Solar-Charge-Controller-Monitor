// Cole L - 1st May 2023 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
// Originally based on https://github.com/fabaff/mqtt-panel

const host = "192.168.2.50"; // MQTT Broker address
const port = 8080; // MQTT websocket port
const useTLS = false;
const cleansession = true;
const reconnectTimeout = 2000;

const MQTT_USER = "CHANGE_ME!!!";
const MQTT_PASS = "CHANGE_ME!!!";

const COST_PER_KWH = 0.1227; // In dollars.

// Indicate when the last report from a charge controller had an error.
const cc_down = [false];

// The id of all the badges relating to charge controller data.
// Used to turn them all grey if that controller throws an error.
const chargeControllerElementIds = [
  "battVoltsLabel",
  "battMaxVoltsLabel",
  "battMinVoltsLabel",
  "numOverDischargesLabel",
  "numFullChargesLabel",
  "numDaysLabel",
  "SOCLabel",
  "totalPowerLabel",
  "dailyPowerLabel",
  "totalAmpHoursLabel",
  "dailyAmpHoursLabel",
  "controllerTempLabel",
  "battTempLabel",
  "chargeModeLabel",
  "panelVoltsLabel",
  "panelAmpsLabel",
  "battAmpsLabel",
  "battMaxChargeCurrentLabel",
  "panelWattsLabel",
  "panelMaxPowerLabel",
  "loadStateLabel",
  "loadWattsLabel",
  "loadAmpsLabel",
  "loadVoltsLabel",
  "loadMaxWattsLabel",
  "loadMaxAmpsLabel",
  "loadDailyPowerLabel",
  "loadDailyAmpHoursLabel",
  "loadTotalPowerLabel",
  "loadTotalAmpHoursLabel",
  "anyFaults",
];

// Bootstrap badge colors that can be used.
const badgeTypes = [
  "bg-warning",
  "bg-success",
  "bg-info",
  "bg-secondary",
  "bg-secondary",
  "bg-danger",
];

let mqtt;
function MQTTconnect() {
  if (typeof path == "undefined") {
    path = "/";
  }
  mqtt = new Paho.MQTT.Client(
    host,
    port,
    path,
    "mqtt_panel" + parseInt(Math.random() * 100, 10)
  );
  let options = {
    timeout: 3,
    userName: MQTT_USER,
    password: MQTT_PASS,
    useSSL: useTLS,
    cleanSession: cleansession,
    onSuccess: onConnect,
    onFailure: function (message) {
      $("#status")
        .html("Connection failed: " + message.errorMessage + " Retrying...")
        .attr("class", "alert alert-danger");
      setTimeout(MQTTconnect, reconnectTimeout);
    },
  };

  mqtt.onConnectionLost = onConnectionLost;
  mqtt.onMessageArrived = onMessageArrived;
  //console.log("Host: " + host + ", Port: " + port + ", Path: " + path + " TLS: " + useTLS);
  mqtt.connect(options);
}

function onConnect() {
  $("#status")
    .html("Connected to " + host)
    .attr("class", "alert alert-success text-dark");
  mqtt.subscribe("#", { qos: 0 });
}

function onConnectionLost(response) {
  setTimeout(MQTTconnect, reconnectTimeout);
  $("#status")
    .html("Connection lost. Reconnecting...")
    .attr("class", "alert alert-warning");
}

function onMessageArrived(message) {
  let topic = message.destinationName;
  let payload = message.payloadString;

  if (topic.substring(0, 2) === "CC") {
    whatToDoWithCC(parseInt(topic[2]), payload);
  } else {
    // Print any unknown messages.
    console.log(`${topic}: ${payload}`);
  }
}

function round(value, decimals) {
  return Number(Math.round(value + "e" + decimals) + "e-" + decimals).toFixed(
    decimals
  );
}

function powerCost(KWh) {
  return `$${round(KWh * COST_PER_KWH, 2)}`;
}

// Swap badge color and badge text colors.
function swapBadge(element, newBadge, newText) {
  $(element)
    .removeClass(
      `${badgeTypes.filter((badge) => badge !== newBadge).join(" ")}`
    )
    .addClass(`${newBadge}`);

  if (newText === "text-dark" && $(element).hasClass("text-light")) {
    $(element).addClass("text-dark").removeClass("text-light");
  } else if (newText === "text-light" && $(element).hasClass("text-dark")) {
    $(element).addClass("text-light").removeClass("text-dark");
  }
}

// Turn all badges for a given charge controller grey to indicate there was an error.
function indicateError(whichCC) {
  for (let i = 0; i < chargeControllerElementIds.length; i++) {
    swapBadge(
      `#CC${whichCC}${chargeControllerElementIds[i]}`,
      "bg-secondary",
      "text-light"
    );
  }
}

// Received data from a charge controller, update all the badges.
function whatToDoWithCC(whichCC, payload) {
  jsonPayload = JSON.parse(payload);

  // If there was an error when reading the data over modbus, turn all badges grey.
  if (jsonPayload.modbusError) {
    if (!cc_down[whichCC - 1]) {
      cc_down[whichCC - 1] = true;
      indicateError(whichCC);
    }
    return;
  } else {
    cc_down[whichCC - 1] = false;
  }

  $(`#CC${whichCC}chargeModeLabel`).text(jsonPayload.controller.chargingMode);
  swapBadge(`#CC${whichCC}chargeModeLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}SOCLabel`).text(`${jsonPayload.battery.stateOfCharge}%`);
  swapBadge(`#CC${whichCC}SOCLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}battVoltsLabel`).text(`${jsonPayload.battery.volts} V`);
  if (jsonPayload.battery.volts > 31 || jsonPayload.battery.volts < 24) {
    swapBadge(`#CC${whichCC}battVoltsLabel`, "bg-danger", "text-light");
  } else if (
    jsonPayload.battery.volts > 30.2 ||
    jsonPayload.battery.volts < 24.8
  ) {
    swapBadge(`#CC${whichCC}battVoltsLabel`, "bg-warning", "text-dark");
  } else {
    swapBadge(`#CC${whichCC}battVoltsLabel`, "bg-success", "text-light");
  }

  $(`#CC${whichCC}battAmpsLabel`).text(`${jsonPayload.charging.amps} A`);
  swapBadge(`#CC${whichCC}battAmpsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}controllerTempLabel`).text(
    `${jsonPayload.controller.temperature}°C`
  );
  if (jsonPayload.controller.temperature > 55) {
    swapBadge(`#CC${whichCC}controllerTempLabel`, "bg-danger", "text-light");
  } else if (jsonPayload.controller.temperature > 40) {
    swapBadge(`#CC${whichCC}controllerTempLabel`, "bg-warning", "text-dark");
  } else if (jsonPayload.controller.temperature > 5) {
    swapBadge(`#CC${whichCC}controllerTempLabel`, "bg-success", "text-light");
  } else if (jsonPayload.controller.temperature > -20) {
    swapBadge(`#CC${whichCC}controllerTempLabel`, "bg-info", "text-dark");
  } else {
    swapBadge(`#CC${whichCC}controllerTempLabel`, "bg-secondary", "text-light");
  }

  $(`#CC${whichCC}battTempLabel`).text(`${jsonPayload.battery.temperature}°C`);
  if (jsonPayload.battery.temperature > 30) {
    swapBadge(`#CC${whichCC}battTempLabel`, "bg-danger", "text-light");
  } else if (jsonPayload.battery.temperature > 25) {
    swapBadge(`#CC${whichCC}battTempLabel`, "bg-warning", "text-dark");
  } else if (jsonPayload.battery.temperature > 5) {
    swapBadge(`#CC${whichCC}battTempLabel`, "bg-success", "text-light");
  } else if (jsonPayload.battery.temperature > -10) {
    swapBadge(`#CC${whichCC}battTempLabel`, "bg-info", "text-dark");
  } else {
    swapBadge(`#CC${whichCC}battTempLabel`, "bg-secondary", "text-light");
  }

  $(`#CC${whichCC}panelVoltsLabel`).text(`${jsonPayload.panels.volts} V`);
  swapBadge(`#CC${whichCC}panelVoltsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}panelAmpsLabel`).text(`${jsonPayload.panels.amps} A`);
  swapBadge(`#CC${whichCC}panelAmpsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}panelWattsLabel`).text(`${jsonPayload.charging.watts} W`);
  swapBadge(`#CC${whichCC}panelWattsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}battMinVoltsLabel`).text(`${jsonPayload.battery.minVolts} V`);
  swapBadge(`#CC${whichCC}battMinVoltsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}battMaxVoltsLabel`).text(`${jsonPayload.battery.maxVolts} V`);
  swapBadge(`#CC${whichCC}battMaxVoltsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}battMaxChargeCurrentLabel`).text(
    `${jsonPayload.charging.maxAmps} A`
  );
  swapBadge(`#CC${whichCC}battMaxChargeCurrentLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}panelMaxPowerLabel`).text(
    `${jsonPayload.charging.maxWatts} W`
  );
  swapBadge(`#CC${whichCC}panelMaxPowerLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}dailyAmpHoursLabel`).text(
    `${jsonPayload.charging.dailyAmpHours} Ah`
  );
  swapBadge(`#CC${whichCC}dailyAmpHoursLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}dailyPowerLabel`).text(
    `${jsonPayload.charging.dailyPower} KWh | ${powerCost(
      jsonPayload.charging.dailyPower
    )}`
  );
  swapBadge(`#CC${whichCC}dailyPowerLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}numDaysLabel`).text(jsonPayload.controller.days);
  swapBadge(`#CC${whichCC}numDaysLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}numOverDischargesLabel`).text(
    jsonPayload.controller.overDischarges
  );
  swapBadge(`#CC${whichCC}numOverDischargesLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}numFullChargesLabel`).text(
    jsonPayload.controller.fullCharges
  );
  swapBadge(`#CC${whichCC}numFullChargesLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}totalAmpHoursLabel`).text(
    `${jsonPayload.charging.totalAmpHours} KAh`
  );
  swapBadge(`#CC${whichCC}totalAmpHoursLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}totalPowerLabel`).text(
    `${jsonPayload.charging.totalPower} KWh`
  );
  swapBadge(`#CC${whichCC}totalPowerLabel`, "bg-info", "text-dark");

  if (jsonPayload.load.state === true) {
    $(`#CC${whichCC}loadStateLabel`).text("ON");
    swapBadge(`#CC${whichCC}loadStateLabel`, "bg-success", "text-light");
  } else if (jsonPayload.load.state === false) {
    $(`#CC${whichCC}loadStateLabel`).text("OFF");
    swapBadge(`#CC${whichCC}loadStateLabel`, "bg-primary", "text-light");
  }

  $(`#CC${whichCC}loadVoltsLabel`).text(`${jsonPayload.load.volts} V`);
  swapBadge(`#CC${whichCC}loadVoltsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadAmpsLabel`).text(`${jsonPayload.load.amps} A`);
  swapBadge(`#CC${whichCC}loadAmpsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadWattsLabel`).text(`${jsonPayload.load.watts} W`);
  swapBadge(`#CC${whichCC}loadWattsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadMaxAmpsLabel`).text(`${jsonPayload.load.maxAmps} A`);
  swapBadge(`#CC${whichCC}loadMaxAmpsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadMaxWattsLabel`).text(`${jsonPayload.load.maxWatts} W`);
  swapBadge(`#CC${whichCC}loadMaxWattsLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadDailyAmpHoursLabel`).text(
    `${jsonPayload.load.dailyAmpHours} Ah`
  );
  swapBadge(`#CC${whichCC}loadDailyAmpHoursLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadTotalAmpHoursLabel`).text(
    `${jsonPayload.load.totalAmpHours} KAh`
  );
  swapBadge(`#CC${whichCC}loadTotalAmpHoursLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadDailyPowerLabel`).text(
    `${jsonPayload.load.dailyPower} KWh`
  );
  swapBadge(`#CC${whichCC}loadDailyPowerLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}loadTotalPowerLabel`).text(
    `${jsonPayload.load.totalPower} KWh`
  );
  swapBadge(`#CC${whichCC}loadTotalPowerLabel`, "bg-info", "text-dark");

  if (jsonPayload.faults.length > 0) {
    faultHtml = "";

    $(`#CC${whichCC}anyFaults`).text("YES");
    swapBadge(`#CC${whichCC}anyFaults`, "bg-danger", "text-light");

    if (jsonPayload.faults.length > 1) {
      jsonPayload.faults.forEach((fault) => {
        faultHtml += `<br>- ${fault}`;
      });
    } else {
      faultHtml = `- ${jsonPayload.faults[0]}`;
    }

    $(`#CC${whichCC}faults`).html(faultHtml);
  } else {
    $(`#CC${whichCC}anyFaults`).text("NO");
    swapBadge(`#CC${whichCC}anyFaults`, "bg-success", "text-light");

    $(`#CC${whichCC}faults`).html("None :)");
  }
}

$(document).ready(function () {
  MQTTconnect();
});
