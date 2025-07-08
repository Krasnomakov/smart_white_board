import Seeed_AMG8833
import os
import math
import time
import sys

import numpy as np
from scipy.interpolate import griddata

from colour import Color

#low range of the sensor (this will be blue on the screen)
MINTEMP = 26

#high range of the sensor (this will be red on the screen)
MAXTEMP = 31

#how many color values we can have
COLORDEPTH = 1024

#initialize the sensor
try:
    # Sensor initialization may fail if the I2C address is not the default.
    # The Seeed_AMG8833 library uses a default address.
    sensor = Seeed_AMG8833.AMG8833()
except Exception as e:
    # Exit if sensor initialization fails, writing the error to stderr.
    sys.stderr.write(f"Error: Could not initialize sensor. {e}\n")
    sys.exit(1)


points = [(math.floor(ix / 8), (ix % 8)) for ix in range(0, 64)]
grid_x, grid_y = np.mgrid[0:7:32j, 0:7:32j]

#the list of colors we can choose from
blue = Color("indigo")
colors = list(blue.range_to(Color("red"), COLORDEPTH))

#create the array of colors
colors = [(int(c.red * 255), int(c.green * 255), int(c.blue * 255)) for c in colors]

#some utility functions
def constrain(val, min_val, max_val):
    return min(max_val, max(min_val, val))

def map_value(x, in_min, in_max, out_min, out_max):
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min

#let the sensor initialize
time.sleep(.1)
	
while True:
    try:
        #read the pixels
        pixels = sensor.read_temp()
        pixels = [map_value(p, MINTEMP, MAXTEMP, 0, COLORDEPTH - 1) for p in pixels]
        
        #perform interpolation, fill missing values with 0
        bicubic = griddata(points, pixels, (grid_x, grid_y), method='cubic', fill_value=0)
        
        #output the data to stdout for the C++ application
        output_buffer = []
        for row in bicubic:
            for pixel in row:
                color_index = constrain(int(pixel), 0, COLORDEPTH - 1)
                color = colors[color_index]
                output_buffer.append(f"{color[0]} {color[1]} {color[2]}")
        
        sys.stdout.write(" ".join(output_buffer) + "\n")
        sys.stdout.flush()

        time.sleep(0.05) # limit frame rate to 20fps
    except (IOError, OSError):
        # This handles the case where the parent C++ process closes the pipe.
        break
    except Exception as e:
        sys.stderr.write(f"An error occurred: {e}\n")
        break 