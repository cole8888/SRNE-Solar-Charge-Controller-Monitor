/*
    Cole L - 23 April 2023 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
    
    This is a simplified example program to use an ESP32 to read data from a single SRNE solar charge controllers and print it to the console.
    For multiple controllers and more features check out My-Current-Setup-ESP32.

    This example uses HardwareSerial to connect to a single charge controller on GPIO pins 16 (RX) and 17 (TX)

    See images and schematic for wiring details.
*/

/* 
    Required libraries.
*/
#include <ModbusMaster.h>   // https://github.com/4-20ma/ModbusMaster
#include <ArduinoJson.h>    // https://github.com/bblanchon/ArduinoJson

/*
    Modbus Constants

    All charge controllers will respond to address 255 no matter what their actual address is, this is useful if you do not know what address to use.
    You can try using 1 here instead of 255, but it is not guaranteed to work.
*/
#define NUM_REGISTERS 35
#define MODBUS_SLAVE_ADDR 255
#define MODBUS_REQUEST_START_ADDR 256

/*
    Other settings.
*/
#define REQUEST_DELAY 3000                                 // Minimum delay in ms between rounds of polling all the charge controllers.
#define SETUP_FINISH_DELAY 100                             // Delay in ms to wait after setup has finished to allow everything to settle down.
#define JSON_BUFFER_SIZE 2048                              // Maximum size for the JSON string. It's actually around ~1600.

/*
    RX and TX pins for serial ports.
    If using my PCB design, do not change these.
    If you plan to change these, make sure you verify your pin is usable, eg: any ADC2 pins cannot be used since WiFi needs them.
    Heads up, some of these are input only.
*/
const uint8_t serialPins[2][6] = {{16, 18, 21, 34, 35, 36}, {17, 19, 22, 23, 32, 33}};

/*
    Describes the different states the program can be in. Repeats for each controller.
*/
enum STATE {
    WAIT     = 0,
    QUERY    = 1,
    TRANSMIT = 2
};
STATE state;

/*
    Array of charging mode strings.
    These are the states the charge controller can be in when charging the battery.
*/
const char* chargeModes[7]={
    "OFF",      //0
    "NORMAL",   //1
    "MPPT",     //2
    "EQUALIZE", //3
    "BOOST",    //4
    "FLOAT",    //5
    "CUR-LIM"   //6 (Current limiting)
};

/*
    Array of fault codes.
    These are all the possible faults the charge controller can indicate.
    Multiple faults can be thrown at the same time.
*/
const char* faultCodes[15] = {
    "Charge MOS short circuit",     // (16384 | 01000000 00000000)
    "Anti-reverse MOS short",       // (8192  | 00100000 00000000)
    "PV panel reversely connected", // (4096  | 00010000 00000000)
    "PV working point over voltage",// (2048  | 00001000 00000000)
    "PV counter current",           // (1024  | 00000100 00000000)
    "PV input side over-voltage",   // (512   | 00000010 00000000)
    "PV input side short circuit",  // (256   | 00000001 00000000)
    "PV input overpower",           // (128   | 00000000 10000000)
    "Ambient temp too high",        // (64    | 00000000 01000000)
    "Controller temp too high",     // (32    | 00000000 00100000)
    "Load over-power/current",      // (16    | 00000000 00010000)
    "Load short circuit",           // (8     | 00000000 00001000)
    "Battery undervoltage warning", // (4     | 00000000 00000100)
    "Battery overvoltage",          // (2     | 00000000 00000010)
    "Battery over-discharge"        // (1     | 00000000 00000001)
};

// Create the modbus node the charge controller.
ModbusMaster node;

// Store all the raw data collected from the charge controller.
uint16_t chargeControllerRegisterData[NUM_REGISTERS]; 

// Was there an error when reading from the charge controller?
uint8_t modbusErr;

// Last time isTime() was run and returned 1.
unsigned long lastTime;

// Store the formatted json string for publishing over MQTT.
char registerDataJson[JSON_BUFFER_SIZE];

void setup(){
    Serial.begin(115200);
    Serial.println(F("Started serial!"));

    // Setup the serial ports and assign them to the modbus nodes.
    Serial2.begin(9600, SERIAL_8N1, serialPins[0][0], serialPins[1][0]);
    node.begin(MODBUS_SLAVE_ADDR, Serial2);

    state = WAIT;
    
    delay(SETUP_FINISH_DELAY);
    
    lastTime = millis();
}

/*
    Helper function to handle the sign bit for negative temperatures.
*/
int getRealTemp(int temp){
    return temp/128 ? -(temp%128) : temp;
}

/*
    millis() Rollover safe time delay tracking.
*/
uint8_t isTime(){
    if(millis() - lastTime >= REQUEST_DELAY){
        lastTime = millis();
        return 1;
    }
    return 0;
}

/*
    Convert the charge controller register data into a formatted JSON string.
    Register addresses were figured out from the modbus document.
*/
void registerToJson(){
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;
    if(modbusErr){
        doc["modbusError"] = true;
    }
    else{
        // We need to account for the load being on when determining the charging mode.
        int loadOffset = chargeControllerRegisterData[32] > 6 ? 32768 : 0;
        
        doc["modbusError"] = false;

        JsonObject controller = doc.createNestedObject("controller");
        controller["chargingMode"] = chargeModes[chargeControllerRegisterData[32] - loadOffset];
        controller["controllerTemp"] = getRealTemp(chargeControllerRegisterData[3] >> 8);
        controller["days"] = chargeControllerRegisterData[21];
        controller["overDischarges"] = chargeControllerRegisterData[22];
        controller["fullCharges"] = chargeControllerRegisterData[23];

        JsonObject charge = doc.createNestedObject("charge");
        charge["amps"] = chargeControllerRegisterData[2]*0.01;
        charge["maxAmps"] = chargeControllerRegisterData[13]*0.01;
        charge["watts"] = chargeControllerRegisterData[9];
        charge["maxWatts"] = chargeControllerRegisterData[15];
        charge["dailyAmpHours"] = chargeControllerRegisterData[17];
        charge["totalAmpHours"] = ((chargeControllerRegisterData[24]*65536 + chargeControllerRegisterData[25])*0.001);
        charge["dailyPower"] = chargeControllerRegisterData[19]*0.001;
        charge["totalPower"] = ((chargeControllerRegisterData[28]*65536 + chargeControllerRegisterData[29])*0.001);

        JsonObject battery = doc.createNestedObject("battery");
        battery["stateOfCharge"] = chargeControllerRegisterData[0];
        battery["volts"] = chargeControllerRegisterData[1]*0.1;
        battery["minVolts"] = chargeControllerRegisterData[11]*0.1;
        battery["maxVolts"] = chargeControllerRegisterData[12]*0.1;
        battery["battTemp"] = getRealTemp(chargeControllerRegisterData[3] & 0xFF);

        JsonObject panels = doc.createNestedObject("panels");
        panels["panelVolts"] = chargeControllerRegisterData[7]*0.1;
        panels["panelAmps"] = chargeControllerRegisterData[8]*0.01;

        JsonObject load = doc.createNestedObject("load");
        load["status"] = chargeControllerRegisterData[10];
        load["voltage"] = chargeControllerRegisterData[4]*0.1;
        load["amps"] = chargeControllerRegisterData[5]*0.01;
        load["watts"] = chargeControllerRegisterData[6];
        load["maxAmps"] = chargeControllerRegisterData[14]*0.01;
        load["maxWatts"] = chargeControllerRegisterData[16];
        load["dailyAmpHours"] = chargeControllerRegisterData[18];
        load["totalAmpHours"] = ((chargeControllerRegisterData[26]*65536 + chargeControllerRegisterData[27])*0.001);
        load["dailyPower"] = chargeControllerRegisterData[20]*0.001;
        load["totalPower"] = ((chargeControllerRegisterData[30]*65536 + chargeControllerRegisterData[31])*0.001);

        JsonArray faults = doc.createNestedArray("faults");
        int faultId = chargeControllerRegisterData[34];
        uint8_t count = 0;
        while(faultId != 0){
            if(faultId >= pow(2, 15-count)){
                faults.add(faultCodes[count-1]);
                faultId -= pow(2, 15-count);
            }
            count += 1;
        }
    }
    // serializeJson(doc, registerDataJson);
    serializeJsonPretty(doc, registerDataJson);
}

/*
    Request data from the charge controller and store it.
*/
void readNode(){
    static uint32_t i;
    i++;
    // set word 0 of TX buffer to least-significant word of counter (bits 15..0)
    node.setTransmitBuffer(0, lowWord(i));
    // set word 1 of TX buffer to most-significant word of counter (bits 31..16)
    node.setTransmitBuffer(1, highWord(i));
    

    uint8_t result = node.readHoldingRegisters(MODBUS_REQUEST_START_ADDR, NUM_REGISTERS);
    if (result == node.ku8MBSuccess){
        modbusErr = 0;
        Serial.println(F("Successfully read from CC"));

        for (int j = 0; j < NUM_REGISTERS; j++){
            chargeControllerRegisterData[j] = node.getResponseBuffer(j);
        }
    }
    else{
        modbusErr = 1;
        Serial.print(F("Failed to read from CC"));
        Serial.print(F(" ("));
        Serial.print(result, HEX);
        Serial.println(F(")"));
    }
}

void loop(){
    if(state == WAIT){
        if(isTime()){
            state = QUERY;
        }
    }
    else if(state == QUERY){
        readNode();
        state = TRANSMIT;
    }
    else if(state == TRANSMIT){
        registerToJson();
        
        // Just an example program, only print the data.
        Serial.println(registerDataJson);
        
        state = WAIT;
    }
    else{
        state = WAIT;
    }
}
