# syncenlight

Software for two (or more) lamps whose colors can be changed by touch and are synchronized with each other over the internet using a MQTT server. Based on [this cool project](https://www.deutschlandfunknova.de/beitrag/netzbasteln-dosentelefon-mit-licht) of the german radio show Netzbasteln. If using a Wemos D1 mini this is how things should be wired for this software to work:

* D2: resistor for capacitive sensor
* D5: resistor for capacitive sensor, wire that goes to metal surface for the sensor
* D8: WS2812B signal
* GND: VSS of WS2812B
* 5V: VDD of WS2812B

A more detailed instruction on how to build the hardware for this can be found [in our Instructable](https://www.instructables.com/id/Color-Synchronized-Touch-Lamps/). Of course something similar can be done using other microcontrollers or hardware but then you have to modify the software accordingly :-)

Here is a video of how it looks like:

[![Alt text](https://img.youtube.com/vi/A-f9uCuspBQ/0.jpg)](https://www.youtube.com/watch?v=A-f9uCuspBQ)
