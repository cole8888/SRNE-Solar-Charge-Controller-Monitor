#!/usr/bin/python

# Cole Lightfoot - 11th May 2022 - https://github.com/cole8888/SRNE-Solar-Charge-Controller-Monitor
#
# Used to gather data from SRNE charge controllers via modbus over RS232.
#
# Enable serial on the raspberry pi, I used this guide: https://pimylifeup.com/raspberry-pi-serial/
# You may need to switch the serial device depending on what Pi or other device you are using.
# 
# MAX3232 breakout board is connected to the raspberry pi serial headers and to the charge controller RS232.

import atexit
import time
from paho.mqtt import client as mqtt_client
from pymodbus.client.sync import ModbusSerialClient as ModbusClient

# Only if you want to use the cpu temperature printouts.
#from gpiozero import CPUTemperature

broker = '192.168.2.100'
port = 1883
topics=["CC1chargingMode",        #0
		"CC1SOC",                 #1
		"CC1battVolts",           #2
		"CC1battAmps",            #3
		"CC1controllerTemp",      #4
		"CC1battTemp",            #5
		"CC1panelVolts",          #6
		"CC1panelAmps",           #7
		"CC1panelWatts",          #8
		"CC1battMinVolts",        #9
		"CC1battMaxVolts",        #10
		"CC1battMaxChargeCurrent",#11
		"CC1panelMaxPower",       #12
		"CC1chargeAmpHours",      #13
		"CC1dailyPower",          #14
		"CC1numDays",             #15
		"CC1numOverCharges",      #16
		"CC1numFullCharges",      #17
		"CC1totalAmpHours",       #18
		"CC1totalPower",          #19
		"CC1faults"]	          #20

faultCodes = [
    "Charge MOS short circuit",     #0
    "Anti-reverse MOS short",       #1
    "PV panel reversely connected", #2
    "PV working point over voltage",#3
    "PV counter current",           #4
    "PV input side over-voltage",   #5
    "PV input side short circuit",  #6
    "PV input overpower",           #7
    "Ambient temp too high",        #8
    "Controller temp too high",     #9
    "Load over-power/current",      #10
    "Load short circuit",           #11
    "Battery undervoltage warning", #12
    "Battery overvoltage",          #13
    "Battery over-discharge"]		#14

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

# Create ModbusClient instance and connect
# MAX3232 Breakout board connects to the raspberry pi serial pins and to the charge controller RS232 port.
modbus = ModbusClient(method='rtu', port='/dev/ttyS0', baudrate=9600, stopbits = 1, bytesize = 8, parity = 'N', timeout = 5, unit = 1) 
a = modbus.connect()

# Used to reconnect on controller timeout.
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

# When program exits, close the modbus connection.
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
				chargeMode = "CURRENT_LIMITING"

		# Determine if there are any faults / what they mean.
		faults = "None :)"
		faultID = r.registers[34]
		if(faultID != 0):
			faults = ""
			count = 0
			while(faultID != 0):
				if(faultID >= pow(2, 15-count)):
					# If there is more than one error, make a new line.
					if(count > 0):
						faults += '\n'
					faults += '- ' + faultCodes[count-1]
					faultID -= pow(2, 15-count)
				count += 1
		
		# If you want to also see the temperature of your RPI cpu.
		# print("CPU Temp:\t", CPUTemperature().temperature)

		# Publish data to MQTT
		publish(client, topics[0], chargeMode)
		publish(client, topics[1], r.registers[0])										# Battery SOC
		publish(client, topics[2], r.registers[1]*0.1)									# Battery VOlts
		publish(client, topics[3], r.registers[2]*0.01)									# Charging Current
		publish(client, topics[4], int(hex(r.registers[3])[2:-2], 16))					# Controller Temp
		publish(client, topics[5], int(hex(r.registers[3])[-2:], 16))					# Battery Temp
		publish(client, topics[6], r.registers[7]*0.1)									# Panel Volts
		publish(client, topics[7], r.registers[8]*0.01)									# Panel Current
		publish(client, topics[8], r.registers[9])										# Panel Watts
		publish(client, topics[9], r.registers[11]*0.1)									# Battery Minimum Voltage
		publish(client, topics[10], r.registers[12]*0.1)								# Battery Maximum Voltage
		publish(client, topics[11], r.registers[13]*0.01)								# Max Charging Current
		publish(client, topics[12], r.registers[15])									# Max Charging Power
		publish(client, topics[13], r.registers[17])									# Daily Charge Amp Hours
		publish(client, topics[14], r.registers[19]*0.001)								# Daily Charge Power
		publish(client, topics[15], r.registers[21])									# Days Operational
		publish(client, topics[16], r.registers[22])									# Times Overcharged
		publish(client, topics[17], r.registers[23])									# Times Fully Charged
		publish(client, topics[18], (r.registers[24]*65536 + r.registers[25])*0.001)	# Total Amp Hours
		publish(client, topics[19], (r.registers[28]*65536 + r.registers[29])*0.001)	# Total Power
		publish(client, topics[20], r.registers[33])									# Fault Codes

		# Realtime information
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
		print("Cumulative Amp Hours:\t\t" + str(round(float((r.registers[24]*65536 + r.registers[25])*0.001), 3)) + "KAh")
		print("Cumulative Power:\t\t" + str(round(float((r.registers[28]*65536 + r.registers[29])*0.001), 3)) + "KWh")
		print("Load Amp Hours:\t\t\t" + str(round(float((r.registers[26]*65536 + r.registers[27])*0.001), 3)) + "KAh")
		print("Load Power:\t\t\t" + str(round(float((r.registers[30]*65536 + r.registers[31])*0.001), 3)) + "KWh")
		print("------------------------------------------")
		
		print("--------------- FAULT DATA ---------------")
		print(faults)
		print("RAW Fault Data:\t\t\t" + str(r.registers[34]))
		print("------------------------------------------")

		time.sleep(2)
	except:
		print("Failed to read data, reconnecting...", r)
		recconnectModbus()
		time.sleep(2)
