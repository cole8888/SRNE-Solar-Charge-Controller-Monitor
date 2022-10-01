/*
	Cole Lightfoot - 30th September 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
	This is intended to run on an arduino nano. Mine uses the old bootloader.
	Tested on both SRNE ML4860 and ML2440.

	Attached components are:
	- BME680 Breakout board: https://www.digikey.ca/short/cdwbnwd3
	- MAX3232 Breakout board: https://www.digikey.ca/short/08jnnm82
	- Adafruit ADS1115: https://www.digikey.ca/short/4rvj1mhq
	- ACS724 30A Breakout board (Two of these): https://www.digikey.ca/short/425ncd02
	- ACS712 30A Module: https://www.amazon.ca/Electronics-Salon-30Amp-Current-Sensor-Module/dp/B06WLN85M4/
	- RF24 module
	- Capacitors on the 3.3V and 5V power rails to prevent voltage drops.
	- 4.7K resistors between CC1 arduino and CC2 arduino on digital pins 6 and 7.
*/

#include <RF24.h>
#include <RF24Network.h>      // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <Adafruit_ADS1X15.h> // ADS1115 16bit ADC.
#include "Zanshin_BME680.h"   // BME680 Enviroment sensor.
#include <ModbusRtu.h>        // Modbus for talking to charge controller.
#include <SoftwareSerial.h>

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
	Digital pins used to prevent both nodes from transmitting at the same time.
*/
#define NODE1_BUSY 6
#define NODE2_BUSY 7

/*
    I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once 
    from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
	Modbus Constants
*/
#define MODBUS_SLAVE_ADDR 1
#define MODBUS_READ_CODE 3
#define MODBUS_REQUEST1_START_ADDR 256
#define MODBUS_REQUEST2_START_ADDR 280
#define MODBUS_MASTER_TIMEOUT 2000

/*
	Other settings.
*/
#define RECEIVE_NODE_ADDR 0     // Address of the receiver node.
#define THIS_NODE_ADDR 1        // Address of this node.
#define RF24_NETWORK_CHANNEL 90 // Channel the RF24 network should use.
#define REQUEST_DELAY 2000      // Delay in ms between requests to the charge controller over modbus.
#define ADC_SAMPLES 30          // Number of times to read the ADS1115 ADC to get an average. (Keep below 255)
#define ADC_DELAY 2             // Delay in ms between ADC readings to allow the ADC to settle.
#define SETUP_FAIL_DELAY 2000
/*
	Describes the different states the program can be in. 
*/
enum STATE {
	WAIT_REQUEST1  = 0,
	QUERY_REQUEST1 = 1,
	POLL_REQUEST1  = 2,
	WAIT_REQUEST2  = 3,
	QUERY_REQUEST2 = 4,
	POLL_REQUEST2  = 5,
	TRANSMIT       = 6
};
STATE state;

unsigned long lastTime; // Last time isTime() was run and returned 1.
uint16_t lastErrCnt;
const short int adcSamples = 30;
// Create the RF24 radio and network
RF24 radio(9, 10);    // CE, CSN
RF24Network network(radio);

// Create software serial for communicating with Charge Controller
SoftwareSerial mySerial(2, 3);

// Create modbus device
Modbus master(0, mySerial);
modbus_t telegram[2];

// Create ADS1115 16bit ADC
Adafruit_ADS1115 ads1115;

// Create BME680 Environment sensor
BME680_Class BME680;

/*
    This is the structure which is transmitted from node1/charge controller 1 to the receiver.
    Front panel amps are split into two parts because the ACS724 only goes up to 30A and I needed 60A, so I placed two ACS724 sensors in parallel.
    These are the raw sensor values (except BME680 ones) and they need to be translated into their actual values later.
*/
typedef struct package{
    float panelAmpsBack;   // Measured Back panel amps (ACS712 current sensor)
    float panelAmpsFrontA; // Measured Front panel amps sensor 1/2 (ACS724 current sensor)
    float panelAmpsFrontB; // Measured Front panel amps sensor 1/2 (ACS724 current sensor)
    // float batteryVolts;    // Measured battery voltage (ADS1115 and a voltage divider. R1=56K, R2=10K)
    int32_t bmePres;         // Atmospheric pressure measured by BME680
    int32_t bmeTemp;         // Air temperature measure by BME680
    int32_t bmeHum;          // Air humidity measured by BME680
    int32_t bmeGas;          // Volatile Organic Compounds measured in kOhms by BME680
    uint16_t modbusErrDiff;  // Modbus Error Difference.
    uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller
    uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller
}Package;
Package data;

// Read the ADC data.
void readADC(){
    for(short int samples = 0; samples < adcSamples; samples++){
        data.panelAmpsFrontA += ads1115.readADC_SingleEnded(FRONT_PANELS_A_ADS1115);
        data.panelAmpsFrontB += ads1115.readADC_SingleEnded(FRONT_PANELS_B_ADS1115);
        data.panelAmpsBack += ads1115.readADC_SingleEnded(BACK_PANELS_ADS1115);
        // data.batteryVolts += ads1115.readADC_SingleEnded(BATT_VOLT_ADS1115);
        delay(ADC_DELAY); // Let the ADC settle down before the next sample.
    }
    // Average out the samples
    data.panelAmpsFrontA = data.panelAmpsFrontA / (float)adcSamples;
    data.panelAmpsFrontB = data.panelAmpsFrontB / (float)adcSamples;
	data.panelAmpsBack = data.panelAmpsBack / (float)adcSamples;
	// data.batteryVolts = data.batteryVolts / (float)ADC_SAMPLES;
}

void setup(){
    Serial.begin(115200);

    // Start the software serial for the modbus connection
    mySerial.begin(9600);

    // Initialize the pins which are used to talk to the other transmitter to make sure they never transmit at the same time.
    pinMode(NODE1_BUSY, OUTPUT);
    pinMode(NODE2_BUSY, INPUT);
    digitalWrite(NODE1_BUSY, LOW);

    // Initialize BME680 Sensor.
    while(!BME680.begin(I2C_STANDARD_MODE)){
        Serial.print(F("\nBME Fail"));
        delay(SETUP_FAIL_DELAY);
    }

    // Setup the BME680 Sensor.
    BME680.setOversampling(TemperatureSensor, Oversample16);
    BME680.setOversampling(HumiditySensor, Oversample16);
    BME680.setOversampling(PressureSensor, Oversample16);
    BME680.setIIRFilter(IIR4);
    BME680.setGas(320, 150);    // 320*c for 150 milliseconds
    Serial.println(F("\nBME Go"));

    // Initialize the ADS115 16bit ADC
    while(!ads1115.begin()){
        Serial.println(F("ADS Fail"));
		delay(SETUP_FAIL_DELAY);
    }
    Serial.println(F("ADS Go"));

    // Initialize the RF24 radio    
    while(!radio.begin()){
        Serial.print(F("RF24 Fail"));
		delay(SETUP_FAIL_DELAY);
    }
    Serial.print(F("RF24 Go"));
    network.begin(RF24_NETWORK_CHANNEL, THIS_NODE_ADDR);
    
    // Prepare the commands we will use to request data from the charge controller.
    telegram[0].u8id = telegram[1].u8id = MODBUS_SLAVE_ADDR;
    telegram[0].u8fct = telegram[1].u8fct = MODBUS_READ_CODE;
    telegram[0].u16RegAdd = MODBUS_REQUEST1_START_ADDR;
    telegram[0].u16CoilsNo = NUM_REGISTERS_REQUEST1;
    telegram[0].au16reg = data.request1;

    telegram[1].u16RegAdd = MODBUS_REQUEST2_START_ADDR;
    telegram[1].u16CoilsNo = NUM_REGISTERS_REQUEST2;
    telegram[1].au16reg = data.request2;
    
    // Start modbus
    master.start();
    master.setTimeOut(MODBUS_MASTER_TIMEOUT);
    lastTime = millis();
    lastErrCnt = 0;
    state = WAIT_REQUEST1; 

    delay(20);
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
    if(state == WAIT_REQUEST1){
        if(isTime()){
            state = QUERY_REQUEST1;
        }
    }
    else if(state == QUERY_REQUEST1){
        master.query(telegram[0]);
        state = POLL_REQUEST1;
    }
    else if(state == POLL_REQUEST1){
        master.poll();
        if(master.getState() == COM_IDLE){
            state = WAIT_REQUEST2;
        }
    }
    else if(state == WAIT_REQUEST2){
        if(isTime()){
        	state = QUERY_REQUEST2;
        }
    }
    else if(state == QUERY_REQUEST2){
        master.query(telegram[1]);
        state = POLL_REQUEST2;
    }
    else if(state == POLL_REQUEST2){
        master.poll();
        if(master.getState() == COM_IDLE){
            state = TRANSMIT;
        }
    }
    // Transmit state
    else if(state == TRANSMIT){
		// Number of modbus errors we encountered this time.
        data.modbusErrDiff = (uint16_t) (master.getErrCnt() - lastErrCnt);
        lastErrCnt = master.getErrCnt();
        
        // Get the BME680 data. This takes a while.
        // static int32_t temp, humidity, pressure, gas;    // Store BME readings
        BME680.getSensorData(data.bmeTemp, data.bmeHum, data.bmePres, data.bmeGas); // Tell BME680 to begin measurement.
        // data.bmeTemp = temp / 100.0;
        // data.bmePres = pressure / 100.0;
        // data.bmeHum = humidity / 1000.0;
        // data.bmeGas = gas / 100.0;

        // data.panelAmpsFrontA = data.panelAmpsFrontB = data.panelAmpsBack = data.batteryVolts = 0; // Input 0 of my ADC is broken, enable if you need this.
        data.panelAmpsFrontA = data.panelAmpsFrontB = data.panelAmpsBack = 0;

        // Do the ADC readings
        readADC();

		Serial.print("FrontA");
		Serial.println(data.panelAmpsFrontA);
		Serial.print("FrontB");
		Serial.println(data.panelAmpsFrontB);
		Serial.print("Back");
		Serial.println(data.panelAmpsBack);

        // // Print CC data (Troubleshooting)
        // for(int i = 0; i<NUM_REGISTERS_REQUEST1; i++){
        //     Serial.print(i);
        //     Serial.print("\t");
        //     Serial.println(data.request1[i]);
        // }
        // for(int i = 0; i<NUM_REGISTERS_REQUEST2; i++){
        //     Serial.print(NUM_REGISTERS_REQUEST1 + i);
        //     Serial.print("\t");
        //     Serial.println(data.request2[i]);
        // }

        // Check if other node is transmitting, if so then wait and check again.
        while(digitalRead(NODE2_BUSY) == HIGH){
           Serial.println(F("Busy"));
            delay(20);
        }
        
        // Indicate to the other node that we are transmitting
        digitalWrite(NODE1_BUSY, HIGH);
        delay(10);
        // Send the data to the receiver
        RF24NetworkHeader header(RECEIVE_NODE_ADDR);
        network.write(header, &data, sizeof(data));
        delay(10);
        // Indicate to the other node that we are no longer transmitting.
        digitalWrite(NODE1_BUSY, LOW);

        // Return to original state
        state = WAIT_REQUEST1;
    }
}
