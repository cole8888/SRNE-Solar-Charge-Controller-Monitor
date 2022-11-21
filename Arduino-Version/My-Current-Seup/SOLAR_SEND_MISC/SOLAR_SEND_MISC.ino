/*
    Cole L - 20th November 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor

    This arduino returns miscellaneous data from various sensors.

    This is intended to run on an arduino nano. Mine uses the old bootloader.

    Attached components are:
    - BME680 Breakout board: https://www.digikey.ca/short/cdwbnwd3
    - Adafruit ADS1115: https://www.digikey.ca/short/4rvj1mhq
    - ACS724 30A Breakout board (Two of these): https://www.digikey.ca/short/425ncd02
    - ACS712 30A Module: https://www.amazon.ca/Electronics-Salon-30Amp-Current-Sensor-Module/dp/B06WLN85M4/
    - RF24 module
    - Capacitors on the 3.3V and 5V power rails to prevent voltage drops.
*/

#include <RF24.h>
#include <RF24Network.h>      // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <Adafruit_ADS1X15.h> // ADS1115 16bit ADC.
#include "Zanshin_BME680.h"   // BME680 Enviroment sensor.
#include <avr/wdt.h>          // Watchdog timer.

/*
    Solar Amps ADS1115 Analog Pins (Front A&B are in parallel)
*/
#define FRONT_PANELS_A_ADS1115 1
#define FRONT_PANELS_B_ADS1115 2
#define BACK_PANELS_ADS1115 3

/*
    Battery Voltage ADS1115 Analog Pin
    Voltage divider makes 0-33V into 0-5V. R1=56K, R2=10K
*/
// #define BATT_VOLT_ADS1115 0 // Input 0 of my ADC is broken, enable if you need this.

/*
    I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once 
    from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
    Other settings.
*/
#define RECEIVE_NODE_ADDR 0                   // Address of the receiver node.
#define THIS_NODE_ADDR 1                      // Address of this node.
#define RF24_NETWORK_CHANNEL 90               // Channel the RF24 network should use.
#define REQUEST_DELAY 2000                    // Delay in ms between requests to the charge controller over modbus.
#define ADC_SAMPLES 30                        // Number of times to read the ADS1115 ADC to get an average. (Keep below 255)
#define ADC_DELAY 2                           // Delay in ms between ADC readings to allow the ADC to settle.
#define SETUP_FAIL_DELAY 2000                 // Delay when retrying setup tasks.
#define SETUP_FINISH_DELAY 100*THIS_NODE_ADDR // Delay after finishing setup. (Multiplied by address to to avoid interference on startup).

/*
    Describes the different states the program can be in. 
*/
enum STATE {
    WAIT_BME680   = 0,
    QUERY_BME680  = 1,
    WAIT_ADS1115  = 2,
    QUERY_ADS1115 = 3,
    TRANSMIT      = 4
};
STATE state;

unsigned long lastTime; // Last time isTime() was run and returned 1.
const short int adcSamples = 30;
// Create the RF24 radio and network
RF24 radio(9, 10);    // CE, CSN
RF24Network network(radio);

// Create ADS1115 16bit ADC
Adafruit_ADS1115 ads1115;

// Create BME680 Environment sensor
BME680_Class BME680;

/*
    This is the structure which is transmitted from node1/charge controller 1 to the receiver.
    Front panel amps are split into two parts because the ACS724 only goes up to 30A and I needed 60A, so I placed two ACS724 sensors in parallel.
    These are the raw sensor values and they need to be translated into their actual values later.
*/
typedef struct package{
    float panelAmpsBack;   // Measured Back panel amps (ACS712 current sensor)
    float panelAmpsFrontA; // Measured Front panel amps sensor 1/2 (ACS724 current sensor)
    float panelAmpsFrontB; // Measured Front panel amps sensor 1/2 (ACS724 current sensor)
    // float batteryVolts;    // Measured battery voltage (ADS1115 and a voltage divider. R1=56K, R2=10K) // Input 0 of my ADC is broken, enable if you need this.
    int32_t bmePres;       // Atmospheric pressure measured by BME680
    int32_t bmeTemp;       // Air temperature measure by BME680
    int32_t bmeHum;        // Air humidity measured by BME680
    int32_t bmeGas;        // Volatile Organic Compounds measured in kOhms by BME680
}Package;
Package data;

// Read the ADC data.
void readADC(){
    for(short int samples = 0; samples < adcSamples; samples++){
        data.panelAmpsFrontA += ads1115.readADC_SingleEnded(FRONT_PANELS_A_ADS1115);
        data.panelAmpsFrontB += ads1115.readADC_SingleEnded(FRONT_PANELS_B_ADS1115);
        data.panelAmpsBack += ads1115.readADC_SingleEnded(BACK_PANELS_ADS1115);
        delay(ADC_DELAY); // Let the ADC settle down before the next sample.
    }
    // Average out the samples
    data.panelAmpsFrontA = data.panelAmpsFrontA / (float)adcSamples;
    data.panelAmpsFrontB = data.panelAmpsFrontB / (float)adcSamples;
    data.panelAmpsBack = data.panelAmpsBack / (float)adcSamples;
}

void setup(){
    Serial.begin(115200);

    // Initialize BME680 Sensor.
    while(!BME680.begin(I2C_STANDARD_MODE)){
        Serial.print(F("BME Fail\n"));
        delay(SETUP_FAIL_DELAY);
    }

    // Setup the BME680 Sensor.
    BME680.setOversampling(TemperatureSensor, Oversample16);
    BME680.setOversampling(HumiditySensor, Oversample16);
    BME680.setOversampling(PressureSensor, Oversample16);
    BME680.setIIRFilter(IIR4);
    BME680.setGas(320, 150);    // 320*c for 150 milliseconds
    Serial.println(F("\nBME Go\n"));

    // Initialize the ADS115 16bit ADC
    while(!ads1115.begin()){
        Serial.println(F("ADS Fail\n"));
        delay(SETUP_FAIL_DELAY);
    }
    Serial.println(F("ADS Go\n"));

    // Initialize the RF24 radio    
    while(!radio.begin()){
        Serial.print(F("RF24 Fail\n"));
        delay(SETUP_FAIL_DELAY);
    }
    Serial.print(F("RF24 Go\n"));
    network.begin(RF24_NETWORK_CHANNEL, THIS_NODE_ADDR);
    
    lastTime = millis();
    state = WAIT_BME680; 

    delay(SETUP_FINISH_DELAY);

    // Start the 4 second watchdog
    wdt_enable(WDTO_4S);
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

void loop(){
    network.update();
    if(state == WAIT_BME680){
        if(isTime()){
            state = QUERY_BME680;
        }
    }
    else if(state == QUERY_BME680){
        // Get the BME680 data. This takes a while.
        BME680.getSensorData(data.bmeTemp, data.bmeHum, data.bmePres, data.bmeGas); // Tell BME680 to begin measurement.
        state = WAIT_ADS1115;
    }
    else if(state == WAIT_ADS1115){
        if(isTime()){
            state = QUERY_ADS1115;
        }
    }
    else if(state == QUERY_ADS1115){
        data.panelAmpsFrontA = data.panelAmpsFrontB = data.panelAmpsBack = 0;

        // Do the ADC readings
        readADC();
        state = TRANSMIT;
    }
    else{
        // Send the data to the receiver
        delay(10);
        RF24NetworkHeader header(RECEIVE_NODE_ADDR);
        network.write(header, &data, sizeof(data));
        delay(10);

        // Return to original state
        state = WAIT_BME680;
    }
    // Reset watchdog
    wdt_reset();
}
