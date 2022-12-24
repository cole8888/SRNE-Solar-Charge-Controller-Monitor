/*
    Cole L - 24th December 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
    
    This is an example to retrieve data from multiple charge controllers.
	This is intended to be run on an Arduino nano, it will communicate with one charge controller and send the data to the receiver.
    
    See images and schematic for wiring details.
*/

#include <RF24.h>
#include <RF24Network.h>    // Easiest way I could find to transfer >32Bytes at once on RF24.
#include <ModbusRtu.h>      // Modbus for talking to charge controller.
#include <SoftwareSerial.h> // Software serial for modbus.
#include <avr/wdt.h>        // Watchdog timer.

/*
	Pins to use for software serial for talking to the charge controller through the MAX3232.
*/
#define MAX3232_RX 2 // RX pin.
#define MAX3232_TX 3 // TX pin.

/*
    I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once 
    from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
    Modbus Constants
*/
/*
	All charge controllers will respond to address 255 no matter what their actual address is, this is useful if you do not know what address to use.
	The library I use will not work with address 255 initially and must be modified, see the README for details.
	You can try using 1 here instead of 255 if you don't want to make changes to the library, but it is not guaranteed to work.
*/
#define MODBUS_SLAVE_ADDR 255
#define MODBUS_READ_CODE 3
#define MODBUS_REQUEST1_START_ADDR 256
#define MODBUS_REQUEST2_START_ADDR 280
#define MODBUS_MASTER_TIMEOUT 2000

/*
    Other settings.
*/
#define RECEIVE_NODE_ADDR 0                    // Address of the receiver node.
#define THIS_NODE_ADDR 1                       // Address of this node.
#define RF24_NETWORK_CHANNEL 90                // Channel the RF24 network should use.
#define REQUEST_DELAY 2000                     // Delay in ms between requests to the charge controller over modbus.
#define SETUP_FAIL_DELAY 2000                  // Delay when retrying setup tasks.
#define SETUP_FINISH_DELAY 100*THIS_NODE_ADDR  // Delay after finishing setup. (Multiplied by address to to avoid interference on startup).

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

uint16_t lastErrCnt; // Used to keep tract of modbus errors so we can compare to see how many new errors were generated.

// Create the RF24 radio and network.
RF24 radio(9, 10); // CE, CSN
RF24Network network(radio);

// Create software serial for communicating with Charge Controller.
SoftwareSerial mySerial(MAX3232_RX, MAX3232_TX);

// Create modbus device
Modbus master(0, mySerial);
modbus_t telegram[2]; // Will be sending two requests.

/*
    This is the structure which is transmitted from the charge controller to the receiver.
*/
typedef struct package{
    uint16_t modbusErrDiff;                    // Modbus Error Difference.
    uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller.
    uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller.
}Package;
Package data;

void setup(){
    Serial.begin(115200);

    // Start the software serial for the modbus connection.
    mySerial.begin(9600);

    // Initialize the RF24 radio and network.
    while(!radio.begin()){
        Serial.print(F("RF24 Fail\n"));
        delay(SETUP_FAIL_DELAY);
    }
    Serial.print(F("RF24 Go\n"));
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

    // Start modbus.
    master.start();
    master.setTimeOut(MODBUS_MASTER_TIMEOUT);
    lastTime = millis();
    lastErrCnt = 0;
    state = WAIT_REQUEST1; 

    delay(SETUP_FINISH_DELAY);

    // Start the 4 second watchdog.
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
    else if(state == TRANSMIT){
        // Number of modbus errors we encountered this time.
        data.modbusErrDiff = (uint16_t) (master.getErrCnt() - lastErrCnt);
        lastErrCnt = master.getErrCnt();

        // // Print CC data (Troubleshooting).
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

        // Send the data to the receiver.
        delay(10);
        RF24NetworkHeader header(RECEIVE_NODE_ADDR);
        network.write(header, &data, sizeof(data));
        delay(10);

        // Return to original state.
        state = WAIT_REQUEST1;
    }
    // Reset watchdog.
    wdt_reset();
}
