# Raspberry Pi Pico Sensor Stream (CircuitPython)

This folder contains the Pico firmware that streams live sensor and input data over USB serial to the Raspberry Pi 4 main application.

## Version

- Current version: 1.0.0
- Version file: VERSION
- Release tag: rpi_pico_code-v1.0.0

## Purpose

- Pico/Pico W reads connected sensors and inputs.
- Pico sends text-based data lines over USB CDC serial.
- Raspberry Pi main app reads those lines through /dev/ttyACM0 and forwards them to LED modes.

## Files

- code.py: stable main script for streaming commands and sensor data.
- cade_wip.py: variant with START_OF_LOG marker support for bulk log captures.

## Hardware / Pins used

- Optical sensor (LTR381 RGB/ALS) on I2C:
	- SCL: GP15
	- SDA: GP14
- Temperature/Humidity sensor (SHT31D) on I2C:
	- SCL: GP21
	- SDA: GP20
- Microphone (PDMIn):
	- CLK: GP4
	- DAT: GP5
- Rotary encoder (left):
	- A/B: GP0, GP1
- Rotary encoder (right):
	- A/B: GP12, GP13
- Button:
	- GP26 (pull-up)

## Install CircuitPython on Pico

Follow Adafruit guide:

https://learn.adafruit.com/getting-started-with-raspberry-pi-pico-circuitpython/circuitpython

## Required libraries on CIRCUITPY/lib

Minimum libraries used by this code:

- adafruit_sht31d.mpy (or adafruit_sht31d.py)
- ltr381rgb.py

Built-in modules used (no copy needed): busio, digitalio, rotaryio, audiobusio, usb_cdc.

Bundle source:

https://circuitpython.org/libraries

## Deploy

1. Connect Pico over USB and open CIRCUITPY drive.
2. Create lib folder if missing.
3. Copy required libraries into lib.
4. Copy code.py (or cade_wip.py renamed to code.py) to CIRCUITPY root.
5. Pico auto-restarts and begins waiting for serial commands.

## USB Serial Command Protocol

Commands are whitespace-separated and case-insensitive after uppercasing.

Control commands:

- STREAM: start periodic output for selected sensors.
- STOP: stop streaming and clear selected commands.
- RESET: stop streaming, clear commands, zero rotary offsets to current position.
- GET_ALL_SENSORS: one-shot read of all sensors.

Sensor selection commands:

- OPTICAL
- TEMPERATURE
- MICROPHONE
- ROTARY
- RIGHT_ENCODER
- BUTTON

Typical combined command examples:

- RESET ROTARY STREAM
- RESET ROTARY RIGHT_ENCODER STREAM
- RESET OPTICAL TEMPERATURE MICROPHONE ROTARY STREAM
- GET_ALL_SENSORS

## Output Format sent to Raspberry Pi

Examples:

- ALS: <lux> lx
- Temperature: <celsius> C
- Humidity: <percent> %
- Sound Magnitude: <value>
- Rotary Encoder Position: <value>
- Rotary Encoder Right Position: <value>
- Button is pressed
- Button is released

For GET_ALL_SENSORS:

- code.py appends: <END_OF_DATA>
- cade_wip.py appends: <START_OF_LOG> ... <END_OF_DATA>

The Raspberry Pi parser in main_application uses these markers for log capture windows.

## Integration notes for main app

- Keep Pico connected before boot so it enumerates as /dev/ttyACM0.
- Main app sends commands over /tmp/uart_fifo to the UART bridge process.
- UART bridge forwards Pico lines to /tmp/mode_fifo for active LED modes.
- If Pico appears on /dev/ttyACM1, update UART device path in main_application.

## Troubleshooting

- No serial data: confirm CIRCUITPY mounted and code.py is present.
- Import errors: missing libraries in CIRCUITPY/lib.
- No sensor values: check wiring and power to each sensor.
- Logging not filling data_logs: use marker-enabled variant (cade_wip.py renamed to code.py) or add START_OF_LOG marker to code.py.
