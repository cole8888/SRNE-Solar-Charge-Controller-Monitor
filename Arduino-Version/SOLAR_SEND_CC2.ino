// Cole Lightfoot - 25th July 2021 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
// This is intended to run on an arduino nano. Mine uses the old bootloader.
// Tested on both SRNE ML4860 and ML2440.

// This arduino connects to the ML2440 Charge Controller (CC2)

// Attached components are:
// MAX3232 Breakout board: https://www.digikey.ca/short/08jnnm82
// RF24 module
// Capacitors on the 3.3V and 5V power rails to prevent voltage drops.
// 4.7K resistors between CC1 arduino and CC2 arduino on digital pins 6 and 7.

#include <nRF24L01.h>
#include <RF24.h>
#include <RF24Network.h>      // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <ModbusRtu.h>        // Modbus for talking to charge controller
#include <SoftwareSerial.h>

// Digital pins used to indicate when an arduino is transmiting.
#define node1_busy 6
#define node2_busy 7

// Create the RF24 radio and network
RF24 radio(9, 10);  // CE, CSN
RF24Network network(radio);
const uint16_t box_node = 02;   // Address of this node
const uint16_t house_node = 00; // Address of receiver node

// Create software serial for communicating with Charge Controller.
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

// This is the struct which is transmitted to the receiver and contains all the data.
typedef struct package{
    uint16_t registers[numRegisters1];  // First group of registers from the charge controller
    uint16_t registers2[numRegisters2]; // Second group of registers from the charge controller
}Package;
Package data;

void setup(){
    Serial.begin(115200);

    // Start the software serial for the modbus connection
    mySerial.begin(9600);

    // Initialize the pins which are used to talk to the other transmitter to make sure they never transmit at the same time.
    pinMode(node1_busy, INPUT);
    pinMode(node2_busy, OUTPUT);
    digitalWrite(node2_busy, LOW);

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
    // Wait CC2 - Group 1
    if(u8state == 0){
        if(millis() > u32wait){
            u8state = 1;
        }
    }
    // Query CC2 - Group 1
    else if(u8state == 1){
        master.query(telegram[0]);
        u8state = 2;
    }
    // Poll CC2 - Group 1
    else if(u8state == 2){
        master.poll();
        if(master.getState() == COM_IDLE){
            u8state = 3;
            u32wait = millis() + 2000; 
        }
    }
    // Wait CC2 - Group 2
    else if(u8state == 3){
        if(millis() > u32wait){
        u8state = 4;
        }
    }
    // Query CC2 - Group 2
    else if(u8state == 4){
        master.query(telegram[1]);
        u8state = 5;
    }
    // Poll CC2 - Group 2
    else if(u8state == 5){
        master.poll();
        if(master.getState() == COM_IDLE){
            u8state = 6;
            u32wait = millis() + 2000; 
        }
    }
    // Transmit state
    else if(u8state == 6){
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
        while(digitalRead(node1_busy) == HIGH){
            Serial.println(F("Busy."));
            delay(20);
        }

        // Indicate to the other node that we are transmiting.
        digitalWrite(node2_busy, HIGH);
        delay(20);
        // Send the data to the receiver.
        RF24NetworkHeader header(/*to node*/ house_node);
        network.write(header, &data, sizeof(data));
        delay(20);
        // Indicate to the other node that we are no longer transmiting.
        digitalWrite(node2_busy, LOW);

        // Return to original state
        u8state = 0;
    }
}
