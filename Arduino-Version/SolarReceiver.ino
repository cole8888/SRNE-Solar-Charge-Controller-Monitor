// This code runs on a NODEMCU 0.9 module.
// The display is a 20x4 I2C lcd display
// 
// This machine is responsible for deceipering the data and sending it to the mqtt server and also an LCD.

#include <SPI.h>              // Probably not needed, was troubleshooting RF24
#include <nRF24L01.h>
#include <RF24.h>             // Library version 1.3.12 works, 1.4.0 did not.
#include <RF24Network.h>      // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>     // Allows us to connect to, and publish to the MQTT broker
#include <ESP8266WiFi.h>
#include <math.h>

// Volts per bit of the ADS1115 ADC (6.144/32768)
#define VPB 0.0001875

// Create the LCD device
LiquidCrystal_I2C lcd(0x27, 20,4);

// Custom symbols for the LCD
byte omegaSymbol[] = {
  0x00,0x0E,0x11,0x11,0x11,0x0A,0x1B,0x00
};
byte degC[] = {
  0x18,0x18,0x07,0x08,0x08,0x08,0x07,0x00
};

// Create the RF24 Radio
RF24 radio(D4, D8);  // CE, CSN
RF24Network network(radio);     // Initialize the RF24 network
const uint16_t box_node = 01;   // Address of the node located in the deck box (transmitter)
const uint16_t house_node = 00; // Address of the node located in the house (receiver)

// I had issues with the transmitter's Serial buffers running out of room when receiving more than
// 29 registers at once from the charge controller. Solved this by just sending two seperate requests.
// Each request has a different number of registers in it.
const short int numRegisters1 = 24;
const short int numRegisters2 = 11;

// This is the struct which is transmitted to the receiver and contains all the data.
// Front panel amps are split into two because the ACS724 only goes up to 30A and I needed 60A,
// so I placed two ACS724 sensors in paralel.
// These are the raw sensor values (except BME680 ones) and they need to be translated into their actual values later
typedef struct package{
  float dcAmpsBack;   // Measured Back panel amps (ACS712 current sensor)
  float dcAmpsFrontA; // Measured Front panel amps sensor 1/2 (ACS724 cureent sensor)
  float dcAmpsFrontB; // Measured Front panel amps sensor 1/2 (ACS724 cureent sensor)
  float dcVolts;      // Measured battery voltage (ADS1115 and a voltage divider. R1=56K, R2=10K)
  float pres;         // Atmospheric pressure measured by BME680
  float temp;         // Air temperature measure by BME680
  float hum;          // Air humidity meaured by BME680
  float gas;          // Volotile Organic Compounts measure in kOhms by BME680
  uint16_t registers[numRegisters1];  // First group of registers from the charge controller
  uint16_t registers2[numRegisters2]; // Second group of registers from the charge controller
}Package;
Package data;

const char* ssid =  "YOUR_WIFI_SSID_HERE!";
const char* password =  "YOUR_WIFI_PASSWORD_HERE!";
const char* mqtt_server = "192.168.2.100";
const char* clientID = "ESP8266";
WiFiClient wifiClient;
PubSubClient client(mqtt_server, 1883, wifiClient); // 1883 is the listener port for the Broker

// Array of MQTT topics
const char* topics[42]={"dcAmpsB",                //0
                        "dcAmpsF",                //1
                        "dcVolts",                //2
                        "Temp",                   //3
                        "Hum",                    //4
                        "Pres",                   //5
                        "Gas",                    //6
                        "CCchargeMode",           //7
                        "CCSOC",                  //8
                        "CCbattVolts",            //9
                        "CCbattAmps",             //10
                        "CCcontrollerTemp",       //11
                        "CCbattTemp",             //12
                        "CCpanelVolts",           //13
                        "CCpanelAmps",            //14
                        "CCpanelWatts",           //15
                        "CCbattMinVolts",         //16
                        "CCbattMaxVolts",         //17
                        "CCbattMaxChargeCurrent", //18
                        "CCpanelMaxPower",        //19
                        "CCchargeAmpHours",       //20
                        "CCdailyPower",           //21
                        "CCnumDays",              //22
                        "CCnumOverCharges",       //23
                        "CCnumFullCHarges",       //24
                        "CCtotalAmpHours",        //25
                        "CCtotalPower",           //26
                        "CCfault1",               //27
                        "CCfault2",               //28
                        "CCfault3",               //29
                        "CCfault4",               //30
                        "CCfault5",               //31
                        "CCfault6",               //32
                        "CCfault7",               //33
                        "CCfault8",               //34
                        "CCfault9",               //35
                        "CCfault10",              //36
                        "CCfault11",              //37
                        "CCfault12",              //38
                        "CCfault13",              //39
                        "CCfault14",              //40
                        "CCfault15"};             //41

// All possible controller faults
const char* faultCodes[15]={"Charge MOS short circuit",     //0
                            "Anti-reverse MOS short",       //1
                            "PV panel reversely connected", //2
                            "PV working point over voltage",//3
                            "PV counter current",           //4
                            "PV input side over-voltage",   //5
                            "PV input side short circuit",  //6
                            "PV input overpower",           //7
                            "Ambient temp too high",        //8
                            "Controller temp too high",     //9
                            "Load over-power/current",      //10
                            "Load short circuit",           //11
                            "Battery undervoltage warning", //12
                            "Battery overvoltage",          //13
                            "Battery over-discharge"};      //14

void setup(){
  while (!Serial);
  Serial.begin(115200);
  
  // Startup LCD
  lcd.begin();
  lcd.createChar(0, omegaSymbol);
  lcd.createChar(1, degC);
  lcd.backlight();

  lcd.setCursor(0,0);
  lcd.print("Connecting");

  // Startup the RF24
  while(!radio.begin()){
    Serial.print(F("\nUnable to start RF24. Trying again in 2 seconds.\n"));
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
    Serial.println(F("Connected to MQTT Broker!"));
  }
  else {
    lcd.print("MQTT Failed :(");
    Serial.println(F("Connection to MQTT Broker failed..."));
  }
  delay(2000);
}

// Function to publish MQTT data
// i is the topic index, val is the actual data value
void pub(int i, float val){
  char tempval[8];
  sprintf(tempval, "%f", val);
  if(client.publish(topics[i], tempval)){
    //Serial.println("Data sent");
  }
  else{ // Try to recconnect to the MQTT broker
    Serial.println(F("Data not sent"));
    if(client.connect(clientID)){
      client.publish(topics[i], tempval);
    }
  }
}

// Function to correct the voltage measurement
float voltApprox(float x){
  return 7.52 + 3.03*x + 0.429*pow(x,2);
}

// Print some of the data to the attacked LCD.
void printLCD(){
  // Convert the raw sensor data values from the transmitter into thair actual values.
  float value = 0;
  data.dcVolts = voltApprox(data.dcVolts*VPB);
  data.dcAmpsFrontA = (2.344-data.dcAmpsFrontA*VPB)/0.066;
  data.dcAmpsFrontB = (2.504-data.dcAmpsFrontB*VPB)/0.066;
  data.dcAmpsBack = (2.57-data.dcAmpsBack*VPB)/0.066;

  // Prepare display contents for lines 2 and 4 ahead of time.
  // By doing this we seriously limit how long they display blanks for.
  String row2 = "Bat: ";
  value = (int)(data.dcVolts * 0.5 * 100 + 0.5);
  row2 += (float)value/100;
  row2 += "V | ";
  value = (int)(data.dcVolts * 100 + 0.5);
  row2 += (float)value/100;
  row2 += "V";

  String row4 = "Pre: ";
  row4 += (int)data.pres;
  row4 += "hPa Hum:";
  row4 += (int)data.hum;
  row4 += "%";
  
  lcd.clear();        
  lcd.print("Amp: F:");
  // If amps are too low, set to zero
  if(data.dcAmpsFrontA+data.dcAmpsFrontB < 2){
    lcd.print("<2");
    data.dcAmpsFrontA = 0;
    data.dcAmpsFrontB = 0;
  }
  else{
    if(data.dcAmpsFrontA+data.dcAmpsFrontB < 10){
      lcd.print(data.dcAmpsFrontA+data.dcAmpsFrontB,2);
    }
    else{
      lcd.print(data.dcAmpsFrontA+data.dcAmpsFrontB,1);
    }
  }
  lcd.print("A B:");
  if(data.dcAmpsBack < 1){
    lcd.print("<1");
    data.dcAmpsBack = 0;
  }
  else{
    if(data.dcAmpsBack < 10){
      lcd.print(data.dcAmpsBack,2);
    }
    else{
      lcd.print(data.dcAmpsBack,1);
    }
  }
  lcd.print("A");

  lcd.setCursor(0,1);
  lcd.print(row2);
  lcd.setCursor(0,3);
  lcd.print(row4);
  
  lcd.setCursor(0,2);
  if(data.temp<=-10){ // Handle case where negative double digit temp will overflow
    lcd.print("Tmp:");
  }
  else{
    lcd.print("Tmp: ");
  }
  lcd.print(data.temp,1);
  lcd.write(1);
  lcd.print(" Gas:");
  if(data.gas>=1000){
    lcd.print(data.gas/1000, 1);
    lcd.print("M");
  }
  else{
    lcd.print(data.gas, 0);
    lcd.print("k");
  }
  lcd.write(0);
}

// Publish everything contained in the data struct.
void publishEverything(){
  pub(0, data.dcAmpsBack);
  pub(1, data.dcAmpsFrontA+data.dcAmpsFrontB);
  pub(2, data.dcVolts);
  pub(3, data.temp);
  pub(4, data.hum);
  pub(5, data.pres);
  pub(6, data.gas);

  // Determine the current charging mode
  int modeOffset = 0;
  // See if load is turned on, if it is then we need to apply an offset.
  if(data.registers2[8] > 6){
   modeOffset = 32768;
  }
  if(data.registers2[8] == 0 + modeOffset){
   client.publish(topics[7], "OFF");
  }
  else if(data.registers2[8] == 1 + modeOffset){
   client.publish(topics[7], "NORMAL");
  }
  else if(data.registers2[8] == 2 + modeOffset){
   client.publish(topics[7], "MPPT");
  }
  else if(data.registers2[8] == 3 + modeOffset){
   client.publish(topics[7], "EQUALIZE");
  }
  else if(data.registers2[8] == 4 + modeOffset){
   client.publish(topics[7], "BOOST");
  }
  else if(data.registers2[8] == 5 + modeOffset){
   client.publish(topics[7], "FLOAT");
  }
  else if(data.registers2[8] == 6 + modeOffset){
   client.publish(topics[7], "CURRENT_LIMITING");
  }
  
  // Publish battery state of charge
  pub(8, data.registers[0]);
  
  // Publish battery volts
  pub(9, (data.registers[1]*0.1));
  
  // Publish battery charging current
  pub(10, (data.registers[2]*0.01));

  // Publish controller temperature (will likely have issues <0 *C)
  pub(11, (data.registers[3] >> 8));
  
  // Publish battery temperature (will likely have issues <0 *C)
  pub(12, (data.registers[3] & 0xFF));

  // Publish load voltage
  // pub(ID_HERE, (data.registers[4]*0.1));
  // Publish load current
  // pub(ID_HERE, (data.registers[5]*0.01));
  // Publish load wattage
  // pub(ID_HERE, (data.registers[6]));

  // Publish battery volts
  pub(13, (data.registers[7]*0.1));

      // Publish panel current
  pub(14, (data.registers[8]*0.01));

  // Publish panel watts
  pub(15, data.registers[9]);

  // Publish load status (on / off)
  // pub(ID_HERE, data.registers[10]);

  // Publish battery daily min volts
  pub(16, (data.registers[11]*0.1));

  // Publish battery daily max volts
  pub(17, (data.registers[12]*0.1));

  // Publish battery daily max charging current
  pub(18, (data.registers[13]*0.01));

  // Publish daily max load current
  // pub(ID_HERE, (data.registers[14]*0.01));

  // Publish daily max panel charging power
  pub(19, data.registers[15]);

  // Publish daily max load power
  // pub(ID_HERE, data.registers[16]);

  // Publish daily charging amp hours
  pub(20, data.registers[17]);

  // Publish daily load amp hours
  // pub(ID_HERE, data.registers[18]);

  // Publish daily power generated
  pub(21, (data.registers[19]*0.001));

  // Publish daily load power used
  // pub(ID_HERE, (data.registers[20]*0.001));

  // Publish number of days operational
  pub(22, data.registers[21]);

  // Publish number of battery over-charges
  pub(23, data.registers[22]);

  // Publish number of battery full-charges
  pub(24, data.registers[23]);

  // Switch to second register group

  // Publish total battery charging amp-hours
  pub(25, ((data.registers2[0]*65536 + data.registers2[1])*0.001));

  // Publish total load consumption amp-hours
  // pub(ID_HERE, ((data.registers2[2]*65536 + data.registers2[3])*0.001));

  // Publish total power generation
  pub(26, ((data.registers2[4]*65536 + data.registers2[5])*0.001));

  // Publish total load power consumption
  // pub(ID_HERE, ((data.registers2[6]*65536 + data.registers2[7])*0.001));

  // Determine if there are any faults detected
  uint16_t faultID = data.registers2[10];
  // Serial.print("Register:\t");
  // Serial.println(data.registers2[9]);
  // Serial.print("FaultID:\t");
  // Serial.println(faultID);
  if(faultID != 0){
    short int count = 0;
    while(faultID != 0){
      if(faultID >= pow(2, 15-count)){
        client.publish(topics[27+count], faultCodes[count-1]);
        faultID -= pow(2, 15-count);
      }
      count += 1;
    }
  }
}

void loop(){
  network.update();
  if(network.available()){
    while (network.available()) {
      RF24NetworkHeader header;                       
      uint16_t payloadSize = network.peek(header);  // Use peek() to get the size of the payload
      network.read(header, &data, payloadSize);     // Is there anything ready for us?
    }
    delay(20); // Give it a moment to move everything. Was having issues with it moving too quick and sending corrupted data.
    printLCD();
    publishEverything();
    // Serial.println("------------");
    // for(int i = 0; i<numRegisters1; i++){
    //   Serial.print(i);
    //   Serial.print(":\t");
    //   Serial.println(data.registers[i]);
    // }
    // for(int i = 0; i<numRegisters2; i++){
    //   Serial.print(i+24);
    //   Serial.print(":\t");
    //   Serial.println(data.registers2[i]);
    // }
  }
}
