/*
	Cole L - 9th December 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor

	This is an example to retrieve data from a single charge controller. All this program does is print the data to the console.
	This should give you enough information on how to read the values so you can do more exciting stuff with the data.
	I would suggest looking into setting up an MQTT server so you can view the data on other devices.

	See images and schematic for wiring details.
*/

#include <ModbusRtu.h>		// Modbus for talking to charge controller.
#include <SoftwareSerial.h> // Software serial for modbus.
#include <math.h>			// Used for pow() when deceiphering fault data.

/*
	Pins to use for software serial for talking to the charge controller through the MAX3232.
*/
#define MAX3232_RX 12 // RX pin. D6 -> MAX3232 RX.
#define MAX3232_TX 14 // TX pin. D5 -> MAX3232 TX.

/*
	I had issues with the transmitter's Serial buffers running out of room when receiving more than 29 registers at once
	from the charge controller. Solved this by just sending two separate requests to get all the data I wanted.
*/
#define NUM_REGISTERS_REQUEST1 24
#define NUM_REGISTERS_REQUEST2 11

/*
	Modbus Constants
*/
#define MODBUS_SLAVE_ADDR 255
#define MODBUS_READ_CODE 3
#define MODBUS_REQUEST1_START_ADDR 256
#define MODBUS_REQUEST2_START_ADDR 280
#define MODBUS_MASTER_TIMEOUT 2000

/*
	Other settings.
*/
#define REQUEST_DELAY 2000		// Delay in ms between requests to the charge controller over modbus.
#define SETUP_FINISH_DELAY 2000 // Delay after finishing setup.

/*
	Array of charging mode strings.
	These are the states the charge controller can be in when charging the battery.
*/
const char *chargeModes[7] = {
	"OFF",			   // 0
	"NORMAL",		   // 1
	"MPPT",			   // 2
	"EQUALIZE",		   // 3
	"BOOST",		   // 4
	"FLOAT",		   // 5
	"CURRENT_LIMITING" // 6
};

/*
	Array of fault codes.
	These are all the possible faults the charge controller can indicate.
*/
const char *faultCodes[15] = {
	"Charge MOS short circuit",		 // (16384 | 01000000 00000000)
	"Anti-reverse MOS short",		 // (8192  | 00100000 00000000)
	"PV panel reversely connected",	 // (4096  | 00010000 00000000)
	"PV working point over voltage", // (2048  | 00001000 00000000)
	"PV counter current",			 // (1024  | 00000100 00000000)
	"PV input side over-voltage",	 // (512   | 00000010 00000000)
	"PV input side short circuit",	 // (256   | 00000001 00000000)
	"PV input overpower",			 // (128   | 00000000 10000000)
	"Ambient temp too high",		 // (64    | 00000000 01000000)
	"Controller temp too high",		 // (32    | 00000000 00100000)
	"Load over-power/current",		 // (16    | 00000000 00010000)
	"Load short circuit",			 // (8     | 00000000 00001000)
	"Battery undervoltage warning",	 // (4     | 00000000 00000100)
	"Battery overvoltage",			 // (2     | 00000000 00000010)
	"Battery over-discharge"		 // (1     | 00000000 00000001)
};

/*
	Describes the different states the program can be in.
*/
enum STATE
{
	WAIT_REQUEST1 = 0,
	QUERY_REQUEST1 = 1,
	POLL_REQUEST1 = 2,
	WAIT_REQUEST2 = 3,
	QUERY_REQUEST2 = 4,
	POLL_REQUEST2 = 5,
	TRANSMIT = 6
};
STATE state;

unsigned long lastTime; // Last time isTime() was run and returned 1.

uint16_t lastErrCnt; // Used to keep tract of modbus errors so we can compare to see how many new errors were generated.

// Create software serial for communicating with Charge Controller.
SoftwareSerial mySerial(MAX3232_RX, MAX3232_TX);

// Create modbus device
Modbus master(0, mySerial);
modbus_t telegram[2]; // Will be sending two requests.

/*
	This is the structure which is transmitted from the charge controller to the receiver.
*/
typedef struct package
{
	uint16_t modbusErrDiff;					   // Modbus Error Difference.
	uint16_t request1[NUM_REGISTERS_REQUEST1]; // First group of registers from the charge controller.
	uint16_t request2[NUM_REGISTERS_REQUEST2]; // Second group of registers from the charge controller.
} Package;
Package chargeControllerNodeData;

/*
	Array of pointers to make it easier to retrieve charge controller data.
*/
uint16_t *chargeControllerRegisterData[NUM_REGISTERS_REQUEST1 + NUM_REGISTERS_REQUEST2];

void setup()
{
	Serial.begin(115200);

	// Start the software serial for the modbus connection.
	mySerial.begin(9600);

	// Prepare the commands we will use to request data from the charge controller.
	telegram[0].u8id = telegram[1].u8id = MODBUS_SLAVE_ADDR;
	telegram[0].u8fct = telegram[1].u8fct = MODBUS_READ_CODE;
	telegram[0].u16RegAdd = MODBUS_REQUEST1_START_ADDR;
	telegram[0].u16CoilsNo = NUM_REGISTERS_REQUEST1;
	telegram[0].au16reg = chargeControllerNodeData.request1;

	telegram[1].u16RegAdd = MODBUS_REQUEST2_START_ADDR;
	telegram[1].u16CoilsNo = NUM_REGISTERS_REQUEST2;
	telegram[1].au16reg = chargeControllerNodeData.request2;

	// Start modbus.
	master.start();
	master.setTimeOut(MODBUS_MASTER_TIMEOUT);
	lastTime = millis();
	lastErrCnt = 0;
	state = WAIT_REQUEST1;

	// Make an array of pointers to make it easier to access the charge controller data.
	// Initialize pointers array for request1 registers.
	for (int i = 0; i < NUM_REGISTERS_REQUEST1; i++)
	{
		chargeControllerRegisterData[i] = &chargeControllerNodeData.request1[i];
	}
	// Initialize pointers array for request2 registers.
	for (int i = 0; i < NUM_REGISTERS_REQUEST2; i++)
	{
		chargeControllerRegisterData[i + NUM_REGISTERS_REQUEST1] = &chargeControllerNodeData.request2[i];
	}

	delay(SETUP_FINISH_DELAY);
}

/*
	millis() Rollover safe time delay tracking.
*/
uint8_t isTime()
{
	if (millis() - lastTime >= REQUEST_DELAY)
	{
		lastTime = millis();
		return 1;
	}
	return 0;
}

/*
	Helper function to handle the sign bit for negative temperatures.
*/
int getRealTemp(int temp)
{
	return temp / 128 ? -(temp % 128) : temp;
}

/*
	Print all the charge controller data to the serial monitor.
*/
void printAllData()
{
	Serial.println(F("------------- Real Time Data -------------"));

	// Print the charging mode of the charge controller:
	int loadOffset = *chargeControllerRegisterData[32] > 6 ? 32768 : 0;
	Serial.print(F("Charging Mode:\t\t\t"));
	Serial.println(chargeModes[*chargeControllerRegisterData[32] - loadOffset]);

	// Print the battery state of charge:
	Serial.print(F("Battery SOC:\t\t\t"));
	Serial.print(*chargeControllerRegisterData[0]);
	Serial.println(F("%"));

	// Print the battery voltage:
	Serial.print(F("Battery Voltage:\t\t"));
	Serial.print((*chargeControllerRegisterData[1] * 0.1));
	Serial.println(F("V"));

	// Print the battery charge current:
	Serial.print(F("Battery Charge Current:\t\t"));
	Serial.print((*chargeControllerRegisterData[2] * 0.01));
	Serial.println(F("A"));

	// Print the controller temperature:
	Serial.print(F("Controller Temperature:\t\t"));
	Serial.print(getRealTemp(*chargeControllerRegisterData[3] >> 8));
	Serial.println(F("*C"));

	// Print the battery temperature:
	Serial.print(F("Battery Temperature:\t\t"));
	Serial.print(getRealTemp(*chargeControllerRegisterData[3] & 0xFF));
	Serial.println(F("*C"));

	// Print the load voltage:
	Serial.print(F("Load Voltage:\t\t\t"));
	Serial.print((*chargeControllerRegisterData[4] * 0.1));
	Serial.println(F("V"));

	// Print the load current:
	Serial.print(F("Load Current:\t\t\t"));
	Serial.print((*chargeControllerRegisterData[5] * 0.01));
	Serial.println(F("A"));

	// Print the load power:
	Serial.print(F("Load Power:\t\t\t"));
	Serial.print(*chargeControllerRegisterData[6]);
	Serial.println(F("W"));

	// Print the panel volts:
	Serial.print(F("Panel Volts:\t\t\t"));
	Serial.print((*chargeControllerRegisterData[7] * 0.1));
	Serial.println(F("V"));

	// Print the panel amps:
	Serial.print(F("Panel Amps:\t\t\t"));
	Serial.print((*chargeControllerRegisterData[8] * 0.01));
	Serial.println(F("A"));

	// Print the panel power:
	Serial.print(F("Panel Power: \t\t\t"));
	Serial.print(*chargeControllerRegisterData[9]);
	Serial.println(F("W"));

	Serial.println("--------------- DAILY DATA ---------------");

	// Print the daily battery minimum voltage:
	Serial.print(F("Battery Minimum Voltage:\t"));
	Serial.print((*chargeControllerRegisterData[11] * 0.1));
	Serial.println(F("V"));

	// Print the daily battery minimum voltage:
	Serial.print(F("Battery Maximum Voltage:\t"));
	Serial.print((*chargeControllerRegisterData[12] * 0.1));
	Serial.println(F("V"));

	// Print the daily maximum charge current:
	Serial.print(F("Maximum Charge Current:\t\t"));
	Serial.print((*chargeControllerRegisterData[13] * 0.01));
	Serial.println(F("A"));

	// Print the daily maximum load current:
	Serial.print(F("Maximum Load Discharge Current:\t"));
	Serial.print((*chargeControllerRegisterData[14] * 0.01));
	Serial.println(F("A"));

	// Print the daily maximum charge power:
	Serial.print(F("Maximum Charge Power:\t\t"));
	Serial.print(*chargeControllerRegisterData[15]);
	Serial.println(F("W"));

	// Print the daily maximum load power:
	Serial.print(F("Maximum Load Discharge Power:\t"));
	Serial.print(*chargeControllerRegisterData[16]);
	Serial.println(F("W"));

	// Print the daily charge amp hours:
	Serial.print(F("Charge Amp Hours:\t\t"));
	Serial.print(*chargeControllerRegisterData[17]);
	Serial.println(F("Ah"));

	// Print the daily load amp hours:
	Serial.print(F("Load Amp Hours:\t\t\t"));
	Serial.print(*chargeControllerRegisterData[18]);
	Serial.println(F("Ah"));

	// Print the daily charge power:
	Serial.print(F("Charge Power:\t\t\t"));
	Serial.print((*chargeControllerRegisterData[19] * 0.001));
	Serial.println(F("KWh"));

	// Print the daily load power:
	Serial.print(F("Load Power:\t\t\t"));
	Serial.print((*chargeControllerRegisterData[20] * 0.001));
	Serial.println(F("KWh"));

	Serial.println(F("------------- LIFETIME DATA --------------"));

	// Print the number of days operational:
	Serial.print(F("Days Operational:\t\t"));
	Serial.print(*chargeControllerRegisterData[21]);
	Serial.println(F("Days"));

	// Print the number of times batteries were over discharged:
	Serial.print(F("Times Over Discharged:\t\t"));
	Serial.println(*chargeControllerRegisterData[22]);

	// Print the number of times batteries were fully charges:
	Serial.print(F("Times Fully Charged:\t\t"));
	Serial.println(*chargeControllerRegisterData[23]);

	// Print the cumulative amp hours:
	Serial.print(F("Cumulative Amp Hours:\t\t"));
	Serial.print(((*chargeControllerRegisterData[24] * 65536 + *chargeControllerRegisterData[25]) * 0.001));
	Serial.println(F("KAh"));

	// Print the cumulative load amp hours:
	Serial.print(F("Load Amp Hours:\t\t\t"));
	Serial.print(((*chargeControllerRegisterData[26] * 65536 + *chargeControllerRegisterData[27]) * 0.001));
	Serial.println(F("KAh"));

	// Print the cumulative power hours:
	Serial.print(F("Cumulative Power:\t\t"));
	Serial.print(((*chargeControllerRegisterData[28] * 65536 + *chargeControllerRegisterData[29]) * 0.001));
	Serial.println(F("KWh"));

	// Print the cumulative load amp hours:
	Serial.print(F("Load Power:\t\t\t"));
	Serial.print(((*chargeControllerRegisterData[30] * 65536 + *chargeControllerRegisterData[31]) * 0.001));
	Serial.println(F("KWh"));

	Serial.println(F("--------------- FAULT DATA ---------------"));

	// Determine if there are any faults / what they mean.
	String faults = "- None :)";
	int faultID = *chargeControllerRegisterData[34];
	if (faultID != 0)
	{
		if (faultID > 16384 || faultID < 0)
		{
			faults = "- Invalid fault code!";
		}
		else
		{
			faults = "";
			int count = 0;
			int faultCount = 0;
			while (faultID != 0)
			{
				if (faultID >= pow(2, 15 - count))
				{
					// If there is more than one error, make a new line.
					if (faultCount > 0)
					{
						faults += "\n";
					}
					faults += "- ";
					faults += faultCodes[count - 1];
					faultID -= pow(2, 15 - count);
					faultCount++;
				}
				count++;
			}
		}
	}
	// Print the faults
	Serial.println(F("Faults:"));
	Serial.println(faults);

	// Print the raw fault data
	Serial.print(F("RAW Fault Data:\t\t\t"));
	Serial.println(*chargeControllerRegisterData[34]);
	Serial.println(F("------------------------------------------\n"));
}

void loop()
{
	if (state == WAIT_REQUEST1)
	{
		if (isTime())
		{
			state = QUERY_REQUEST1;
		}
	}
	else if (state == QUERY_REQUEST1)
	{
		master.query(telegram[0]);
		state = POLL_REQUEST1;
	}
	else if (state == POLL_REQUEST1)
	{
		master.poll();
		if (master.getState() == COM_IDLE)
		{
			state = WAIT_REQUEST2;
		}
	}
	else if (state == WAIT_REQUEST2)
	{
		if (isTime())
		{
			state = QUERY_REQUEST2;
		}
	}
	else if (state == QUERY_REQUEST2)
	{
		master.query(telegram[1]);
		state = POLL_REQUEST2;
	}
	else if (state == POLL_REQUEST2)
	{
		master.poll();
		if (master.getState() == COM_IDLE)
		{
			state = TRANSMIT;
		}
	}
	else if (state == TRANSMIT)
	{
		// Number of modbus errors we encountered this time.
		chargeControllerNodeData.modbusErrDiff = (uint16_t)(master.getErrCnt() - lastErrCnt);
		lastErrCnt = master.getErrCnt();

		// Indicate if there was a problem talking to the charge controller.
		if (chargeControllerNodeData.modbusErrDiff > 0)
		{
			Serial.println(F("Error when trying to communicate with the charge controller."));
		}

		// As an example, print out all the data.
		printAllData();

		// Return to original state.
		state = WAIT_REQUEST1;
	}
}
