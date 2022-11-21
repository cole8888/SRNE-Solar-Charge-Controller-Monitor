# SRNE-Solar-Charge-Controller-Monitor
Read data from SRNE solar charge controllers via modbus over RS232.

This repository contains two setups for reading data from SRNE solar charge controllers. One for a raspberry pi and another based on an arduino network.

### Raspberry Pi Version
Please note that all the raspberry pi version does is fetch the data from the controller and print it to screen. If you encounter any issues with it let me know, I have not been really been able to test it recently since I've switched to the arduino version.

### Arduino Version
This is the version I actively use and have spent the most time on.
I have included some examples to make getting started easier, I've also included my current setup for reference.
There is a much more detailed README file in that directory.
I also designed a custom PCB (PCB files are included):
![PCB](./Arduino-Version/My-Current-Seup/PCB%20and%20Schematic/Assembled-PCB.jpg)

### Other notes
I've included the modbus manual in this repo if you want to have a look at it. My examples read all the registers I think are important, but there are lots of other registers which I haven't touched. Be careful, some of them can be written to!

Please note I have not had many real world occasions to test the fault reporting so it may not work as intended in all cases.

Python version was originally based on: http://vcsco.com/uncategorized/renogy-rover-monitoring-with-the-raspberry-pi/

That guide also has a good illustration on how to hook up the raspberry pi to the MAX3232.

If you need help with something feel free to create an issue and I'll see what I can do.
