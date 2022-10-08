/*
	Cole Lightfoot - 30th September 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
	This is intended to run on an arduino nano. Mine uses the old bootloader.
	Tested on both SRNE ML4860 and ML2440.

	This arduino connects to the ML2440 Charge Controller (CC2)

	Attached components are:
	MAX3232 Breakout board: https://www.digikey.ca/short/08jnnm82
	RF24 module
	Capacitors on the 3.3V and 5V power rails to prevent voltage drops.
	4.7K resistors between CC1 arduino and CC2 arduino on digital pins 6 and 7.
*/

#include <RF24.h>
#include <RF24Network.h>    // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <ModbusRtu.h>      // Modbus for talking to charge controller
#include <SoftwareSerial.h>

/*
    I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once 
    from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
	Digital pins used to prevent both nodes from transmitting at the same time.
*/
#define NODE1_BUSY 6
#define NODE2_BUSY 7

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
#define RECEIVE_NODE_ADDR 0
#define THIS_NODE_ADDR 2
#define RF24_NETWORK_CHANNEL 90   // Channel the RF24 network should use.
#define REQUEST_DELAY 2000

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

// This is the struct which is transmitted to the receiver and contains all the data.
typedef struct package{
	uint16_t modbusErrDiff;
    uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller
    uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller
}Package;
Package data;

unsigned long lastTime; // Last time isTime() was run and returned 1.
uint16_t lastErrCnt;
int cnt;

// Create the RF24 radio and network
RF24 radio(9, 10);  // CE, CSN
RF24Network network(radio);

// Create software serial for communicating with Charge Controller.
SoftwareSerial mySerial(2, 3);

// Create modbus device
Modbus master(0, mySerial);
modbus_t telegram[2]; // 2 because we make 2 requests.

void setup(){
    Serial.begin(115200);

    // Start the software serial for the modbus connection
    mySerial.begin(9600);

    // Initialize the pins which are used to talk to the other transmitter to make sure they never transmit at the same time.
    pinMode(NODE1_BUSY, INPUT);
    pinMode(NODE2_BUSY, OUTPUT);
    digitalWrite(NODE2_BUSY, LOW);

    // Initialize the RF24 radio  
    while(!radio.begin()){
        Serial.print(F("RF24 Fail"));
    }
    Serial.print(F("RF24 Start"));
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
	cnt = 0;
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
        while(digitalRead(NODE1_BUSY) == HIGH){
            Serial.println(F("Busy."));
            delay(20);
        }

        // Indicate to the other node that we are transmitting.
        digitalWrite(NODE2_BUSY, HIGH);
        delay(20);
        // Send the data to the receiver.
        RF24NetworkHeader header(RECEIVE_NODE_ADDR);
        network.write(header, &data, sizeof(data));
        delay(20);
        // Indicate to the other node that we are no longer transmitting.
        digitalWrite(NODE2_BUSY, LOW);

        // Return to original state
        state = WAIT_REQUEST1;
    }
}
