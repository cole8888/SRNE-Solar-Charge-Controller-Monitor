/*
    Cole L - 1st May 2023 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor

    This is an example to retrieve data from a single charge controller. All this program does is print the data to the console.
    This should give you enough information on how to read the values so you can do more exciting stuff with the data.

    See images and schematic for wiring details.
*/

#include <ArduinoJson.h>    // https://github.com/bblanchon/ArduinoJson
#include <ModbusMaster.h>   // https://github.com/4-20ma/ModbusMaster
#include <SoftwareSerial.h> // Software serial for modbus.

/*
    Pins to use for software serial for talking to the charge controller through the MAX3232.
*/
#define MAX3232_RX 2 // RX pin.
#define MAX3232_TX 3 // TX pin.

/*
    Modbus Constants
    All charge controllers will respond to address 255 no matter what their actual address is, this is useful if you do not know what address to use.
    You can try using 1 here instead of 255 if you don't want to make changes to the library, but it is not guaranteed to work.
*/
#define NUM_REGISTERS 35
#define MODBUS_SLAVE_ADDR 255
#define MODBUS_REQUEST_START_ADDR 256

/*
    Other settings.
*/
#define REQUEST_DELAY 3000     // Delay in ms between requests to the charge controller over modbus.
#define SETUP_FINISH_DELAY 100 // Delay in ms after finishing setup.
#define JSON_BUFFER_SIZE 1024  // Maximum size for the JSON.

/*
    Describes the different states the program can be in.
*/
enum STATE {
    WAIT = 0,
    QUERY = 1,
    TRANSMIT = 2
};
STATE state;

/*
    Array of charging mode strings.
    These are the states the charge controller can be in when charging the battery.
*/
const char *chargeModes[7] = {
    "OFF",      // 0
    "NORMAL",   // 1
    "MPPT",     // 2
    "EQUALIZE", // 3
    "BOOST",    // 4
    "FLOAT",    // 5
    "CUR_LIM"   // 6 (Current limiting)
};

/*
    Array of fault codes.
    These are all the possible faults the charge controller can indicate.
*/
const char *faultCodes[15] = {
    "Charge MOS short circuit",      // (16384 | 01000000 00000000)
    "Anti-reverse MOS short",        // (8192  | 00100000 00000000)
    "PV panel reversely connected",  // (4096  | 00010000 00000000)
    "PV working point over voltage", // (2048  | 00001000 00000000)
    "PV counter current",            // (1024  | 00000100 00000000)
    "PV input side over-voltage",    // (512   | 00000010 00000000)
    "PV input side short circuit",   // (256   | 00000001 00000000)
    "PV input overpower",            // (128   | 00000000 10000000)
    "Ambient temp too high",         // (64    | 00000000 01000000)
    "Controller temp too high",      // (32    | 00000000 00100000)
    "Load over-power/current",       // (16    | 00000000 00010000)
    "Load short circuit",            // (8     | 00000000 00001000)
    "Battery undervoltage warning",  // (4     | 00000000 00000100)
    "Battery overvoltage",           // (2     | 00000000 00000010)
    "Battery over-discharge"         // (1     | 00000000 00000001)
};

// Create the modbus node for the charge controller.
ModbusMaster node;

// Create software serial for communicating with Charge Controller.
SoftwareSerial mySerial(MAX3232_RX, MAX3232_TX);

// Store all the raw data collected from the charge controller.
uint16_t chargeControllerRegisterData[NUM_REGISTERS];

// Was there an error when reading from the charge controller?
uint8_t modbusErr;

// Last time isTime() was run and returned 1.
unsigned long lastTime;

void setup() {
    Serial.begin(115200);

    // Start the software serial for the modbus connection.
    mySerial.begin(9600);

    node.begin(MODBUS_SLAVE_ADDR, mySerial);

    state = WAIT;

    delay(SETUP_FINISH_DELAY);

    lastTime = millis();
}

/*
    millis() Rollover safe time delay tracking.
*/
uint8_t isTime() {
    if (millis() - lastTime >= REQUEST_DELAY) {
        lastTime = millis();
        return 1;
    }
    return 0;
}

/*
    Helper function to handle the sign bit for negative temperatures.
*/
int getRealTemp(int temp) {
    return temp / 128 ? -(temp % 128) : temp;
}

/*
    Convert the charge controller register data into a formatted JSON string and print to console.
    Register addresses were figured out from the modbus document.
*/
void registerToJson() {
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;
    if (modbusErr) {
        doc["modbusError"] = true;
    } else {
        // We need to account for the load being on when determining the charging mode.
        int loadOffset = chargeControllerRegisterData[32] > 6 ? 32768 : 0;

        doc["modbusError"] = false;

        JsonObject controller = doc.createNestedObject("controller");
        controller["chargingMode"] = chargeModes[chargeControllerRegisterData[32] - loadOffset];
        controller["temperature"] = getRealTemp(chargeControllerRegisterData[3] >> 8);
        controller["days"] = chargeControllerRegisterData[21];
        controller["overDischarges"] = chargeControllerRegisterData[22];
        controller["fullCharges"] = chargeControllerRegisterData[23];

        JsonObject charging = doc.createNestedObject("charging");
        charging["amps"] = chargeControllerRegisterData[2] * 0.01;
        charging["maxAmps"] = chargeControllerRegisterData[13] * 0.01;
        charging["watts"] = chargeControllerRegisterData[9];
        charging["maxWatts"] = chargeControllerRegisterData[15];
        charging["dailyAmpHours"] = chargeControllerRegisterData[17];
        charging["totalAmpHours"] = ((chargeControllerRegisterData[24] * 65536 + chargeControllerRegisterData[25]) * 0.001);
        charging["dailyPower"] = chargeControllerRegisterData[19] * 0.001;
        charging["totalPower"] = ((chargeControllerRegisterData[28] * 65536 + chargeControllerRegisterData[29]) * 0.001);

        JsonObject battery = doc.createNestedObject("battery");
        battery["stateOfCharge"] = chargeControllerRegisterData[0];
        battery["volts"] = chargeControllerRegisterData[1] * 0.1;
        battery["minVolts"] = chargeControllerRegisterData[11] * 0.1;
        battery["maxVolts"] = chargeControllerRegisterData[12] * 0.1;
        battery["temperature"] = getRealTemp(chargeControllerRegisterData[3] & 0xFF);

        JsonObject panels = doc.createNestedObject("panels");
        panels["volts"] = chargeControllerRegisterData[7] * 0.1;
        panels["amps"] = chargeControllerRegisterData[8] * 0.01;

        JsonObject load = doc.createNestedObject("load");
        load["state"] = chargeControllerRegisterData[10] ? true : false;
        load["volts"] = chargeControllerRegisterData[4] * 0.1;
        load["amps"] = chargeControllerRegisterData[5] * 0.01;
        load["watts"] = chargeControllerRegisterData[6];
        load["maxAmps"] = chargeControllerRegisterData[14] * 0.01;
        load["maxWatts"] = chargeControllerRegisterData[16];
        load["dailyAmpHours"] = chargeControllerRegisterData[18];
        load["totalAmpHours"] = ((chargeControllerRegisterData[26] * 65536 + chargeControllerRegisterData[27]) * 0.001);
        load["dailyPower"] = chargeControllerRegisterData[20] * 0.001;
        load["totalPower"] = ((chargeControllerRegisterData[30] * 65536 + chargeControllerRegisterData[31]) * 0.001);

        JsonArray faults = doc.createNestedArray("faults");
        int faultId = chargeControllerRegisterData[34];
        uint8_t count = 0;
        while (faultId != 0) {
            if (faultId >= pow(2, 15 - count)) {
                faults.add(faultCodes[count - 1]);
                faultId -= pow(2, 15 - count);
            }
            count += 1;
        }
    }
    serializeJsonPretty(doc, Serial);
    Serial.println();
}

/*
    Request data from the charge controller and store it.
*/
void readNode() {
    static uint32_t i;
    i++;
    // set word 0 of TX buffer to least-significant word of counter (bits 15..0)
    node.setTransmitBuffer(0, lowWord(i));
    // set word 1 of TX buffer to most-significant word of counter (bits 31..16)
    node.setTransmitBuffer(1, highWord(i));

    uint8_t result = node.readHoldingRegisters(MODBUS_REQUEST_START_ADDR, NUM_REGISTERS);
    if (result == node.ku8MBSuccess) {
        modbusErr = 0;
        Serial.println(F("Successfully read from CC"));

        for (int j = 0; j < NUM_REGISTERS; j++) {
            chargeControllerRegisterData[j] = node.getResponseBuffer(j);
        }
    } else {
        modbusErr = 1;
        Serial.print(F("Failed to read from CC"));
        Serial.print(F(" ("));
        Serial.print(result, HEX);
        Serial.println(F(")"));
    }
}

void loop() {
    if (state == WAIT && isTime()) {
        state = QUERY;
    } else if (state == QUERY) {
        readNode();
        state = TRANSMIT;
    } else if (state == TRANSMIT) {
        // Handle printing to console in this function.
        registerToJson();
        state = WAIT;
    } else {
        state = WAIT;
    }
}
