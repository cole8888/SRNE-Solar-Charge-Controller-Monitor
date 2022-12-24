/*
    Cole L - 24th December 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
    
    This is an example to retrieve data from multiple charge controllers. All this program does is print the data to the console.
    This should give you enough information on how to read the values so you can do more exciting stuff with the data.
    I would suggest looking into setting up an MQTT server so you can view the data on other devices.
    
    This code runs on an ESP8266 NODEMCU 0.9 module.
*/

#include <RF24.h>              // Library version 1.3.12 works, 1.4.0 did not.
#include <RF24Network.h>       // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <math.h>              // Use this for pow().

/*
    I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once 
    from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
    Other constant and settings.
*/
#define NUM_CHARGE_CONTROLLERS 2   // Number of nodes that will report charge controller data. Addresses of charge controller nodes must start counting from 1.
#define RECEIVE_NODE_ADDR 0        // Address of node which will be receiving the data. (Address of this node, don't change this).
#define RF24_NETWORK_CHANNEL 90    // Channel the RF24 network should use.
#define SETUP_FAIL_DELAY 2000      // Delay when retrying setup tasks.
#define INVALID_SENDER_DELAY 25    // Delay after receiving a transmission from an invalid sender.

/*
    Structure for the charge controller data from the charge controller nodes.
*/
typedef struct package{
    uint16_t modbusErrDiff;                    // Number of new modbus errors since last transmission.
    uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller
    uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller
}Package;
Package chargeControllerNodeData[NUM_CHARGE_CONTROLLERS];

/*
    2D Array of pointers to make it easier to retrieve charge controller data.
*/
uint16_t* chargeControllerRegisterData[NUM_CHARGE_CONTROLLERS][NUM_REGISTERS_REQUEST1 + NUM_REGISTERS_REQUEST2];

/*
    Array of charging mode strings.
    These are the states the charge controller can be in when charging the battery.
*/
const char* chargeModes[7]={
    "OFF",             //0
    "NORMAL",          //1
    "MPPT",            //2
    "EQUALIZE",        //3
    "BOOST",           //4
    "FLOAT",           //5
    "CURRENT_LIMITING" //6
};

/*
    Array of fault codes.
    These are all the possible faults the charge controller can indicate.
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

// Create the RF24 Radio and network.
RF24 radio(D4, D8); // CE, CSN
RF24Network network(radio);

void setup(){
    while (!Serial);
    Serial.begin(115200);
    
    // Startup the RF24
    while(!radio.begin()){
        Serial.print(F("\nUnable to start RF24.\n"));
        delay(SETUP_FAIL_DELAY);
    }
    network.begin(RF24_NETWORK_CHANNEL, RECEIVE_NODE_ADDR);
    Serial.println(F("RF24 Initialized"));

    // Make a 2D array of pointers to make it easier to access the charge controller data.
    // Initialize pointers array for request1 registers.
    for(int i = 0; i<NUM_REGISTERS_REQUEST1; i++){
        for(int j = 0; j<NUM_CHARGE_CONTROLLERS; j++){
            chargeControllerRegisterData[j][i] = &chargeControllerNodeData[j].request1[i];
        }
    }
    // Initialize pointers array for request2 registers.
    for(int i = 0; i<NUM_REGISTERS_REQUEST2; i++){
        for(int j = 0; j<NUM_CHARGE_CONTROLLERS; j++){
            chargeControllerRegisterData[j][i + NUM_REGISTERS_REQUEST1] = &chargeControllerNodeData[j].request2[i];
        }
    }

    delay(1500); // Give enough time to read the details from the display.
}

/*
    Helper function to handle the sign bit for negative temperatures.
*/
int getRealTemp(int temp){
    return temp/128 ? -(temp%128) : temp;
}

/*
    Print all the charge controller data to the serial monitor.
*/
void printAllData(int nodeId){
    Serial.print(F("------------ Data From Node "));
    Serial.print(nodeId);
    Serial.println(F(" ------------"));

    Serial.println(F("------------- Real Time Data -------------"));

    // Print the charging mode of the charge controller:
    int loadOffset = *chargeControllerRegisterData[nodeId][32] > 6 ? 32768 : 0;
    Serial.print(F("Charging Mode:\t\t\t"));
    Serial.println(chargeModes[*chargeControllerRegisterData[nodeId][32] - loadOffset]);

    // Print the battery state of charge:
    Serial.print(F("Battery SOC:\t\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][0]);
    Serial.println(F("%"));

    // Print the battery voltage:
    Serial.print(F("Battery Voltage:\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][1]*0.1));
    Serial.println(F("V"));

    // Print the battery charge current:
    Serial.print(F("Battery Charge Current:\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][2]*0.01));
    Serial.println(F("A"));

    // Print the controller temperature:
    Serial.print(F("Controller Temperature:\t\t"));
    Serial.print(getRealTemp(*chargeControllerRegisterData[nodeId][3] >> 8));
    Serial.println(F("*C"));

    // Print the battery temperature:
    Serial.print(F("Battery Temperature:\t\t"));
    Serial.print(getRealTemp(*chargeControllerRegisterData[nodeId][3] & 0xFF));
    Serial.println(F("*C"));

    // Print the load voltage:
    Serial.print(F("Load Voltage:\t\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][4]*0.1));
    Serial.println(F("V"));

    // Print the load current:
    Serial.print(F("Load Current:\t\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][5]*0.01));
    Serial.println(F("A"));

    // Print the load power:
    Serial.print(F("Load Power:\t\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][6]);
    Serial.println(F("W"));

    // Print the panel volts:
    Serial.print(F("Panel Volts:\t\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][7]*0.1));
    Serial.println(F("V"));

    // Print the panel amps:
    Serial.print(F("Panel Amps:\t\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][8]*0.01));
    Serial.println(F("A"));

    // Print the panel power:
    Serial.print(F("Panel Power: \t\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][9]);
    Serial.println(F("W"));

    Serial.println("--------------- DAILY DATA ---------------");

    // Print the daily battery minimum voltage:
    Serial.print(F("Battery Minimum Voltage:\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][11]*0.1));
    Serial.println(F("V"));

    // Print the daily battery minimum voltage:
    Serial.print(F("Battery Maximum Voltage:\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][12]*0.1));
    Serial.println(F("V"));

    // Print the daily maximum charge current:
    Serial.print(F("Maximum Charge Current:\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][13]*0.01));
    Serial.println(F("A"));

    // Print the daily maximum load current:
    Serial.print(F("Maximum Load Discharge Current:\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][14]*0.01));
    Serial.println(F("A"));

    // Print the daily maximum charge power: 
    Serial.print(F("Maximum Charge Power:\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][15]);
    Serial.println(F("W"));

    // Print the daily maximum load power: 
    Serial.print(F("Maximum Load Discharge Power:\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][16]);
    Serial.println(F("W"));

    // Print the daily charge amp hours:
    Serial.print(F("Charge Amp Hours:\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][17]);
    Serial.println(F("Ah"));

    // Print the daily load amp hours:
    Serial.print(F("Load Amp Hours:\t\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][18]);
    Serial.println(F("Ah"));

    // Print the daily charge power:
    Serial.print(F("Charge Power:\t\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][19]*0.001));
    Serial.println(F("KWh"));

    // Print the daily load power:
    Serial.print(F("Load Power:\t\t\t"));
    Serial.print((*chargeControllerRegisterData[nodeId][20]*0.001));
    Serial.println(F("KWh"));

    Serial.println(F("------------- LIFETIME DATA --------------"));

    // Print the number of days operational:
    Serial.print(F("Days Operational:\t\t"));
    Serial.print(*chargeControllerRegisterData[nodeId][21]);
    Serial.println(F("Days"));

    // Print the number of times batteries were over discharged:
    Serial.print(F("Times Over Discharged:\t\t"));
    Serial.println(*chargeControllerRegisterData[nodeId][22]);

    // Print the number of times batteries were fully charges:
    Serial.print(F("Times Fully Charged:\t\t"));
    Serial.println(*chargeControllerRegisterData[nodeId][23]);

    // Print the cumulative amp hours:
    Serial.print(F("Cumulative Amp Hours:\t\t"));
    Serial.print(((*chargeControllerRegisterData[nodeId][24]*65536 + *chargeControllerRegisterData[nodeId][25])*0.001));
    Serial.println(F("KAh"));

    // Print the cumulative load amp hours:
    Serial.print(F("Load Amp Hours:\t\t\t"));
    Serial.print(((*chargeControllerRegisterData[nodeId][26]*65536 + *chargeControllerRegisterData[nodeId][27])*0.001));
    Serial.println(F("KAh"));

    // Print the cumulative power hours:
    Serial.print(F("Cumulative Power:\t\t"));
    Serial.print(((*chargeControllerRegisterData[nodeId][28]*65536 + *chargeControllerRegisterData[nodeId][29])*0.001));
    Serial.println(F("KWh"));

    // Print the cumulative load amp hours:
    Serial.print(F("Load Power:\t\t\t"));
    Serial.print(((*chargeControllerRegisterData[nodeId][30]*65536 + *chargeControllerRegisterData[nodeId][31])*0.001));
    Serial.println(F("KWh"));

    Serial.println(F("--------------- FAULT DATA ---------------"));

    // Determine if there are any faults / what they mean.
    String faults = "- None :)";
    int faultID = *chargeControllerRegisterData[nodeId][34];
    if(faultID != 0){
        if(faultID > 16384 || faultID < 0){
            faults = "- Invalid fault code!";
        }
        else{
            faults = "";
            int count = 0;
            int faultCount = 0;
            while(faultID != 0){
                if(faultID >= pow(2, 15-count)){
                    // If there is more than one error, make a new line.
                    if(faultCount > 0){
                        faults += "\n";
                    }
                    faults += "- ";
                    faults += faultCodes[count-1];
                    faultID -= pow(2, 15-count);
                    faultCount++;
                }
                count++;
            }
        }
    }
    // Print the faults
    Serial.println(F("Faults:"));
    Serial.println(faults);

    // Print the raw fault data
    Serial.print(F("RAW Fault Data:\t\t\t"));
    Serial.println(*chargeControllerRegisterData[nodeId][34]);
    Serial.println(F("------------------------------------------\n"));
}

void loop(){
    network.update();
    if(network.available()){
        while(network.available()){
            RF24NetworkHeader header;                                             
            network.peek(header);

            // See which node this transmission is from.
            if(header.from_node <= NUM_CHARGE_CONTROLLERS && header.from_node > 0){
                int index = header.from_node - 1; // Need to subtract 1 extra because 0 is receiver node.
                network.read(header, &chargeControllerNodeData[index], sizeof(chargeControllerNodeData[index]));
                delay(10);

                printAllData(index);
            }
            else{
                Serial.print(F("\nInvalid Sender: "));
                Serial.println(header.from_node);
                delay(INVALID_SENDER_DELAY);
            }
        }
    }
}
