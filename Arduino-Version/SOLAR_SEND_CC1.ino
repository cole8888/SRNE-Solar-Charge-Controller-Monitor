// Cole Lightfoot - 22nd June 2021 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
// This is intended to run on an arduino nano. Mine uses the old bootloader.
// Tested on both SRNE ML4860 and ML2440.

// Attached components are:
// BME680 Breakout board: https://www.digikey.ca/short/cdwbnwd3
// MAX3232 Breakout board: https://www.digikey.ca/short/08jnnm82
// Adafruit ADS1115: https://www.digikey.ca/short/4rvj1mhq
// ACS724 30A Breakout board (Two of these): https://www.digikey.ca/short/425ncd02
// ACS712 30A Module: https://www.amazon.ca/Electronics-Salon-30Amp-Current-Sensor-Module/dp/B06WLN85M4/
// RF24 module
// Capacitors on the 3.3V and 5V power rails to prevent voltage drops.
// 4.7K resistors between CC1 arduino and CC2 arduino on digital pins 6 and 7.

#include <nRF24L01.h>
#include <RF24.h>
#include <RF24Network.h>        // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <Adafruit_ADS1X15.h>   // ADS1115 16bit ADC.
#include "Zanshin_BME680.h"     // BME680 Enviroment sensor.
#include <ModbusRtu.h>          // Modbus for talking to charge controller.
#include <SoftwareSerial.h>

// Battery Voltage ADS1115 Analog Pin
// Voltage divider makes 0-33V into 0-5V. R1=56K, R2=10K
// ADC input 0 is broken, enable when replaced.
// #define battVoltIn 0

// FRONT Solar Amps ADS1115 Analog Pins (Two ACS724 sensors in parallel)
#define solarAmpInF_A 1
#define solarAmpInF_B 2

// BACK Solar Amps ADS1115 Analog Pin (ACS712)
#define solarAmpInB 3

// Digital pins used to indicate when an arduino is transmiting.
#define node1_busy 6
#define node2_busy 7

// Create the RF24 radio and network
RF24 radio(9, 10);    // CE, CSN
RF24Network network(radio);
const uint16_t box_node = 01;     // Address of this node
const uint16_t house_node = 00; // Address of receiver node

// Create ADS1115 16bit ADC
Adafruit_ADS1115 ads1115;

// Create BME680 Environment sensor
BME680_Class BME680;

// Create software serial for communicating with Charge Controller
SoftwareSerial mySerial(2, 3);

// Create modbus device
Modbus master(0, mySerial);
modbus_t telegram[2];
uint8_t u8state;
unsigned long u32wait;

// I had issues with the transmitter's Serial buffers running out of room when receiving more than
// 29 registers at once from the charge controller. Solved this by just sending two separate requests.
// Each request has a different number of registers in it.
const short int numRegisters1 = 24;
const short int numRegisters2 = 11;

// Number of times to read the ADC to get an average.
const short int adcSamples = 30;

// This is the struct which is transmitted to the receiver and contains all the data.
// Front panel amps are split into two because the ACS724 only goes up to 30A and I needed 60A, so I placed two ACS724 sensors in parallel.
// These are the raw sensor values (except BME680 ones) and they need to be translated into their actual values later by the receiver
typedef struct package{
    float dcAmpsBack;     // Measured Back panel amps (ACS712 current sensor)
    float dcAmpsFrontA; // Measured Front panel amps sensor 1/2 (ACS724 current sensor)
    float dcAmpsFrontB; // Measured Front panel amps sensor 1/2 (ACS724 current sensor)
    // ADC input 0 is broken, enable when replaced.
    // float dcVolts;            // Measured battery voltage (ADS1115 and a voltage divider. R1=56K, R2=10K)
    float pres;                 // Atmospheric pressure measured by BME680
    float temp;                 // Air temperature measure by BME680
    float hum;                    // Air humidity measured by BME680
    float gas;                    // Volatile Organic Compounds measure in kOhms by BME680
    uint16_t registers[numRegisters1];    // First group of registers from the charge controller
    uint16_t registers2[numRegisters2]; // Second group of registers from the charge controller
}Package;
Package data;

// Read the ADC data.
void readADC(){
    for(short int x = 0; x < adcSamples; x++){
        data.dcAmpsFrontA += ads1115.readADC_SingleEnded(solarAmpInF_A);
        data.dcAmpsFrontB += ads1115.readADC_SingleEnded(solarAmpInF_B);
        data.dcAmpsBack += ads1115.readADC_SingleEnded(solarAmpInB);
        // ADC input 0 is broken, enable when replaced.
        // data.dcVolts += ads1115.readADC_SingleEnded(battVoltIn);
        delay(2); // Let the ADC settle down before the next sample.
    }
    // Average out the samples
    // ADC input 0 is broken, enable when replaced.
    // data.dcVolts = data.dcVolts / (float)adcSamples;
    data.dcAmpsBack = data.dcAmpsBack / (float)adcSamples;
    data.dcAmpsFrontA = data.dcAmpsFrontA / (float)adcSamples;
    data.dcAmpsFrontB = data.dcAmpsFrontB / (float)adcSamples;
}

void setup(){
    Serial.begin(115200);

    // Start the software serial for the modbus connection
    mySerial.begin(9600);

    // Initialize the pins which are used to talk to the other transmitter to make sure they never transmit at the same time.
    pinMode(node1_busy, OUTPUT);
    pinMode(node2_busy, INPUT);
    digitalWrite(node1_busy, LOW);

    // Initialize BME680 Sensor.
    while(!BME680.begin(I2C_STANDARD_MODE)){
        Serial.print(F("\nBME Fail"));
        delay(2000);
    }

    // Setup the BME680 Sensor.
    BME680.setOversampling(TemperatureSensor, Oversample16);
    BME680.setOversampling(HumiditySensor, Oversample16);
    BME680.setOversampling(PressureSensor, Oversample16);
    BME680.setIIRFilter(IIR4);
    BME680.setGas(320, 150);    // 320*c for 150 milliseconds
    Serial.println(F("\nBME Start"));

    // Initialize the ADS115 16bit ADC
    while(!ads1115.begin()){
        Serial.println(F("ADS Fail"));
    }
    Serial.println(F("ADS Start"));

    // Initialize the RF24 radio    
    while(!radio.begin()){
        Serial.print(F("RF24 Fail"));
    }
    Serial.print(F("RF24 Start"));
    network.begin(/*channel*/ 90, /*node address*/ box_node);
    
    // Prepare the commands we will use to request data from the charge controller.
    telegram[0].u8id = telegram[1].u8id = 1;    // Slave address
    telegram[0].u8fct = telegram[1].u8fct = 3;  // Function code (this one is registers read)
    telegram[0].u16RegAdd = 256;                // Start address in slave
    telegram[0].u16CoilsNo = numRegisters1;     // Number of elements (coils or registers) to read
    telegram[0].au16reg = data.registers;       // Pointer to a memory array in the Arduino

    telegram[1].u16RegAdd = 280;                // Start address in slave
    telegram[1].u16CoilsNo = numRegisters2;     // Number of elements (coils or registers) to read
    telegram[1].au16reg = data.registers2;      // Pointer to a memory array in the Arduino
    
    // Start modbus
    master.start();
    master.setTimeOut(2000);
    u32wait = millis() + 1000;
    u8state = 0; 

    delay(20);
}

void loop(){
    network.update();
    // Wait CC1 - Group 1
    if(u8state == 0){
        if(millis() > u32wait){
            u8state = 1;
        }
    }
    // Query CC1 - Group 1
    else if(u8state == 1){
        master.query(telegram[0]);
        u8state = 2;
    }
    // Poll CC1 - Group 1
    else if(u8state == 2){
        master.poll();
        if(master.getState() == COM_IDLE){
            u8state = 3;
            u32wait = millis() + 2000; 
        }
    }
    // Wait CC1 - Group 2
    else if(u8state == 3){
        if(millis() > u32wait){
            u8state = 4;
        }
    }
    // Query CC1 - Group 2
    else if(u8state == 4){
        master.query(telegram[1]);
        u8state = 5;
    }
    // Poll CC1 - Group 2
    else if(u8state == 5){
        master.poll();
        if(master.getState() == COM_IDLE){
            u8state = 6;
            u32wait = millis() + 2000; 
        }
    }
    // Transmit state
    else if(u8state == 6){
        // Get the BME680 data. This takes a while.
        static int32_t    temp, humidity, pressure, gas;    // Store BME readings
        BME680.getSensorData(temp, humidity, pressure, gas); // Tell BME680 to begin measurement.
        data.temp = temp / 100.0;
        data.pres = pressure / 100.0;
        data.hum = humidity / 1000.0;
        data.gas = gas / 100.0;

        // ADC input 0 is broken, enable when replaced.
        // data.dcAmpsFrontA = data.dcAmpsFrontB = data.dcAmpsBack = data.dcVolts = 0;
        data.dcAmpsFrontA = data.dcAmpsFrontB = data.dcAmpsBack = 0;

        // Do the ADC readings
        readADC();

        // // Print CC data (Troubleshooting)
        // for(int i = 0; i<numRegisters1; i++){
        //     Serial.print(i);
        //     Serial.print("\t");
        //     Serial.println(data.registers[i]);
        // }
        // for(int i = 0; i<numRegisters2; i++){
        //     Serial.print(numRegisters1 + i);
        //     Serial.print("\t");
        //     Serial.println(data.registers2[i]);
        // }

        // Check if other node is transmiting, if so then wait and check again.
        while(digitalRead(node2_busy) == HIGH){
            Serial.println(F("Busy."));
            delay(20);
        }
        
        // Indicate to the other node that we are transmiting
        digitalWrite(node1_busy, HIGH);
        delay(10);
        // Send the data to the receiver
        RF24NetworkHeader header(/*to node*/ house_node);
        network.write(header, &data, sizeof(data));
        delay(10);
        // Indicate to the other node that we are no longer transmiting.
        digitalWrite(node1_busy, LOW);

        // Return to original state
        u8state = 0;
    }
}
