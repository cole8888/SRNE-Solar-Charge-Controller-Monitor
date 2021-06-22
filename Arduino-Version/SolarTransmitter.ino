// Cole Lightfoot - 22nd June 2021 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
// This is intended to run on an arduino nano. Mine uses the old bootloader.
// Tested on both SRNE ML4860 and ML2440.

// This has several components attached:
// BME680 Breakout board: https://www.digikey.ca/short/cdwbnwd3
// MAX3232 Breakout board: https://www.digikey.ca/short/08jnnm82
// ADS1115: https://www.digikey.ca/short/4rvj1mhq
// ACS724 30A Breakout board: https://www.digikey.ca/short/425ncd02
// ACS712 30A Module: https://www.amazon.ca/Electronics-Salon-30Amp-Current-Sensor-Module/dp/B06WLN85M4/
// RF24 module

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <RF24Network.h>      // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <Adafruit_ADS1X15.h> // ADS1115 16bit ADC
#include "Zanshin_BME680.h"   // BME680 Enviroment sensor
#include <ModbusRtu.h>        // Modbust for talking to charge controller
#include <SoftwareSerial.h>

// Battery Voltage ADS1115 Analog Pin
#define battVoltIn 0

// FRONT Solar Amps ADS1115 Analog Pins (Two sensors in parallel)
#define solarAmpInF_A 1
#define solarAmpInF_B 2

// BACK Solar Amps ADS1115 Analog Pin
#define solarAmpInB 3

// Volts per bit of the ADS1115 ADC (6.144/32768)
#define VPB 0.0001875

RF24 radio(9, 10);  // CE, CSN
RF24Network network(radio);          // Network uses that radio
const uint16_t box_node = 01;       // Address of our node in Octal format
const uint16_t house_node = 00;      // Address of the other node in Octal format

// Create ADS1115 16bit ADC
Adafruit_ADS1115 ads1115;

// Create BME680 Enviromental sensor
BME680_Class BME680;

// Create software serial for communicating with Charge Controller
SoftwareSerial mySerial(2, 3);
// Create modbus device
Modbus master(0, mySerial);
modbus_t telegram[2];

// I had issues with the transmitter's Serial buffers running out of room when receiving more than
// 29 registers at once from the charge controller. Solved this by just sending two seperate requests.
// Each request has a different number of registers in it.
const short int numRegisters1 = 24;
const short int numRegisters2 = 11;

const short int adcSamples = 30;    // Number of times to read the ADC to get an average.

// This is the struct which is transmitted to the receiver and contains all the data.
// Front panel amps are split into two because the ACS724 only goes up to 30A and I needed 60A,
// so I placed two ACS724 sensors in paralel.
// These are the raw sensor values (except BME680 ones) and they need to be translated into their actual values later by the receiver
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

void readADC(){
  for (short int x = 0; x < adcSamples; x++){ //Get samples
    data.dcAmpsFrontA += ads1115.readADC_SingleEnded(solarAmpInF_A);
    data.dcAmpsFrontB += ads1115.readADC_SingleEnded(solarAmpInF_B);
    data.dcAmpsBack += ads1115.readADC_SingleEnded(solarAmpInB);
    data.dcVolts += ads1115.readADC_SingleEnded(battVoltIn);
    delay(2); // Let the ADC settle down before the next sample.
  }
  // Average out the samples
  data.dcVolts = data.dcVolts / (float)adcSamples;
  data.dcAmpsBack = data.dcAmpsBack / (float)adcSamples;
  data.dcAmpsFrontA = data.dcAmpsFrontA / (float)adcSamples;
  data.dcAmpsFrontB = data.dcAmpsFrontB / (float)adcSamples;
}

// Read ML4860 registers 256 - 279 (0x0100 - 0x0117)
void readCC1Registers1(){
 master.query(telegram[0]); // send query (only once)
 delay(20);
 master.poll(); // check incoming messages
 // for(int i = 0; i<numRegisters1; i++){
 //   Serial.print(i);
 //   Serial.print(":\t");
 //   Serial.println(data.registers[i]);
 // }
}

// Read ML4860 registers 280 - 290 (0x0118 - 0x0122)
void readCC1Registers2(){
 master.query(telegram[1]); // send query (only once)
 delay(20);
 master.poll(); // check incoming messages
 // for(int i = 0; i<numRegisters2; i++){
 //   Serial.print(numRegisters1 + i);
 //   Serial.print(":\t");
 //   Serial.println(data.registers2[i]);
 // }
}

void setup(){
  Serial.begin(115200);

  // Initialize BME680
  while (!BME680.begin(I2C_STANDARD_MODE)) {  // Start BME680 using I2C, use first device found
    Serial.print(F("\nBME680 Failed"));
    delay(2000);
  }  // loop until device is located

  // Setup the BME680
  BME680.setOversampling(TemperatureSensor, Oversample16);
  BME680.setOversampling(HumiditySensor, Oversample16);
  BME680.setOversampling(PressureSensor, Oversample16);
  BME680.setIIRFilter(IIR4);
  BME680.setGas(320, 150);  // 320*c for 150 milliseconds
  Serial.println(F("\nBME680 Started"));

  // Initialize the ADS115 16bit ADC
  ads1115.begin();
  Serial.println(F("ADS1115 Started"));

  // Initialize the RF24 radio  
  while (!radio.begin()) {
    Serial.print(F("\nRF24 Failed"));
    delay(2000);
  }
  network.begin(/*channel*/ 90, /*node address*/ box_node);

  // Start the software serial for the modbus connection
  mySerial.begin(9600);
  // Start modbus
  master.start();
  master.setTimeOut(5000);
  
  // Prepare the commands we will use to request data from the charge controller.
  telegram[0].u8id = telegram[1].u8id = 1; // slave address
  telegram[0].u8fct = telegram[1].u8fct = 3; // function code (this one is registers read)
  telegram[0].u16RegAdd = 256; // start address in slave
  telegram[0].u16CoilsNo = numRegisters1; // number of elements (coils or registers) to read
  telegram[0].au16reg = data.registers; // pointer to a memory array in the Arduino
  
  telegram[1].u16RegAdd = 280; // start address in slave
  telegram[1].u16CoilsNo = numRegisters2; // number of elements (coils or registers) to read
  telegram[1].au16reg = data.registers2; // pointer to a memory array in the Arduino
  delay(20);
}

void loop(){
  network.update();
  
  data.dcAmpsFrontA = data.dcAmpsFrontB = data.dcAmpsBack = data.dcVolts = 0;
  
  // Get the BME680 data. This takes a while.
  static int32_t  temp, humidity, pressure, gas;  // Store BME readings
  BME680.getSensorData(temp, humidity, pressure, gas); // Tell BME680 to begin measurement.
  data.temp = temp / 100.0;
  data.pres = pressure / 100.0;
  data.hum = humidity / 1000.0;
  data.gas = gas / 100.0;

  // I'm being generous with the delays because I was having some issues getting the charge controller registers to update. These delays can definitely be shrunk down a fair bit.
  delay(20);

  // Do the ADC readings
  readADC();

  delay(20);
  
  // Read the first set of Charge Controller registers
  readCC1Registers1();
  
  delay(100);
  
  // Read the second set of Charge Controller registers
  readCC1Registers2();
  
  delay(100);

  // Send the data to the receiver
  RF24NetworkHeader header(/*to node*/ house_node);
  network.write(header, &data, sizeof(data));
  delay(200);
}
