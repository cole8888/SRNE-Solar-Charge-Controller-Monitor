/*
    Cole L - 23 April 2023 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor

    UNTESTED

    This ESP8266 subscribes to MQTT topics for charge controller data and Tasmota smart plug statistics.

    This code runs on an ESP8266 NODEMCU 0.9 module.
*/

#include <LiquidCrystal_I2C.h> // LCD
#include <ESP8266WiFi.h>       // Enables The WiFi radio on the ESP8266.
#include <PubSubClient.h>      // https://github.com/knolleary/pubsubclient
#include <ArduinoJson.h>       // https://github.com/bblanchon/ArduinoJson

/*
    WiFi Settings.
*/
#define WIFI_SSID "CHANGE_ME!!!!"
#define WIFI_PASS "CHANGE_ME!!!!"

/*
    MQTT related settings. You many need to change these depending on your implementation.
*/
#define MQTT_PORT 1883                   // Port to use for MQTT.
#define MQTT_SERVER_ADDR "192.168.2.50"  // Address of the MQTT server.
#define MQTT_USER "CHANGE_ME!!!!"        // Username for the MQTT server.
#define MQTT_PASS "CHANGE_ME!!!!"        // Password for the MQTT server.
#define MQTT_CLIENT_ID "ESP8266"         // Client identifier for MQTT.
#define MQTT_RECONNECT_DELAY 5000

/*
    Other constant and settings.
*/
#define NUM_CHARGE_CONTROLLERS 3   // Number of nodes that will report charge controller data. Addresses of charge controller nodes must start counting up from at NUM_MISC_DATA_NODES+1.
#define NUM_PLUGS 3                // Number of plugs to use for calculating total power consumption.
#define WIFI_SINGLE_WAIT_DELAY 500 // Delay in ms for a single wait for the wifi to connect (time for a "." to show uo on lcd)
#define LCD_REFRESH_NODE 2         // Receiving a transmission from this node will cause the LCD to update. 
#define BATTERY_VOLTAGE_NODE 0         // Receiving a transmission from this node will cause the LCD to update. 

#define ROW_MAX_CHARS 21           // Maximum number of characters for row 1, includes null.
#define WATTS_STRING_MAX_CHARS 6   // Maximum number of characters for the watts string for the lcd. Includes the W unit and null.
#define CC_JSON_BUFFER_SIZE 2048   // Maximum size for the JSON string. It's actually around ~1600.
#define PLUG_JSON_BUFFER_SIZE 2048 // Maximum size for the JSON string. It's actually around ~1600.

/*
    Describes the different states the program can be in. 
*/
enum SOURCE {
    CC   = 0,
    PLUG = 1,
};
SOURCE msgSource;

int ccPower[NUM_CHARGE_CONTROLLERS] = {0, 0, 0};

uint8_t ccError[NUM_CHARGE_CONTROLLERS] = {0, 0, 0};

int plugPower[NUM_PLUGS] = {0, 0, 0};

float battVolts = 0.0;

int sums[2] = {0, 0};

StaticJsonDocument<CC_JSON_BUFFER_SIZE> cc_doc[NUM_CHARGE_CONTROLLERS];
StaticJsonDocument<PLUG_JSON_BUFFER_SIZE> plug_doc[NUM_PLUGS];

// Create the LCD device.
LiquidCrystal_I2C lcd(0x27, 20, 4);

void sumPower(){
    sums[0] = 0;
    sums[1] = 0;
    for(int i = 0; i<NUM_CHARGE_CONTROLLERS; i++){
        sums[0] += ccPower[i];
    }
    for(int i = 0; i<NUM_PLUGS; i++){
        sums[1] += plugPower[i];
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    uint8_t index = 0;
    msgSource = CC;

    if(strncmp(topic, "CC1", 3) == 0){
        index = 0;
    }
    else if(strncmp(topic, "CC2", 3) == 0){
        index = 1;
    }
    else if(strncmp(topic, "CC3", 3) == 0){
        index = 2;
    }
    else if(strncmp(topic, "tele/WaterHeater/SENSOR", 6) == 0){
        index = 0;
        msgSource = PLUG;      
    }
    else if(strncmp(topic, "tele/15AMP/SENSOR", 6) == 0){
        index = 1;
        msgSource = PLUG;      
    }
    else if(strncmp(topic, "tele/20AMP/SENSOR", 6) == 0){
        index = 2;
        msgSource = PLUG;      
    }
    else{
        return;
    }
    if(msgSource == CC){
        if(length >= CC_JSON_BUFFER_SIZE){
            Serial.print(F("Received message too large. ("));
            Serial.print(length);
            Serial.println(F(")"));
        }
        else{
            DeserializationError error = deserializeJson(cc_doc[index], (char*)payload);

            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.f_str());
                return;
            }
            else{
                if(cc_doc[index]["modbusErr"]){
					ccError[index] = 1;
                    
                }
                else{
                    ccPower[index] = (int)cc_doc[index]["charge"]["watts"];
                    ccError[index] = 0;
                }
            }
        }
        if(index == BATTERY_VOLTAGE_NODE){
            battVolts = (float)cc_doc[index]["battery"]["volts"];
        }
        if(index == LCD_REFRESH_NODE){
            printLCD();
        }
    }
    else{
        if(length >= PLUG_JSON_BUFFER_SIZE){
            Serial.print(F("Received message too large. ("));
            Serial.print(length);
            Serial.println(F(")"));
        }
        else{
            DeserializationError error = deserializeJson(plug_doc[index], (char*)payload);

            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.f_str());
                return;
            }
            else{
                plugPower[index] = (int)plug_doc[index]["ENERGY"]["Power"];
            }
        }
    }
    
}

// Create the WiFi and MQTT clients.
WiFiClient wifiClient;
PubSubClient client(MQTT_SERVER_ADDR, MQTT_PORT, callback, wifiClient);

void setup(){
    while (!Serial);
    Serial.begin(115200);

    // Startup LCD
    lcd.begin();
    lcd.backlight();
    lcd.setCursor(0, 0);
    
    lcd.print(F("Connecting"));

    WiFi.begin(WIFI_SSID, WIFI_PASS); // Begin WiFi connection
    // Wait for connection
    int trys = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(WIFI_SINGLE_WAIT_DELAY);
        Serial.print(F("."));
        lcd.print(F("."));
    }

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

    delay(1000);

    if(!client.setBufferSize(CC_JSON_BUFFER_SIZE)){
        Serial.println(F("MQTT buffer allocation failed!"));      
    }  
    if(!client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)){
        lcd.print(F("MQTT Failed :("));
        Serial.println(F("MQTT Connection failed..."));
        delay(1000);
        while(!client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)){
            Serial.println(F("MQTT Connection failed..."));
            delay(2000);
        }        
    }

    lcd.print(F("MQTT Connected :)"));
    Serial.println(F("MQTT Connected"));

    client.subscribe("CC1");
    client.subscribe("CC2");
    client.subscribe("CC3");
    client.subscribe("tele/WaterHeater/SENSOR");
    client.subscribe("tele/15AMP/SENSOR");
    client.subscribe("tele/20AMP/SENSOR");

    delay(1500); // Give enough time to read the details from the display.
}
    
/*
    Generate the watts string to be printed on the LCD
    When nodeId is negative then calculate the total across all charge controllers.
*/
void getWattsForDisplay(char* wattsString, int watts, int index){
    int temp = watts;

    // Is the charge controller offline?
    if(index > -1 && ccError[index]){
        snprintf(wattsString, WATTS_STRING_MAX_CHARS, "----W");
        return;
    }

    snprintf(wattsString, WATTS_STRING_MAX_CHARS, "%dW", temp);

    // Generate the correct spacing after the watts number.
    if(temp < 10){
        char space[4] = "   ";
        strncat(wattsString, space, 3);
    }
    else{
        char space[2] = " ";
        while(temp/1000 == 0){
            temp *= 10;
            strncat(wattsString, space, 1);
        }
    }
}

/*
    Print data to the attached LCD.
*/
void printLCD(){
    // Prepare display contents before we clear the display.
    // By doing this we decrease how long the display blanks for.

    char* wattsString = (char*)malloc(WATTS_STRING_MAX_CHARS);

    sumPower();

    // T:XXXXW Plug: XXXXW
    char row1[ROW_MAX_CHARS];
    getWattsForDisplay(wattsString, sums[0], -1); // Get total watts.
    snprintf(row1, sizeof(row1), "T:%s Plugs:%dW", wattsString, sums[1]);

    // F:XXXXW Bat: XX.XV
    char row2[ROW_MAX_CHARS];
    getWattsForDisplay(wattsString, ccPower[0], 0); // Get CC1 watts.
    snprintf(row2, sizeof(row2), "F:%s Bat: %.1fV", wattsString, battVolts);

    // S:XXXXW
    char row3[ROW_MAX_CHARS];
    getWattsForDisplay(wattsString, ccPower[1], 1); // Get CC2 watts.
    // snprintf(row3, sizeof(row3), "S:%s Hum: %d%%", wattsString, (int)bmeHum);
    snprintf(row3, sizeof(row3), "S:%s", wattsString);


    // B:XXXXW
    char row4[ROW_MAX_CHARS];
    getWattsForDisplay(wattsString, ccPower[2], 2); // Get CC3 watts.
    snprintf(row4, ROW_MAX_CHARS, "B:%s", wattsString);

    // Clear the display and start writing to it.
    lcd.clear();

    lcd.print(row1);

    lcd.setCursor(0, 1);
    lcd.print(row2);
    lcd.setCursor((int)(strlen(&row2[0])), 1);

    lcd.setCursor(0, 2);
    lcd.print(row3);

    lcd.setCursor(0, 3);
    lcd.print(row4);
    lcd.setCursor((int)(strlen(&row4[0])), 3);

    free(wattsString);
}

void reconnectMQTT(){
    while(!client.connected()){
        if(client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)){
            client.subscribe("CC1");
            client.subscribe("CC2");
            client.subscribe("CC3");
            client.subscribe("tele/WaterHeater/SENSOR");
            client.subscribe("tele/15AMP/SENSOR");
            client.subscribe("tele/20AMP/SENSOR");
            Serial.println(F("MQTT Recconected"));
        }
        else{
            Serial.print(F("MQTT Failed to reconnect: "));
            Serial.println(client.state());      
            delay(MQTT_RECONNECT_DELAY);
        }
    }
}

void loop(){
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();
}
