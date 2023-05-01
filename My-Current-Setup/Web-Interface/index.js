// Cole L - 1st May 2023 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
// Originally based on https://github.com/fabaff/mqtt-panel

const host = "192.168.2.50";
const port = 8080;
const useTLS = false;
const cleansession = true;
const reconnectTimeout = 2000;

const MQTT_USER = "CHANGE_ME!!!";
const MQTT_PASS = "CHANGE_ME!!!";

const COST_PER_KWH = 0.1227; // In dollars.

// Indicate when the last report from a charge controller had an error.
const cc_down = [false, false, false];

// Current watts for each charge controller. Used for cumulative total.
const panelWatts = [0, 0, 0];

// Plug IDs to use for calculating cumulative total.
const plugsForTotalWatts = [
  0, // 15AMP
  1, // 20AMP
  2, // Water Heater
];

// Current watts for each plug. Used for cumulative total. (Some plugs are connected to other ones already, ignore them).
const plugWatts = [
  0, // 15AMP
  0, // 20AMP
  0, // Water Heater
];

// Total energy generated so far today of each charge controller. Used for cumulative daily total total.
const totalPanelWatts = [0, 0, 0];

// The expected current state of each plug.
const plugStates = [undefined, undefined, undefined, undefined, undefined];

// Hold the retrieved state of a plug before it is toggled.
const preToggleState = [undefined, undefined, undefined, undefined, undefined];

// Plug IDs which have toggle confirmation modals. (Stuff that would be bad to turn off by accident)
const plugsToConfirmToggle = [
  0, // 15AMP
  1, // 20AMP
];

// "Yes" buttons of each confirmation modal.
const confirmElements = [undefined, undefined];

// "Cancel" buttons of each confirmation modal.
const cancelElements = [undefined, undefined];

// UI plug switch is locked to it's current state and will not be updated by MQTT until unlocked.
// Used to prevent the switch from switching back if we get an update just as someone toggles the switch.
const lockedSwitch = [false, false, false, false, false];

// The plug switch elements.
const plugElements = [undefined, undefined, undefined, undefined, undefined];

// Record if there is ever a discrepancy between the expected and actual plug states.
let plugError = false;

const miscTopics = [
  "Temp", // Box Air Temperature
  "Hum", // Box Air Humidity
  "Pres", // Box Air Pressure
  "Gas", // Box Air VOC Reading
];

// Names of each plug used in the MQTT topics.
const plugTopics = ["15AMP", "20AMP", "WaterHeater", "AC-Heater", "HVAC"];

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
  } else if (topic.substring(0, 4) === "tele") {
    whatToDoWithPlugs(topic, payload);
  } else if (topic.substring(0, 4) === "stat") {
    whatToDoWithPlugQuery(topic, payload);
  } else if (topic.substring(0, 4) === "cmnd") {
    whatToDoWithPlugCommand(topic, payload);
  } else {
    whatToDoWithMisc(topic, payload);
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
  swapBadge(`#cumulativePowerLabel`, "bg-secondary", "text-light");
}

// Helper to pause execution of a function and return upon a condition being met.
function until(conditionFunction) {
  const poll = (resolve) => {
    if (conditionFunction()) resolve();
    else setTimeout((_) => poll(resolve), 50);
  };
  return new Promise(poll);
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

  panelWatts[whichCC - 1] = jsonPayload.charging.watts;
  $(`#cumulativePowerLabel`).text(
    `${panelWatts.reduce((partialSum, a) => partialSum + a, 0)} W`
  );
  // Don't change badge color if any one of the controllers had an error.
  if (cc_down.every((val) => !val)) {
    swapBadge("#cumulativePowerLabel", "bg-info", "text-dark");
  }
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

  totalPanelWatts[whichCC - 1] = jsonPayload.charging.dailyPower;
  const total = totalPanelWatts.reduce((partialSum, a) => partialSum + a, 0);
  $("#cumulativeDailyTotalPowerLabel").text(
    `${round(total, 3)} KWh | ${powerCost(total)}`
  );
  // Don't change badge color if any one of the controllers had an error.
  if (cc_down.every((val) => !val)) {
    swapBadge("#cumulativeDailyTotalPowerLabel", "bg-info", "text-dark");
  }
  $(`#CC${whichCC}dailyPowerLabel`).text(
    `${jsonPayload.charging.dailyPower} KWh`
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
    `${round(jsonPayload.charging.totalAmpHours, 2)}KAh`
  );
  swapBadge(`#CC${whichCC}totalAmpHoursLabel`, "bg-info", "text-dark");

  $(`#CC${whichCC}totalPowerLabel`).text(
    `${round(jsonPayload.charging.totalPower, 2)}KWh`
  );
  swapBadge(`#CC${whichCC}totalPowerLabel`, "bg-info", "text-dark");

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

function whatToDoWithPlugs(receivedTopic, payload) {
  // Topic from plug will be in format like: tele/WaterHeater/SENSOR
  const topicBreakup = receivedTopic.split("/");

  const plugId = plugTopics.findIndex((topic) => {
    return topicBreakup[1] === topic;
  });

  if (topicBreakup[2] === "STATE") {
    const jsonPayload = JSON.parse(payload);

    if (jsonPayload.POWER === "ON") {
      if (!plugError && !lockedSwitch[plugId]) {
        if (plugElements[plugId].checked === false) {
          tempDisablePlug(plugId);
          plugElements[plugId].checked = true;
        }
        plugElements[plugId].disabled = false;
        plugStates[plugId] = true;
        $(`#Plug${topicBreakup[1]}PowerStatusLabel`).text("ON");
        swapBadge(
          `#Plug${topicBreakup[1]}PowerStatusLabel`,
          "bg-success",
          "text-light"
        );
      }
    } else if (jsonPayload.POWER === "OFF") {
      if (!plugError && !lockedSwitch[plugId]) {
        if (plugElements[plugId].checked === true) {
          tempDisablePlug(plugId);
          plugElements[plugId].checked = false;
        }
        plugElements[plugId].disabled = false;
        plugStates[plugId] = false;
        $(`#Plug${topicBreakup[1]}PowerStatusLabel`).text("OFF");
        swapBadge(
          `#Plug${topicBreakup[1]}PowerStatusLabel`,
          "bg-primary",
          "text-light"
        );
      }
    } else {
      $(`#Plug${topicBreakup[1]}PowerStatusLabel`).text("UNKNOWN STATE");
      swapBadge(
        `#Plug${topicBreakup[1]}PowerStatusLabel`,
        "bg-danger",
        "text-light"
      );
    }
  } else if (topicBreakup[2] === "SENSOR") {
    const jsonPayload = JSON.parse(payload);
    $(`#Plug${topicBreakup[1]}PowerLabel`).text(
      jsonPayload.ENERGY.Power + " W"
    );
    swapBadge(`#Plug${topicBreakup[1]}PowerLabel`, "bg-info", "text-dark");
    if (plugsForTotalWatts.includes(plugId)) {
      const plugsTotalId = plugsForTotalWatts.findIndex((id) => {
        return plugId === id;
      });

      plugWatts[plugsTotalId] = jsonPayload.ENERGY.Power;
      $(`#cumulativePlugPowerLabel`).text(
        `${plugWatts.reduce((partialSum, a) => partialSum + a, 0)} W`
      );
      swapBadge(`#cumulativePlugPowerLabel`, "bg-info", "text-dark");
    }

    $(`#Plug${topicBreakup[1]}CurrentLabel`).text(
      jsonPayload.ENERGY.Current + " A"
    );
    swapBadge(`#Plug${topicBreakup[1]}CurrentLabel`, "bg-info", "text-dark");

    $(`#Plug${topicBreakup[1]}VoltageLabel`).text(
      jsonPayload.ENERGY.Voltage + " V"
    );
    swapBadge(`#Plug${topicBreakup[1]}VoltageLabel`, "bg-info", "text-dark");

    $(`#Plug${topicBreakup[1]}TodayPowerLabel`).text(
      `${jsonPayload.ENERGY.Today}KWh ${powerCost(jsonPayload.ENERGY.Today)}`
    );
    swapBadge(`#Plug${topicBreakup[1]}TodayPowerLabel`, "bg-info", "text-dark");

    // $(`#Plug${topicBreakup[1]}YesterdayPowerLabel`).text(`${jsonPayload.ENERGY.Yesterday}KWh | ${powerCost(jsonPayload.ENERGY.Yesterday)}`);
    // swapBadge(`#Plug${topicBreakup[1]}YesterdayPowerLabel`, 'bg-info', 'text-dark');

    $(`#Plug${topicBreakup[1]}TotalPowerLabel`).text(
      `${jsonPayload.ENERGY.Total}KWh ${powerCost(jsonPayload.ENERGY.Total)}`
    );
    swapBadge(`#Plug${topicBreakup[1]}TotalPowerLabel`, "bg-info", "text-dark");

    $(`#Plug${topicBreakup[1]}ApparentPowerLabel`).text(
      jsonPayload.ENERGY.ApparentPower + " VA"
    );
    swapBadge(
      `#Plug${topicBreakup[1]}ApparentPowerLabel`,
      "bg-info",
      "text-dark"
    );

    $(`#Plug${topicBreakup[1]}ReactivePowerLabel`).text(
      jsonPayload.ENERGY.ReactivePower + " VAr"
    );
    swapBadge(
      `#Plug${topicBreakup[1]}ReactivePowerLabel`,
      "bg-info",
      "text-dark"
    );

    $(`#Plug${topicBreakup[1]}FactorLabel`).text(jsonPayload.ENERGY.Factor);
    swapBadge(`#Plug${topicBreakup[1]}FactorLabel`, "bg-info", "text-dark");

    // $(`#Plug${topicBreakup[1]}PeriodLabel`).text(jsonPayload.ENERGY.Period);
    // swapBadge(`#Plug${topicBreakup[1]}PeriodLabel`, 'bg-info', 'text-dark');
  } else {
    // Print any unknown messages.
    console.log(`${receivedTopic}: ${payload}`);
  }
}

// Received response from plug asking about it's state.
function whatToDoWithPlugQuery(receivedTopic, payload) {
  const topicBreakup = receivedTopic.split("/");

  const plugId = plugTopics.findIndex((topic) => {
    return topicBreakup[1] === topic;
  });

  if (topicBreakup[2] === "POWER") {
    if (payload === "ON") {
      preToggleState[plugId] = true;
    } else if (payload === "OFF") {
      preToggleState[plugId] = false;
    }
  } else {
    // Print any unknown messages.
    console.log(`${receivedTopic}: ${payload}`);
  }
}

// Command was sent by a device to toggle a plug. Disable that switch until next update is received.
function whatToDoWithPlugCommand(receivedTopic, payload) {
  const topicBreakup = receivedTopic.split("/");

  const plugId = plugTopics.findIndex((topic) => {
    return topicBreakup[1] === topic;
  });

  if (topicBreakup[2] === "Power") {
    if (payload === "TOGGLE") {
      plugElements[plugId].disabled = true;
    } else {
      // Print any unknown messages.
      console.log(`${receivedTopic}: ${payload}`);
    }
  } else {
    // Print any unknown messages.
    console.log(`${receivedTopic}: ${payload}`);
  }
}

function whatToDoWithMisc(receivedTopic, payload) {
  const topicIndex = miscTopics.findIndex((topic) => {
    return receivedTopic === topic;
  });

  switch (topicIndex) {
    case 1:
      $("#tempSensorLabel").text(round(payload, 2) + " °C");
      if (payload > 40) {
        swapBadge("#tempSensorLabel", "bg-danger", "text-light");
      } else if (payload > 30) {
        swapBadge("#tempSensorLabel", "bg-warning", "text-dark");
      } else if (payload > 10) {
        swapBadge("#tempSensorLabel", "bg-success", "text-light");
      } else if (payload > -10) {
        swapBadge("#tempSensorLabel", "bg-info", "text-dark");
      } else {
        swapBadge("#tempSensorLabel", "bg-secondary", "text-light");
      }
      break;
    case 2:
      $("#humLabel").text(round(payload, 2) + " %");
      swapBadge("#humLabel", "bg-info", "text-dark");
      break;
    case 3:
      $("#presLabel").text(round(payload, 2) + " hPa");
      swapBadge("#presLabel", "bg-info", "text-dark");
      break;
    case 4:
      $("#gasLabel").text(round(payload, 2) + " kΩ");
      swapBadge("#gasLabel", "bg-info", "text-dark");
      break;
    default:
      // Print any unknown messages.
      console.log(`${receivedTopic}: ${payload}`);
      break;
  }
}

// Disable all plug switches in case of an error.
function disablePlugs() {
  plugError = true;
  plugElements.forEach((element) => {
    element.disabled = true;
  });
}

// Temporarily disable a plug switch, this helps to avoid scenarios of double clicking or two people toggling at almost the same time.
function tempDisablePlug(plugId) {
  // If plug is already disabled (ex: by local toggling), shorten wait time.
  if (plugElements[plugId].disabled === true) {
    setTimeout(function () {
      plugElements[plugId].disabled = false;
    }, 500);
  } else {
    plugElements[plugId].disabled = true;
    setTimeout(function () {
      plugElements[plugId].disabled = false;
    }, 2000);
  }
}

// Fetch the current state of the plug, compare with expected state and send command to toggle if it has the expected state.
async function togglePlug(plugId, action) {
  lockedSwitch[plugId] = true;
  plugElements[plugId].disabled = true;
  preToggleState[plugId] = undefined;
  let message = new Paho.MQTT.Message("");
  message.destinationName = `cmnd/${plugTopics[plugId]}/Power`;
  mqtt.send(message);

  await until((_) => preToggleState[plugId] != undefined);

  if (preToggleState[plugId] != null && preToggleState[plugId] !== action) {
    let messageToggle = new Paho.MQTT.Message("TOGGLE");
    messageToggle.destinationName = `cmnd/${plugTopics[plugId]}/Power`;
    mqtt.send(messageToggle);
  } else {
    disablePlugs();
    $(`#Plug${plugTopics[plugId]}PowerStatusLabel`).text("ERROR! (Reload)");
    swapBadge(
      `#Plug${plugTopics[plugId]}PowerStatusLabel`,
      "bg-danger",
      "text-light"
    );
  }
  lockedSwitch[plugId] = false;
}

// Perform initial setup for the smart plug toggle switches.
function setupPlugs() {
  plugTopics.forEach((element, index) => {
    plugElements[index] = document.getElementById(`${element}Switch`);
    plugElements[index].disabled = true; // Disable the plug until we get an MQTT update from it.

    // Does this plug need to show a modal before turning it off.
    if (plugsToConfirmToggle.includes(index)) {
      // Wire up the modal buttons.
      confirmElements[index] = document.getElementById(
        `${plugTopics[index]}Confirm`
      );
      confirmElements[index].addEventListener("click", function () {
        togglePlug(index, false);
      });
      cancelElements[index] = document.getElementById(
        `${plugTopics[index]}Cancel`
      );
      cancelElements[index].addEventListener("click", function () {
        plugElements[index].checked = true;
        lockedSwitch[index] = false;
      });

      // Only show the modal when turning the plug off.
      plugElements[index].addEventListener("click", function () {
        if (plugElements[index].checked) {
          togglePlug(index, true);
        } else {
          lockedSwitch[index] = true;
          $(`#modal${plugTopics[index]}`).modal("show");
        }
      });
    } else {
      plugElements[index].addEventListener("click", function () {
        if (plugElements[index].checked) {
          togglePlug(index, true);
        } else {
          togglePlug(index, false);
        }
      });
    }
  });
}

$(document).ready(function () {
  MQTTconnect();
  setupPlugs();
});
