# SRNE-Solar-Charge-Controller-Monitor
Read data from SRNE solar charge controllers via modbus over RS232.

Work in progress, but the basic functionality works.

I use two charge controllers for my solar system. One is for the front of the house (ML4860, aka: CC1) and the other is for the back (ML2440, aka: CC2).

There are three arduinos, one talks to CC1, one talks to CC2 and the other receives data from both transmitters and publishes it to an MQTT server and displays some of it on an LCD. The CC1 arduino also has some other sensors hooked up to it such as a BME680, ADS1115 and various current sensors. Each transmitter arduino has an RF24 module and a MAX3232 breakout module while the receiver has an RF24 module and a 20x4 character I2C LCD. The transmitter arduinos are arduino nanos and the receiver is a NodeMCU ESP8266.

The two transmitter arduinos will signal to each other via digital pins 6 and 7 to indicate whether or not they are transmitting data and if the other one is transmitting it will wait to avoid interference.

For some reason the ML4860 controller occasionally stops responding to queries for a few days, but the issue is not related to this project and the controller probably has something wrong with it. The ML2440 on the other hand has been reporting data flawlessly for over a year.

I originally started with the the python version because I wasn't able to get the arduinos to talk to the controllers. I eventually got them to talk and have been using them ever since. It is much more reliable and easier to manage than a raspberry pi so I would recommend using it instead if you feel comfortable. I use the python version once and a while to help diagnose issues.

I've included the modbus manual in this repo if you want to have a look at it. The code is already able to read data from all the registers I consider important, but there are lots of other registers which I haven't touched. Be careful, some of them can be written to!

Please note I have not had many real world occasions to test the fault reporting so it may not work as intended.

Python version was originally based on: http://vcsco.com/uncategorized/renogy-rover-monitoring-with-the-raspberry-pi/

WayBack Machine link of that site incase it dies: https://web.archive.org/web/20220511044309/http://vcsco.com/uncategorized/renogy-rover-monitoring-with-the-raspberry-pi/

That guide also has a good illustration on how to hook up the raspberry pi to the MAX3232.