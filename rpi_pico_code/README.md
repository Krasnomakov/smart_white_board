This is a circuitpython code for raspberry pi pico/pico w. 

Flash Pi Pico to work with Circuitpython 
https://learn.adafruit.com/getting-started-with-raspberry-pi-pico-circuitpython/circuitpython

Make a lib directory on pi pico and copy there the following libraries into it: 
adafruit_framebuf.py  adafruit_register    font5x8.bin
adafruit_lsm6ds       adafruit_sht31d.py   ltr381rgb.py
adafruit_mmc56x3.py   adafruit_ssd1306.py  neopixel.py

Download from here: 
https://wiki.deskpi.com/attachments/libraries.zip

Or from here for your version of circuitpython (usually 9):
https://circuitpython.org/libraries

If there are issues with neopixel library from the bundle, use this file:
https://github.com/adafruit/Adafruit_CircuitPython_NeoPixel/blob/main/neopixel.py

Copy it manually. 

Copy code.py to pico.

Microcontroller should start working immediately after the file was copied. 
