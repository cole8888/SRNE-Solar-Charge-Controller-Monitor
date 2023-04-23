# SRNE-Solar-Charge-Controller-Monitor
Read data from SRNE solar charge controllers via modbus over RS232.

This repository contains three setups for reading data from SRNE solar charge controllers. One for a raspberry pi, ESP32 another for other Arduino boards.

I'd recommend going with the ESP32 version.

### [Raspberry Pi Version](./RaspberryPi-Version/)
All the raspberry pi version does is fetch the data from the controller and print it to screen. If you encounter any issues with it let me know, I have not been really been able to test it recently since I've switched to the arduino version.

### [ESP32 Version (v2.1)](./ESP32-Version/)
This is the version I actively use. I strongly recommend using this version over the Arduino version as it should be much easier to implement and work with since it does not need RF24 modules or a receiver. It supports up to 6 charge controllers, and I've included my custom PCB design for it.
There is a more detailed README in that directory.

![PCB](./ESP32-Version/PCB%20and%20Schematic/ESP32-Assembled-PCB.jpg)

### [Arduino Version (v1.1)](./Arduino-Version/)
This is the version I used before making the ESP32 version. It supports Arduino Nano and ESP8266 boards. 
There is a much more detailed README file in that directory.
I also designed a custom PCB for this one:

![PCB](./Arduino-Version/My-Current-Seup/PCB%20and%20Schematic/Assembled-PCB.jpg)

### Troubleshooting
If you notice lots of timeout errors, or other random errors, try some these steps before creating an issue:
- Try a different power supply and USB cable. My ML2440 does not respond on SoftwareSerial when I use one of my USB power bricks, but it does with another power brick.
- Try increasing the timeout value. My ML2440 was missing the default timeout occasionally.
- Try using HardwareSerial instead of SoftwareSerial. I've found HardwareSerial is more reliable, configure the program to use it if you are having problems.
  - The ESP32 can only have 2 (maybe 3 if you are brave) controllers hooked up to HardwareSerial, so if you have less than 3 controllers, you can use HardwareSerial for all of them, but to use all 6 ports you will need some to use SoftwareSerial for at least four of them.
- Try using a shorter cables and moving them away from interference.
- Verify you are using straight cables and not ones that reverse from one end to the other.

### Other notes
I've included the modbus manual in this repo if you want to have a look at it. My examples read all the registers I think are important, but there are lots of other registers which I haven't touched. Be careful, some of them can be written to!

Python version was originally based on: http://vcsco.com/uncategorized/renogy-rover-monitoring-with-the-raspberry-pi/

That guide also has a good illustration on how to hook up the raspberry pi to the MAX3232 module.

If you need help with something feel free to create an issue and I'll see what I can do.
