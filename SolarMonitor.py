#!/usr/bin/python

import atexit
import time, datetime, serial, os
from gpiozero import CPUTemperature
from paho.mqtt import client as mqtt_client
from pymodbus.client.sync import ModbusSerialClient as ModbusClient

broker = '192.168.2.100'
port = 1883
topics=["piTemp",
		"CCchargeMode",
		"CCSOC",
		"CCbattVolts",
		"CCbattAmps",
		"CCcontrollerTemp",
		"CCbattTemp",
		"CCpanelVolts",
		"CCpanelAmps",
		"CCpanelWatts",
		"CCdailyPower",
		"CCtotalPower",
		"CCfaults"]
client_id = 'RPI3B'

def connect_mqtt():
	def on_connect(client, userdata, flags, rc):
		if rc == 0:
			print("Connected to MQTT Broker!")
		else:
			print("Failed to connect, return code %d\n", rc)
	# Set Connecting Client ID
	client = mqtt_client.Client(client_id)
	# client.username_pw_set(username, password)
	client.on_connect = on_connect
	client.connect(broker, port)
	return client

def publish(client, topic, msg):
		result = client.publish(topic, msg)
		if(result[0] != 0):
				return False
		return True

client = connect_mqtt()

# All possible controller faults
faultCodes=["Circuit, charge MOS short circuit",
			"Anti-reverse MOS short",
			"Solar panel reversely connected",
			"Solar panel working point over voltage",
			"Solar panel counter current",
			"Photovoltaic input side over-voltage",
			"Photovoltaic input side short circuit",
			"Photovoltaic input overpower",
			"Ambient temperature too high",
			"Controller temperature too high",
			"Load overpower / over-current",
			"Load short circuit",
			"Battery undervoltage warning",
			"Battery overvoltage",
			"Battery over-discharge"]

# Create ModbusClient instance and connect
# MAX3232 Breakout board connects to the raspberry pi serial pins and to the charge controller RS232 port.
modbus = ModbusClient(method='rtu', port='/dev/ttyS0', baudrate=9600, stopbits = 1, bytesize = 8, parity = 'N', timeout = 5, unit = 1) 
a = modbus.connect()

def recconnectModbus():
	global a
	global modbus
	modbus = ModbusClient(method='rtu', port='/dev/ttyS0', baudrate=9600, stopbits = 1, bytesize = 8, parity = 'N', timeout = 5, unit = 1) 
	a = modbus.connect()

# Retry connection to charge controller if it fails.
while(not a):
		print("Failed to connect to the Charge Controller, retrying in 5 seconds...")
		time.sleep(5)
		a = modbus.connect()

# When program existes, close the modbus connection.
def exit_handler():
		print("\nClosing Modbus Connection...")
		modbus.close()
atexit.register(exit_handler)

while 1:
	try:
		# Read 33 registers from the controller starting at address 0x0100 (Decimal 256) until 0x0122 (Decimal 290)
		r = modbus.read_holding_registers(256, 35, unit=1)

		# Offset to apply when determining the charging mode.
		# This only applies when the load is turned on since they share a register.
		modeOffset = 0

		# Is the charge controller load turned on?
		loadStatus = False
		if(r.registers[32] > 6):
			loadStatus = True
			modeOffset = 32768

		# Charging modes
		chargeMode = ""
		if(r.registers[32] == 0 + modeOffset):
				chargeMode = "OFF"
		elif(r.registers[32] == 1 + modeOffset):
				chargeMode = "NORMAL"
		elif(r.registers[32] == 2 + modeOffset):
				chargeMode = "MPPT"
		elif(r.registers[32] == 3 + modeOffset):
				chargeMode = "EQUALISE"
		elif(r.registers[32] == 4 + modeOffset):
				chargeMode = "BOOST"
		elif(r.registers[32] == 5 + modeOffset):
				chargeMode = "FLOAT"
		elif(r.registers[32] == 6 + modeOffset):
				chargeMode = "CURRENT LIMITING (OVERPOWER)"

		publish(client, topics[0], CPUTemperature().temperature)
		publish(client, topics[1], chargeMode)
		publish(client, topics[2], r.registers[0])
		publish(client, topics[3], round(float(r.registers[1]*0.1), 1))
		publish(client, topics[4], round(float(r.registers[2]*0.01), 2))
		publish(client, topics[5], int(hex(r.registers[3])[2:-2], 16))
		publish(client, topics[6], int(hex(r.registers[3])[-2:], 16))
		publish(client, topics[7], round(float(r.registers[7]*0.1), 1))
		publish(client, topics[8], round(float(r.registers[8]*0.01), 2))
		publish(client, topics[9], r.registers[9])
		publish(client, topics[10], round(float(r.registers[19]*0.001), 3))
		publish(client, topics[11], round(float((r.registers[28]*65536 + r.registers[29])*0.001), 3))

		# # Realtime information
		print("------------- Real Time Data -------------")
		print("Charging Mode:\t\t\t" + chargeMode)
		print("Battery SOC:\t\t\t" + str(r.registers[0]) + "%")
		print("Battery Voltage:\t\t" + str(round(float(r.registers[1]*0.1), 1)) + "V")
		print("Battery Charge Current:\t\t" + str(round(float(r.registers[2]*0.01), 2)) + "A")
		print("Controller Temperature:\t\t" + str(int(hex(r.registers[3])[2:-2], 16)) + "°C")
		print("Battery Temperature:\t\t" + str(int(hex(r.registers[3])[-2:], 16)) + "°C")
		print("Load Voltage:\t\t\t" + str(round(float(r.registers[4]*0.1), 1)) + " Volts")
		print("Load Current:\t\t\t" + str(round(float(r.registers[5]*0.01), 2)) + " Amps")
		print("Load Power:\t\t\t" + str(r.registers[6]) + " Watts")
		print("Load Enabled:\t\t\t" + str(loadStatus))
		print("Panel Volts:\t\t\t" + str(round(float(r.registers[7]*0.1), 1)) + "V")
		print("Panel Amps:\t\t\t" + str(round(float(r.registers[8]*0.01), 2)) + "A")
		print("Panel Power:\t\t\t" + str(r.registers[9]) + "W")

		# Data accumulated over the day
		print("--------------- DAILY DATA ---------------")
		print("Battery Minimum Voltage:\t" + str(round(float(r.registers[11]*0.1), 1)) + "V")
		print("Battery Maximum Voltage:\t" + str(round(float(r.registers[12]*0.1), 1)) + "V")
		print("Maximum Charge Current:\t\t" + str(round(float(r.registers[13]*0.01), 2)) + "A")
		print("Maximum Charge Power:\t\t" + str(r.registers[15]) + "W")
		print("Maximum Load Discharge Current:\t" + str(float(r.registers[14])*0.01) + "A")
		print("Maximum Load Discharge Power:\t" + str(r.registers[16]) + "W")
		print("Charge Amp Hours:\t\t" + str(r.registers[17]) + "Ah")
		print("Charge Power:\t\t\t" + str(round(float(r.registers[19]*0.001), 3)) + "KWh")
		print("Load Amp Hours:\t\t\t" + str(r.registers[18]) + "Ah")
		print("Load Power:\t\t\t" + str(round(float(r.registers[16]*0.001), 3)) + "KWh")

		# Data from the lifetime of the charge controller
		print("-------------- GLOBAL DATA ---------------")
		print("Days Operational:\t\t" + str(r.registers[21]) + " Days")
		print("Times Over Discharged:\t\t" + str(r.registers[22]))
		print("Times Fully Charged:\t\t" + str(r.registers[23]))
		print("Cummulative Amp Hours:\t\t" + str(round(float((r.registers[24]*65536 + r.registers[25])*0.001), 3)) + "KAh")
		print("Cummulative Power:\t\t" + str(round(float((r.registers[28]*65536 + r.registers[29])*0.001), 3)) + "KWh")
		print("Load Amp Hours:\t\t\t" + str(round(float((r.registers[26]*65536 + r.registers[27])*0.001), 3)) + "KAh")
		print("Load Power:\t\t\t" + str(round(float((r.registers[30]*65536 + r.registers[31])*0.001), 3)) + "KWh")

		# Check for controller faults and display them if there are any.
		print("----------------- FAULTS -----------------")
		faultID = r.registers[34]
		if(faultID == 0):
			print("None :)")
		else:
			faults = ""
			count = 0
			while(faultID != 0):
					if(faultID >= pow(2, 15-count)):
							faults += faultCodes[count-1]
							print("- " + faultCodes[count-1] + " (" + count + ")")
							faultID -= pow(2, 15-count)
					count += 1
			publish(client, topics[12], faults)
		print("\n------------------------------------------\n")
		time.sleep(3)
	except:
		print("Failed to read data, reconnecting...")
		recconnectModbus()
		time.sleep(3)
