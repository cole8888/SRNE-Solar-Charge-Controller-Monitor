# SRNE-Solar-Charge-Controller-Monitor
Read data from SRNE solar charge controllers via modbus over RS232.

This repository contains two setups for reading data from SRNE solar charge controllers. One for a raspberry pi and another based on an arduino network.

### Raspberry Pi Version
Please note that all the raspberry pi version does is fetch the data from the controller and print it to screen. If you encounter any issues with it let me know, I have not been really been able to test it recently since I've switched to only using the arduino version.

### Arduino Version
This is the version I actively use and have spent the most time on.
This project isn't really intended to be immediately plug and play. I suggest figuring out what your needs are and adjusting the code to meet them.
The transmitter code will not need to be tweaked very much, but the receiver is quite specific to my application.
For instance, you might only want to talk to one charge controller, in which case you can get rid of CC2 and CC3, or you might not care about environment data, so you can get rid of the misc data node and only use the charge controller nodes.

For my application I use three charge controllers. There are panels on the front of the house (CC1), side (CC2), and back (CC3). Each charge controller has an arduino nano attached to it and there is an additional arduino for reporting some miscellaneous environment readings and direct amp readings. The arduinos relay the information back to a receiver node using RF24 modules. See the attached schematic for more details. I've also included the PCB I'm using.

![PCB](/Arduino-Version/PCB and Schematic/PXL_20221116_032239785~2.jpg)

### Other notes
I've included the modbus manual in this repo if you want to have a look at it. Both versions to already read data from all the registers I consider important, but there are lots of other registers which I haven't touched. Be careful, some of them can be written to!

Please note I have not had many real world occasions to test the fault reporting so it may not work as intended in all cases.

Python version was originally based on: http://vcsco.com/uncategorized/renogy-rover-monitoring-with-the-raspberry-pi/

That guide also has a good illustration on how to hook up the raspberry pi to the MAX3232.

If you need help with something feel free to create an issue and I'll see what I can do.
