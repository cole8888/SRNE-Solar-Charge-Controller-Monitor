/*
    Cole Lightfoot - 30th September 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
    This code runs on an ESP8266 NODEMCU 0.9 module.
    There are two arduinos inside the deck box and each one reads data from one of the two charge controllers (CC1 and CC2).
    This data along with some other data such as environment sensor data and direct sensor measurements are then transmitted to this receiver.
    This ESP8266 then processes the data from the transmitters and then publishes it to an MQTT server as well as displays it on an attached LCD.

    Attached components are:
    - Generic 20x4 character I2C lcd display
    - RF24 Module
    - Capacitors on the 3.3V and 5V power rails to prevent voltage drops.
*/

#include <RF24.h>              // Library version 1.3.12 works, 1.4.0 did not.
#include <RF24Network.h>       // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <LiquidCrystal_I2C.h> // LCD
#include <PubSubClient.h>      // Allows us to connect to, and publish to the MQTT broker
#include <ESP8266WiFi.h>       // Enables The WiFi radio (Not RF24) on the ESP8266.
// #include <math.h>              // Use this for pow().

/*
    WiFi Settings.
*/
#define WIFI_SSID "YOUR_SSID_HERE"
#define WIFI_PASS "YOUR_PASSWORD_HERE"

/*
    MQTT related settings. You many need to change these depending on your implementation.
*/
#define MQTT_PORT 1883                   // Port to use for MQTT.
#define MQTT_SERVER_ADDR "192.168.2.100" // Address of the MQTT server.
#define MQTT_CLIENT_ID "ESP8266"         // Client identifier for MQTT.
#define NUM_CC_MQTT_TOPICS 22            // Number of Charge Controller related MQTT topics.
#define NUM_MISC_MQTT_TOPICS 7           // Number of misc data related MQTT topics.

/*
    Offset of the ACS724 current sensors used for the misc data.
    In theory these should be 2.5 but my sensors kinda suck.
    Found experimentally, your numbers will differ.
*/
#define FRONT_AMPS_SENSOR_A_OFFSET 2.23 
#define FRONT_AMPS_SENSOR_B_OFFSET 2.36
#define BACK_AMPS_SENSOR_OFFSET 2.40

/* 
    Multipliers for the ADC and Current sensors.
*/
#define ADS1115_VPB 0.0001875 // Volts per bit of the ADS1115 ADC (6.144V/32768).
#define ACS724_VPA 0.066      // Volts per amp of the ACS724 current sensor.

/*
    Some logic in this program assumes that node1 will be reporting additional data along with it's charge controller data unlike the other nodes.
    If this is not the case for your application you will need to make some adjustments to make sure it can handle this, reach out if you need help.
*/
#define NUM_MISC_DATA_NODES 1  // Number of nodes which will be reporting misc data.
#define MISC_DATA_NODE_ADDR 1  // Address of node which will also be reporting misc data.

/*
    I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once 
    from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
    Other constant and settings.
*/
#define NUM_CHARGE_CONTROLLERS 2  // Number of connected nodes that will report charge controller data. Includes ones reporting misc data.
#define CHARGE_MODE_TOPIC_INDEX 1 // Index of the Charging Mode topic. Need to keep track of this since pub() needs to treat it differently.
#define MODBUS_ERR_TOPIC_INDEX 0  //
#define RECEIVE_NODE_ADDR 0       // Address of node which will be receiving the data. (Address of this node, don't change this).
#define CC_TOPIC_PREFIX "CC"      // Prefix for all charge controller related MQTT topics. Will also have a number after this. Ex: CC1chargingMode.
#define RF24_NETWORK_CHANNEL 90   // Channel the RF24 network should use.

/*
    This is the structure which is transmitted from node1/charge controller 1 to the receiver.
    Front panel amps are split into two parts because the ACS724 only goes up to 30A and I needed 60A, so I placed two ACS724 sensors in parallel.
    These are the raw sensor values and they need to be translated into their actual values later.
*/
typedef struct package{
    float panelAmpsBack;    // Measured Back panel amps (ACS712 current sensor)
    float panelAmpsFrontA;  // Measured Front panel amps sensor 1/2 (ACS724 curent sensor)
    float panelAmpsFrontB;  // Measured Front panel amps sensor 1/2 (ACS724 curent sensor)
    // float batteryVolts;      // Measured battery voltage (ADS1115 and a voltage divider. R1=56K, R2=10K) // Input 0 of my ADC is broken, enable if you need this.
    int32_t bmePres;        // Atmospheric pressure measured by BME680
    int32_t bmeTemp;        // Air temperature measured by BME680
    int32_t bmeHum;         // Air humidity measured by BME680
    int32_t bmeGas;         // Volatile Organic Compounds measured by BME680
    uint16_t modbusErrDiff; // Number of new modbus errors since last transmission.
    uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller
    uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller
}Package;
Package miscNodeData;

/*
    Structure for the charge controller data from other nodes.
*/
typedef struct package2{
	uint16_t modbusErrDiff; // Number of new modbus errors since last transmission.
    uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller
    uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller
}Package2;
Package2 otherNodeData[NUM_CHARGE_CONTROLLERS - NUM_MISC_DATA_NODES];

/*
    2D Array of pointers used to retrieve charge controller data.
*/
uint16_t* registersMembers[NUM_CHARGE_CONTROLLERS][NUM_REGISTERS_REQUEST1 + NUM_REGISTERS_REQUEST2];

/*
    Array of all MQTT topic names related to the charge controllers.
    "CCX" will be appended to the start, X indicating the node/charge controller number.
    You can change these as you see fit, just make sure whatever you are using to receive them also uses the same topic names.
*/
const char* chargeControllerTopics[NUM_CC_MQTT_TOPICS]={
    "modbusErrDiff",         //0  Number of new modbus errors since last refresh.
    "chargingMode",          //1  Charging mode
    "SOC",                   //2  Battery State of Charge percentage
    "battVolts",             //3  Battery voltage
    "battAmps",              //4  Current flowing from charge controller to battery
    "controllerTemp",        //5  Charge controller temperature
    "battTemp",              //6  Battery temperature sensor reading
    "panelVolts",            //7  Voltage from panels to charge controller
    "panelAmps",             //8  Current flowing from panels to charge controller
    "panelWatts",            //9  Charging watts coming from the panels
    "battMinVolts",          //10 Minimum battery voltage seen today
    "battMaxVolts",          //11 Maximum battery voltage seen today
    "battMaxChargeCurrent",  //12 Maximum current flowing from charge controller to battery seen today
    "panelMaxPower",         //13 Maximum charging watts seen today
    "chargeAmpHours",        //14 Accumulated amp hours from today so far
    "dailyPower",            //15 Accumulated kilo watt hours from today so far
    "numDays",               //16 Number of days active
    "numOverCharges",        //17 Number of times the battery has been overcharged
    "numFullCharges",        //18 Number of times the battery has been fully charged
    "totalAmpHours",         //19 Total kilo amp hours accumulated over lifetime
    "totalPower",            //20 Total kilo watt hours accumulated over lifetime
    "faults"                 //21 Fault data
};

/*
    Array of topics not related to the charge controller data.
    These are sent by the misc data node.
    These are treated by pub() as if their index is part of chargeControllerTopics.
*/
const char* miscDataTopics[NUM_MISC_MQTT_TOPICS]={
    "dcAmpsB",  //0 DC Amps Back
    "dcAmpsF",  //1 DC Amps Front
    "dcVolts",  //2 DC Battery Volts
    "Temp",     //3 Box Air Temperature
    "Hum",      //4 Box Air Humidity
    "Pres",     //5 Box Air Pressure
    "Gas"       //6 Box Air VOC Reading
};

/*
    Array of charging mode strings.
    These are the states the charge controller can be in when charging the battery.
*/
const char* chargeModes[7]={
    "OFF",              //0
    "NORMAL",           //1
    "MPPT",             //2
    "EQUALIZE",         //3
    "BOOST",            //4
    "FLOAT",            //5
    "CURRENT_LIMITING"  //6
};

/*
    Custom symbols for the LCD.
*/
byte omegaSymbol[] = { // Omega Symbol (Ohms)
    0x00,0x0E,0x11,0x11,0x11,0x0A,0x1B,0x00
};
byte degC[] = { // Degrees Celsius Symbol
    0x18,0x18,0x07,0x08,0x08,0x08,0x07,0x00
};

/*
    Hold converted bme readings.
*/
float bmePres, bmeTemp, bmeHum, bmeGas;

/*
	Used to avoid trying to publish data to mqtt if it failed to connect on startup.
*/
short mqttConnected;

// Create the LCD device.
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Create the RF24 Radio and network.
RF24 radio(D4, D8); // CE, CSN
RF24Network network(radio);

// Create the WiFi and MQTT clients.
WiFiClient wifiClient;
PubSubClient client(MQTT_SERVER_ADDR, MQTT_PORT, wifiClient);

void setup(){
    while (!Serial);
    Serial.begin(115200);
    
    lcd.begin(); // Startup LCD
    
    // Load custom characters into display ram.
    lcd.createChar(0, omegaSymbol);
    lcd.createChar(1, degC);

    lcd.backlight(); // Turn on the LCD backlight
    lcd.setCursor(0, 0);
    lcd.print(F("Connecting"));

    // Startup the RF24
    while(!radio.begin()){
        Serial.print(F("\nUnable to start RF24. Trying in 2 seconds.\n"));
        delay(2000);
    }
    network.begin(RF24_NETWORK_CHANNEL, RECEIVE_NODE_ADDR);
    Serial.println(F("RF24 Initialized"));

    WiFi.begin(WIFI_SSID, WIFI_PASS); // Begin WiFi connection
    // Wait for connection
    int trys = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if(trys > 20){
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print(F("WiFi Disconnected :("));
            break;
        }
        delay(500);
        Serial.print(F("."));
        lcd.print(F("."));
        trys++;
    }
    if(WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("Connected :)"));
        lcd.setCursor(0, 1);
        lcd.print(F("SSID: "));
        lcd.print(WIFI_SSID);
        lcd.setCursor(0, 2);
        lcd.print(F("IP: "));
        lcd.print(WiFi.localIP());
        lcd.setCursor(0, 3);

        Serial.print(F("\nConnected to "));
        Serial.println(WIFI_SSID);
        Serial.print(F("IP address: "));
        Serial.println(WiFi.localIP());
    }

    // Connect to MQTT Broker
    if (client.connect(MQTT_CLIENT_ID)) {
        lcd.print(F("MQTT Connected :)"));
        Serial.println(F("MQTT Connected"));
		mqttConnected = 1;
    }
    else {
        lcd.print(F("MQTT Failed :("));
        Serial.println(F("MQTT Connection failed..."));
		mqttConnected = 0;
    }

    // Initialize pointers array for request1 registers.
    for(int i = 0; i<NUM_REGISTERS_REQUEST1; i++){
        registersMembers[0][i] = &miscNodeData.request1[i];
        for(int j = NUM_MISC_DATA_NODES; j<NUM_CHARGE_CONTROLLERS; j++){
            registersMembers[j][i] = &otherNodeData[j - NUM_MISC_DATA_NODES].request1[i];
        }
    }
    // Initialize pointers array for request2 registers.
    for(int i = 0; i<NUM_REGISTERS_REQUEST2; i++){
        registersMembers[0][i + NUM_REGISTERS_REQUEST1] = &miscNodeData.request2[i];
        for(int j = NUM_MISC_DATA_NODES; j<NUM_CHARGE_CONTROLLERS; j++){
            registersMembers[j][i + NUM_REGISTERS_REQUEST1] = &otherNodeData[j - NUM_MISC_DATA_NODES].request2[i];
        }
    }

	bmePres = bmeTemp = bmeHum = bmeGas = 0;

    delay(1500); // Give enough time to read the details from the display.
}

/*
    Print data to the attached LCD.
*/
void printLCD(){
    // Prepare display contents for ahead of time.
    // By doing this we decrease how long the display blanks for.

    // Wat: F:XXXXW B:XXXXW
	String row1 = "Wat: F:";
    // Is the front charge controller offline?
    if(miscNodeData.modbusErrDiff){
		row1 += "----W";
	}
	else{
        int watts = (int)(*registersMembers[0][9]);
		row1 += watts;
        row1 += "W";
        
        // Generate the correct spacing after the front watts number.
        if(!watts){
            row1 += "   ";
        }
        else{
            while (watts/1000 == 0) {
                watts *= 10;
                row1 += " ";
            }
        }
	}
	row1 += " B:";
    // Is the back charge controller offline?
    if(otherNodeData[0].modbusErrDiff){
        row1 += "----";
    }
    else{
        row1 += (int)(*registersMembers[1][9]);
    }
	row1 += "W";
	
	// Bat: XX.XV | XX.XV
	char row2[21];
    sprintf(row2, "Bat: %.1fV | %.1fV", *registersMembers[1][1]*0.05, *registersMembers[1][1]*0.1);

	// Tmp: XX.X°C Gas:XXXXX
    char row3_0[11];
    const char* tempLabel = bmeTemp > -10 ? "Tmp: " : "Tmp:";
    sprintf(row3_0, "%s%.1f", tempLabel, bmeTemp);
    char row3_1[11];
    if(bmeGas >= 1000){
		if(bmeGas >= 10000){
			sprintf(row3_1, " Gas:%dM", (int)bmeGas/1000);
		}
		else{
        	sprintf(row3_1, " Gas:%.1fM", bmeGas/1000.0);
		}
    }
    else{
        sprintf(row3_1, " Gas:%dk", (int)bmeGas);
    }

	// Pre: XXXXhPa Hum:XX% 
	char row4[21];
	const char* presLabel = bmeHum >= 100 && bmePres >= 1000 ? "Pre:" : "Pre: ";
	sprintf(row4, "%s%dhPa Hum:%d%%", presLabel, (int)bmePres, (int)bmeHum);

    lcd.clear();

    lcd.print(row1);

    lcd.setCursor(0, 1);
	lcd.print(row2);
	
	lcd.setCursor(0, 2);
	lcd.print(row3_0);
    lcd.setCursor((int)(strlen(&row3_0[0])), 2);
    lcd.write(1); // Write the degC symbol (degrees Celsius)
    lcd.print(row3_1);
    lcd.setCursor((int)(strlen(&row3_0[0]) + strlen(&row3_1[0]) + 1), 2);
    lcd.write(0); // Write the omega symbol (Ohm)

    lcd.setCursor(0, 3);
    lcd.print(row4);
}

// Function to correct the voltage measurement. Found experimentally, will differ by setup.
// Input 0 of my ADC is broken, enable if you need this.
// float voltApprox(float x){
//     return 7.52 + 3.03*x + 0.429*pow(x,2);
// }

/*
    Publish MQTT data that is not misc data.
    topicIndex is the index of the topic name, val is the actual data value, nodeId indicates which node the data came from.
*/
void pub(int topicIndex, float val, int nodeId=0){
    // Build the topic name.
    char* tempTopic;
    // If the topicIndex is outside the range of the charge controller topics, switch to misc data topics.
    if(topicIndex >= NUM_CC_MQTT_TOPICS){
      tempTopic = (char*) malloc(strlen(miscDataTopics[topicIndex - NUM_CC_MQTT_TOPICS]));
      sprintf(tempTopic, "%s", miscDataTopics[topicIndex - NUM_CC_MQTT_TOPICS]);
    }
    else{
      tempTopic = (char*) malloc(strlen(chargeControllerTopics[topicIndex]) + strlen(CC_TOPIC_PREFIX) + 1); // +1 for the charge controller number after the CC prefix. >9 will need adjustment.
      sprintf(tempTopic, "%s%d%s", CC_TOPIC_PREFIX, nodeId + 1, chargeControllerTopics[topicIndex]);
    }

    // Is this the charging mode topic?
    if(topicIndex == CHARGE_MODE_TOPIC_INDEX){
		// See if load is turned on, if it is then we need to apply an offset.
        int loadOffset = *registersMembers[nodeId][32] > 6 ? 32768 : 0;

        if(!client.publish(tempTopic, chargeModes[*registersMembers[nodeId][32] - loadOffset])){
            // If error then try reconnecting and publishing again.
            Serial.println(F("Data not sent"));
            if(client.connect(MQTT_CLIENT_ID)){
                client.publish(tempTopic, chargeModes[*registersMembers[nodeId][32] - loadOffset]);
            }
        }
    }
    else{
        char tempVal[8];
        
        // Is this the modbus error topic?
        if(topicIndex == MODBUS_ERR_TOPIC_INDEX){
			if(nodeId + 1 == MISC_DATA_NODE_ADDR){
				sprintf(tempVal, "%d", miscNodeData.modbusErrDiff);

			}
			else{
				sprintf(tempVal, "%d", otherNodeData[0].modbusErrDiff);
			}
		}
		else{
			sprintf(tempVal, "%f", val);
		}

        if(!client.publish(tempTopic, tempVal)){
            // If error then try reconnecting and publishing again.
            Serial.println(F("Data not sent"));
            if(client.connect(MQTT_CLIENT_ID)){
                client.publish(tempTopic, tempVal);
            }
        }
    }
    free(tempTopic);
}

// Coordinate publishing the misc data which is not from the charge controllers.
void publishMiscData(){
    pub(0 + NUM_CC_MQTT_TOPICS, miscNodeData.panelAmpsBack);
    pub(1 + NUM_CC_MQTT_TOPICS, miscNodeData.panelAmpsFrontA + miscNodeData.panelAmpsFrontB);
    // pub(2 + NUM_CC_MQTT_TOPICS, miscNodeData.dcVolts); // Input 0 of my ADC is broken, enable if you need this.
    pub(3 + NUM_CC_MQTT_TOPICS, bmeTemp);
    pub(4 + NUM_CC_MQTT_TOPICS, bmeHum);
    pub(5 + NUM_CC_MQTT_TOPICS, bmePres);
    pub(6 + NUM_CC_MQTT_TOPICS, bmeGas);
}

/*
    Coordinate publishing all charge controller data. nodeId is the id of the node/charge controller.
    Register addresses were figured out from the modbus document, have a look at it if you are confused about them.
*/
void publishCC(int nodeId){
	if(!mqttConnected) return;	// Handle case where mqtt connection failed at initial startup, no point int trying to connect.

    pub(0, 0, nodeId);
    
    // Publish the charging mode of the charge controller.
    pub(1, 0, nodeId);
    
    // Publish battery state of charge.
    pub(2, *registersMembers[nodeId][0], nodeId);
    
    // Publish battery volts.
    pub(3, (*registersMembers[nodeId][1]*0.1), nodeId);

    // Publish battery charging current.
    pub(4, (*registersMembers[nodeId][2]*0.01), nodeId);

    // Publish controller temperature.
    pub(5, getRealTemp(*registersMembers[nodeId][3] >> 8), nodeId);
    
    // Publish battery temperature.
    pub(6, getRealTemp(*registersMembers[nodeId][3] & 0xFF), nodeId);

    // // Publish load voltage.
    // pub(TOPIC_ID_HERE, (*registersMembers[nodeId][4]*0.1), nodeId);
    // // Publish load current.
    // pub(TOPIC_ID_HERE, (*registersMembers[nodeId][5]*0.01), nodeId);
    // // Publish load wattage.
    // pub(TOPIC_ID_HERE, (*registersMembers[nodeId][6]), nodeId);

    // Publish battery volts.
    pub(7, (*registersMembers[nodeId][7]*0.1), nodeId);

    // Publish panel current.
    pub(8, (*registersMembers[nodeId][8]*0.01), nodeId);

    // Publish panel watts.
    pub(9, *registersMembers[nodeId][9], nodeId);

    // // Publish load status (on / off).
    // pub(TOPIC_ID_HERE, *registersMembers[nodeId][10], nodeId);

    // Publish battery daily min volts.
    pub(10, (*registersMembers[nodeId][11]*0.1), nodeId);

    // Publish battery daily max volts.
    pub(11, (*registersMembers[nodeId][12]*0.1), nodeId);

    // Publish battery daily max charging current.
    pub(12, (*registersMembers[nodeId][13]*0.01), nodeId);

    // // Publish daily max load current.
    // pub(TOPIC_ID_HERE, (*registersMembers[nodeId][14]*0.01), nodeId);

    // Publish daily max panel charging power.
    pub(13, *registersMembers[nodeId][15], nodeId);

    // // Publish daily max load power.
    // pub(TOPIC_ID_HERE, *registersMembers[nodeId][16], nodeId);

    // Publish daily charging amp hours.
    pub(14, *registersMembers[nodeId][17], nodeId);

    // // Publish daily load amp hours.
    // pub(TOPIC_ID_HERE, *registersMembers[nodeId][18], inodeIdd);

    // Publish daily power generated.
    pub(15, (*registersMembers[nodeId][19]*0.001), nodeId);

    // // Publish daily load power used.
    // pub(TOPIC_ID_HERE, (*registersMembers[nodeId][20]*0.001), nodeId);

    // Publish number of days operational
    pub(16, *registersMembers[nodeId][21], nodeId);

    // Publish number of battery over-charges
    pub(17, *registersMembers[nodeId][22], nodeId);

    // Publish number of battery full-charges
    pub(18, *registersMembers[nodeId][23], nodeId);

    // Publish total battery charging amp-hours.
    pub(19, ((*registersMembers[nodeId][24]*65536 + *registersMembers[nodeId][25])*0.001), nodeId);

    // // Publish total load consumption amp-hours.
    // pub(TOPIC_ID_HERE, ((*registersMembers[nodeId][26]*65536 + *registersMembers[nodeId][27])*0.001), nodeId);

    // Publish total power generation.
    pub(20, ((*registersMembers[nodeId][28]*65536 + *registersMembers[nodeId][29])*0.001), nodeId);

    // // Publish total load power consumption.
    // pub(TOPIC_ID_HERE, ((*registersMembers[nodeId][30]*65536 + *registersMembers[nodeId][31])*0.001), nodeId);

    // Register 32 is charging state which is handled in the first pub() call, 33 is reserved. 

    // Publish fault data.
    pub(21, *registersMembers[nodeId][34], nodeId);
}

// Helper to handle the sign bit for negative temperatures.
int getRealTemp(int temp){
    return temp/128 ? -(temp%128) : temp;
}

/*
    Function to correct the voltage measurement. Found experimentally, will differ by setup.
*/
// Input 0 of my ADC is broken, enable if you need this.
// float voltApprox(float x){
//     return 7.52 + 3.03*x + 0.429*pow(x,2);
// }

void loop(){
    network.update();
    if(network.available()){
        while (network.available()) {
            RF24NetworkHeader header;                                             
            network.peek(header);

            // See which node this transmission is from.
            // If from the node/charge controller reporting misc data, treat differently.
            if(header.from_node == MISC_DATA_NODE_ADDR){
                network.read(header, &miscNodeData, sizeof(miscNodeData));
                delay(10);
                
                // Convert the raw sensor data values from the transmitter into their actual values.
                // miscNodeData.dcVolts = voltApprox(miscNodeData.dcVolts*ADS1115_VPB); // Input 0 of my ADC is broken, enable if you need this.
                miscNodeData.panelAmpsFrontA = (FRONT_AMPS_SENSOR_A_OFFSET - miscNodeData.panelAmpsFrontA * ADS1115_VPB) / ACS724_VPA;
                miscNodeData.panelAmpsFrontB = (FRONT_AMPS_SENSOR_B_OFFSET - miscNodeData.panelAmpsFrontB * ADS1115_VPB) / ACS724_VPA;
                miscNodeData.panelAmpsBack = (BACK_AMPS_SENSOR_OFFSET - miscNodeData.panelAmpsBack * ADS1115_VPB) / ACS724_VPA;

                if(miscNodeData.panelAmpsFrontA < 0){
                    miscNodeData.panelAmpsFrontA = 0;
                }
                if(miscNodeData.panelAmpsFrontB < 0){
                    miscNodeData.panelAmpsFrontB = 0;
                }
                if(miscNodeData.panelAmpsBack < 0){
                    miscNodeData.panelAmpsBack = 0;
                }

				bmeTemp = miscNodeData.bmeTemp / 100.0;
				bmePres = miscNodeData.bmePres / 100.0;
				bmeHum = miscNodeData.bmeHum / 1000.0;
				bmeGas = miscNodeData.bmeGas / 100.0;

                publishCC(MISC_DATA_NODE_ADDR - 1);
                publishMiscData();
            }
            // If from another node/charge controller which is not reporting misc data.
            else if(header.from_node <= NUM_CHARGE_CONTROLLERS && header.from_node > 0){
                int index = header.from_node - NUM_MISC_DATA_NODES;
                network.read(header, &otherNodeData[index - NUM_MISC_DATA_NODES], sizeof(otherNodeData[index - NUM_MISC_DATA_NODES]));
                delay(10);

                publishCC(index);
                printLCD();
            }
            else{
                Serial.print(F("Invalid Sender: "));
                Serial.println(header.from_node);
                delay(500);
            }
        }
    }
}
