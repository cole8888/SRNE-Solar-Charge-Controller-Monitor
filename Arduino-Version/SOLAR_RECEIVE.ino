// This code runs on a NODEMCU 0.9 module.
// There are two arduinos inside the deck box and each one reads data from one of the two charge controllers (CC1 and CC2).
// This data along with some other data such as environment sensor data and direct sensor measurements are then transmitted to this receiver.
// This ESP8266 then proccesses the data from the transmitters and then publishes it to an MQTT server as well as diplays it on an attached LCD.

// Attached components are:
// Generic 20x4 character I2C lcd display
// RF24 Module
// Capacitors on the 3.3V and 5V power rails to prevent voltage drops.

#include <SPI.h>                // Probably not needed, was troubleshooting RF24
#include <nRF24L01.h>
#include <RF24.h>               // Library version 1.3.12 works, 1.4.0 did not.
#include <RF24Network.h>        // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>       // Allows us to connect to, and publish to the MQTT broker
#include <ESP8266WiFi.h>        // Enables The WiFi radio (Not RF24) on the ESP8266.
#include <math.h>               // Use this for pow()
#include <stdbool.h>

// Volts per bit of the ADS1115 ADC (6.144V/32768)
#define VPB 0.0001875

// Create the LCD device
LiquidCrystal_I2C lcd(0x27, 20,4);

// Custom symbols for the LCD
// Omega Symbol (Ohms)
byte omegaSymbol[] = {
    0x00,0x0E,0x11,0x11,0x11,0x0A,0x1B,0x00
};
// Degrees Celsius Symbol
byte degC[] = {
    0x18,0x18,0x07,0x08,0x08,0x08,0x07,0x00
};

// Create the RF24 Radio and network
RF24 radio(D4, D8); // CE, CSN
RF24Network network(radio); // Initialize the RF24 network
const uint16_t box_node1 = 01;  // Address of the arduino reading CC1 (transmitter)
const uint16_t box_node2 = 02;  // Address of the arduino reading CC2 (transmitter)
const uint16_t house_node = 00; // Address of the node located in the house (receiver)

// I had issues with the transmitter's Serial buffers running out of room when receiving more than
// 29 registers at once from the charge controller. Solved this by just sending two separate requests.
// Each request has a different number of registers in it.
const short int numRegisters1 = 24;
const short int numRegisters2 = 11;

// This is the struct which is transmitted from node1 to the receiver.
// Front panel amps are split into two because the ACS724 only goes up to 30A and I needed 60A, so I placed two ACS724 sensors in parallel.
// These are the raw sensor values (except BME680 ones) and they need to be translated into their actual values later
typedef struct package{
    float dcAmpsBack;                   // Measured Back panel amps (ACS712 current sensor)
    float dcAmpsFrontA;                 // Measured Front panel amps sensor 1/2 (ACS724 curent sensor)
    float dcAmpsFrontB;                 // Measured Front panel amps sensor 1/2 (ACS724 curent sensor)
    // ADC input 0 is broken, enable when replaced.
    // float dcVolts;                   // Measured battery voltage (ADS1115 and a voltage divider. R1=56K, R2=10K)
    float pres;                         // Atmospheric pressure measured by BME680
    float temp;                         // Air temperature measure by BME680
    float hum;                          // Air humidity measured by BME680
    float gas;                          // Volatile Organic Compounds measure in kOhms by BME680
    uint16_t registers[numRegisters1];  // First group of registers from the charge controller
    uint16_t registers2[numRegisters2]; // Second group of registers from the charge controller
}Package;
Package data;

// Struct for node2. This contains all the charge controller data for CC2.
typedef struct package2{
    uint16_t registers[numRegisters1];  // First group of registers from the charge controller
    uint16_t registers2[numRegisters2]; // Second group of registers from the charge controller
}Package2;
Package2 data2;

// 2D Array of pointers which is used to retrieve charge controller data in publishCC()
uint16_t *registers1Members[2][48];
uint16_t *registers2Members[2][22];

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_WIFI_PASS";
const char* mqtt_server = "MQTT_SERVER_ADDR";
const char* clientID = "ESP8266";
WiFiClient wifiClient;
PubSubClient client(mqtt_server, 1883, wifiClient); // 1883 is the listener port for the Broker

// Array of all MQTT topics.
// pub() will automatically change the '1' in most of these to a '2' if it's CC2 data to save flash space.
const char* topics[29]={
    "dcAmpsB",                  //0
    "dcAmpsF",                  //1
    "dcVolts",                  //2
    "Temp",                     //3
    "Hum",                      //4
    "Pres",                     //5
    "Gas",                      //6
    "CC1chargingMode",          //7
    "CC2chargingMode",          //8 We cannot use pub() when publishing a string, we need to include this.
    "CC1SOC",                   //9
    "CC1battVolts",             //10
    "CC1battAmps",              //11
    "CC1controllerTemp",        //12
    "CC1battTemp",              //13
    "CC1panelVolts",            //14
    "CC1panelAmps",             //15
    "CC1panelWatts",            //16
    "CC1battMinVolts",          //17
    "CC1battMaxVolts",          //18
    "CC1battMaxChargeCurrent",  //19
    "CC1panelMaxPower",         //20
    "CC1chargeAmpHours",        //21
    "CC1dailyPower",            //22
    "CC1numDays",               //23
    "CC1numOverCharges",        //24
    "CC1numFullCharges",        //25
    "CC1totalAmpHours",         //26
    "CC1totalPower",            //27
    "CC1faults"};               //28

void setup(){
    while (!Serial);
    Serial.begin(115200);
    
    // Startup LCD
    lcd.begin();
    // Load custom characters into display ram.
    lcd.createChar(0, omegaSymbol);
    lcd.createChar(1, degC);
    // Turn on the LCD backlight
    lcd.backlight();

    lcd.setCursor(0,0);
    lcd.print("Connecting");

    // Startup the RF24
    while(!radio.begin()){
        Serial.print(F("\nUnable to start RF24. Trying in 2 seconds.\n"));
        delay(2000);
    }
    network.begin(/*channel*/ 90, /*node address*/ house_node);
    Serial.println(F("RF24 Initialized"));

    // Begin WiFi connection
    WiFi.begin(ssid, password);
    // Wait for connection
    int trys = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if(trys > 20){
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("WiFi Disconnected :(");
            break;
        }
        delay(500);
        Serial.print(F("."));
        lcd.print(".");
        trys++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Connected :)");
        lcd.setCursor(0,1);
        lcd.print("SSID: ");
        lcd.print(ssid);
        lcd.setCursor(0,2);
        lcd.print("IP: ");
        lcd.print(WiFi.localIP());
        lcd.setCursor(0,3);

        Serial.println(F(""));
        Serial.print(F("Connected to "));
        Serial.println(ssid);
        Serial.print(F("IP address: "));
        Serial.println(WiFi.localIP());
    }

    // Connect to MQTT Broker
    if (client.connect(clientID)) {
        lcd.print("MQTT Connected :)");
        Serial.println(F("MQTT Connected"));
    }
    else {
        lcd.print("MQTT Failed :(");
        Serial.println(F("MQTT Connection failed..."));
    }

    // Initialize register1 pointers array.
    for(int i = 0; i<24; i++){
        registers1Members[0][i] = &data.registers[i];
    }
    for(int i = 0; i<24; i++){
        registers1Members[1][i] = &data2.registers[i];
    }
    // Initialize register2 pointers array.
    for(int i = 0; i<11; i++){
        registers2Members[0][i] = &data.registers2[i];
    }
    for(int i = 0; i<11; i++){
        registers2Members[1][i] = &data2.registers2[i];
    }

    delay(1500);
}

// Print some of the data to the attached LCD.
void printLCD(){
    float value = 0;
    // Prepare display contents for line 4 ahead of time.
    // By doing this we decrease how long they display blanks for.
    String row4 = "Pre: ";
    row4 += (int)data.pres;
    row4 += "hPa Hum:";
    row4 += (int)data.hum;
    row4 += "%";
    
    lcd.clear();

    // Amp: F:X.XXA B:X.XXA
    lcd.print("Amp: F:");
    if((data.registers[2]*0.01) < 10){
        lcd.print((data.registers[2]*0.01),2);
    }
    else{
        lcd.print((data.registers[2]*0.01),1);
    }
    lcd.print("A B:");
    if((data2.registers[2]*0.01) < 10){
        lcd.print((data2.registers[2]*0.01),2);
    }
    else{
        lcd.print((data2.registers[2]*0.01),1);
    }
    lcd.print("A");

    // Bat: XX.XV | XX.XV
    lcd.setCursor(0,1);
    lcd.print("Bat: ");
    // CC2 has never crashed unlike CC1. Use CC2 voltage instead.
    lcd.print(data2.registers[1]*0.05, 1);
    lcd.print("V | ");
    lcd.print(data2.registers[1]*0.1, 1);
    lcd.print("V");
    
    lcd.setCursor(0,2);
    if(data.temp<=-10){ // Handle case where negative double digit temp will overflow
        lcd.print("Tmp:");
    }
    else{
        lcd.print("Tmp: ");
    }
    lcd.print(data.temp,1);
    lcd.write(1); // Write the degC symbol (degrees Celsius)
    lcd.print(" Gas:");
    if(data.gas>=1000){
        lcd.print(data.gas/1000, 1);
        lcd.print("M");
    }
    else{
        lcd.print(data.gas, 0);
        lcd.print("k");
    }
    lcd.write(0); // Write the omega symbol (Ohm)

    // Pre: XXXXhPa Hum:XX% 
    lcd.setCursor(0,3);
    lcd.print(row4);
}

// Function to correct the voltage measurement. Found experimentally, will differ by setup.
// ADC input 0 is broken, enable when replaced.
// float voltApprox(float x){
//     return 7.52 + 3.03*x + 0.429*pow(x,2);
// }

// Function to publish MQTT data. Only works for ints / floats.
// i is the topic index, val is the actual data value, isNode2 indicates which node the data came from.
void pub(int i, float val, bool isNode2){
    char tempval[8];
    char* tempTopic = (char*) malloc(strlen(topics[i]) + 1); 
    strcpy(tempTopic, topics[i]);
    sprintf(tempval, "%f", val);
    // See if we are publishing data from CC2. If we are then need to change the MQTT topic.
    if(isNode2){
        tempTopic[2] = '2';
    }
    if(!client.publish(tempTopic, tempval)){
        // If error then try reconnecting and publishing again.
        Serial.println(F("Data not sent"));
        if(client.connect(clientID)){
            client.publish(tempTopic, tempval);
        }
    }
    free(tempTopic);
}

// Publish sensor data which is not from the charge controllers.
void publishSensor(){
    pub(0, data.dcAmpsBack, false);
    pub(1, data.dcAmpsFrontA+data.dcAmpsFrontB, false);
    // ADC input 0 is broken, enable when replaced.
    // pub(2, data.dcVolts, false);
    pub(3, data.temp, false);
    pub(4, data.hum, false);
    pub(5, data.pres, false);
    pub(6, data.gas, false);
}

// Publish all charge controller data (id = 0 for CC1, id = 1 for CC2)
void publishCC(int id){
    bool isNode2 = true;

    // If node 1 sent the data...
    if(id == 0){
        isNode2 = false;
        // If node1, then we also need to publish the non CC data.
        publishSensor();
    }

    // Determine the current charging mode
    int modeOffset = 0;
    // See if load is turned on, if it is then we need to apply an offset.
    if(*registers2Members[id][8] > 6){
     modeOffset = 32768;
    }
    // Cannot use pub() because we need to send strings.
    if(*registers2Members[id][8] == 0 + modeOffset){
        client.publish(topics[7+id], "OFF");
    }
    else if(*registers2Members[id][8] == 1 + modeOffset){
        client.publish(topics[7+id], "NORMAL");
    }
    else if(*registers2Members[id][8] == 2 + modeOffset){
        client.publish(topics[7+id], "MPPT");
    }
    else if(*registers2Members[id][8] == 3 + modeOffset){
        client.publish(topics[7+id], "EQUALIZE");
    }
    else if(*registers2Members[id][8] == 4 + modeOffset){
        client.publish(topics[7+id], "BOOST");
    }
    else if(*registers2Members[id][8] == 5 + modeOffset){
        client.publish(topics[7+id], "FLOAT");
    }
    else if(*registers2Members[id][8] == 6 + modeOffset){
        client.publish(topics[7+id], "CURRENT_LIMITING");
    }
    
    // Publish battery state of charge
    pub(9, *registers1Members[id][0], isNode2);
    
    // Publish battery volts
    pub(10, (*registers1Members[id][1]*0.1), isNode2);
    
    // Publish battery charging current
    pub(11, (*registers1Members[id][2]*0.01), isNode2);

    // Publish controller temperature (will likely have issues <0*C)
    pub(12, (*registers1Members[id][3] >> 8), isNode2);
    
    // Publish battery temperature (will likely have issues <0*C)
    pub(13, (*registers1Members[id][3] & 0xFF), isNode2);

    // // Publish load voltage
    // pub(TOPIC_ID_HERE, (*registers1Members[id][4]*0.1), isNode2);
    // // Publish load current
    // pub(TOPIC_ID_HERE, (*registers1Members[id][5]*0.01), isNode2);
    // // Publish load wattage
    // pub(TOPIC_ID_HERE, (*registers1Members[id][6]), isNode2);

    // Publish battery volts
    pub(14, (*registers1Members[id][7]*0.1), isNode2);

    // Publish panel current
    pub(15, (*registers1Members[id][8]*0.01), isNode2);

    // Publish panel watts
    pub(16, *registers1Members[id][9], isNode2);

    // // Publish load status (on / off)
    // pub(TOPIC_ID_HERE, *registers1Members[id][10], isNode2);

    // Publish battery daily min volts
    pub(17, (*registers1Members[id][11]*0.1), isNode2);

    // Publish battery daily max volts
    pub(18, (*registers1Members[id][12]*0.1), isNode2);

    // Publish battery daily max charging current
    pub(19, (*registers1Members[id][13]*0.01), isNode2);

    // // Publish daily max load current
    // pub(TOPIC_ID_HERE, (*registers1Members[id][14]*0.01), isNode2);

    // Publish daily max panel charging power
    pub(20, *registers1Members[id][15], isNode2);

    // // Publish daily max load power
    // pub(TOPIC_ID_HERE, *registers1Members[id][16], isNode2);

    // Publish daily charging amp hours
    pub(21, *registers1Members[id][17], isNode2);

    // // Publish daily load amp hours
    // pub(TOPIC_ID_HERE, *registers1Members[id][18]);

    // Publish daily power generated
    pub(22, (*registers1Members[id][19]*0.001), isNode2);

    // // Publish daily load power used
    // pub(TOPIC_ID_HERE, (*registers1Members[id][20]*0.001), isNode2);

    // Publish number of days operational
    pub(23, *registers1Members[id][21], isNode2);

    // Publish number of battery over-charges
    pub(24, *registers1Members[id][22], isNode2);

    // Publish number of battery full-charges
    pub(25, *registers1Members[id][23], isNode2);

    // Switch to second register group

    // Publish total battery charging amp-hours
    pub(26, ((*registers2Members[id][0]*65536 + *registers2Members[id][1])*0.001), isNode2);

    // // Publish total load consumption amp-hours
    // pub(TOPIC_ID_HERE, ((*registers2Members[id][2]*65536 + *registers2Members[id][3])*0.001), isNode2);

    // Publish total power generation
    pub(27, ((*registers2Members[id][4]*65536 + *registers2Members[id][5])*0.001), isNode2);

    // // Publish total load power consumption
    // pub(TOPIC_ID_HERE, ((*registers2Members[id][6]*65536 + *registers2Members[id][7])*0.001), isNode2);

    // Publish fault data.
    pub(28, *registers2Members[id][10], isNode2);
}

void loop(){
    network.update();
    if(network.available()){
        while (network.available()) {
            RF24NetworkHeader header;                                             
            network.peek(header);
            // If from CC1 arduino (node1)
            if(header.from_node == box_node1){
                network.read(header, &data, sizeof(data)); // Is there anything ready for us?
                delay(10);
                // Convert the raw sensor data values from the transmitter into their actual values.
                // ADC input 0 is broken, enable when replaced.
                // data.dcVolts = voltApprox(data.dcVolts*VPB);
                data.dcAmpsFrontA = (2.23-data.dcAmpsFrontA*VPB)/0.066;
                data.dcAmpsFrontB = (2.36-data.dcAmpsFrontB*VPB)/0.066;
                data.dcAmpsBack = (2.40-data.dcAmpsBack*VPB)/0.066;
                if(data.dcAmpsFrontA < 0){
                    data.dcAmpsFrontA = 0;
                }
                if(data.dcAmpsFrontB < 0){
                    data.dcAmpsFrontB = 0;
                }
                if(data.dcAmpsBack < 0){
                    data.dcAmpsBack = 0;
                }

                publishCC(0);
                printLCD();
            }
            // If from CC2 arduino (node2)
            else if(header.from_node == box_node2){
                network.read(header, &data2, sizeof(data2)); // Is there anything ready for us?
                delay(10);
                publishCC(1);
            }
            else{
                Serial.print(F("Invalid Sender: "));
                Serial.println(header.from_node);
                delay(500);
            }
        }
    }
}
