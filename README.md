# Ambient AIoT LED Board

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Python](https://img.shields.io/badge/Python-3.x-3776AB?style=for-the-badge&logo=python&logoColor=white)
![CircuitPython](https://img.shields.io/badge/CircuitPython-Pico_W-7B2CBF?style=for-the-badge&logo=raspberrypi&logoColor=white)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-4%20%2F%20Pico%20W-C51A4A?style=for-the-badge&logo=raspberrypi&logoColor=white)
![rpi-rgb-led-matrix](https://img.shields.io/badge/rpi--rgb--led--matrix-Display%20Engine-111111?style=for-the-badge)
![SQLite](https://img.shields.io/badge/SQLite-Data%20Logging-003B57?style=for-the-badge&logo=sqlite&logoColor=white)
![libgpiod](https://img.shields.io/badge/libgpiod-GPIO-2E7D32?style=for-the-badge)
![GraphicsMagick](https://img.shields.io/badge/GraphicsMagick-Image%20Pipeline-5C2D91?style=for-the-badge)
![libcurl](https://img.shields.io/badge/libcurl-HTTP%20Fetch-0F4C81?style=for-the-badge)

Multi-component Raspberry Pi + Pico project for sensor-driven LED matrix visuals.

- Raspberry Pi 4 runs the multi-mode LED renderer and process orchestration.
- Raspberry Pi Pico/Pico W streams live sensor and input data over USB serial.
- Modes include games, visualizations, weather/NASA panels, and thermal display.

## Repository layout

- [main_application](main_application/README.md): primary runtime, process orchestration, mode binaries.
- [rpi_pico_code](rpi_pico_code/README.md): CircuitPython firmware for USB sensor stream.
- [grid-eye-demo](grid-eye-demo/README.md): AMG8833 thermal sensor Python demo.
- [rpi-rgb-led-matrix](rpi-rgb-led-matrix/README.md): included LED matrix driver library source.

## End-to-end architecture

1. Pico reads sensors and controls, then publishes text lines over USB CDC serial.
2. Pi runtime opens serial device (normally /dev/ttyACM0).
3. Buffer process forwards serial data into FIFO for active display mode.
4. Master process launches/switches modes and sends command profiles to Pico.
5. Active mode renders on HUB75 LED matrix via rpi-rgb-led-matrix.

Runtime FIFOs used by the Pi application:

- Command FIFO: /tmp/uart_fifo
- Data FIFO: /tmp/mode_fifo

Logs created by main runtime:

- [main_application/logs](main_application/logs/)
- [main_application/data_logs](main_application/data_logs/)

## Quick start (Pi runtime)

Environment target:

- Raspberry Pi 4
- Raspberry Pi OS Bookworm (or similar Debian-based distro)

1) Install OS packages:

```bash
sudo apt update
sudo apt install -y g++ make libgpiod-dev libsqlite3-dev libcurl4-openssl-dev graphicsmagick libgraphicsmagick++-dev python3-pip
pip3 install requests buienradar
```

2) Optional, for rotary-menu mode support:

```bash
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build
```

3) Build main binaries:

```bash
cd main_application
bash build_all.sh
```

If needed, set custom LED matrix library location before building:

```bash
export RGBMATRIX_DIR=/home/pi/Documents/rpi-rgb-led-matrix
```

4) Run orchestrator:

```bash
cd main_application
sudo ./master_script
```

Detailed build and mode notes: [main_application/README.md](main_application/README.md)

## Pico setup (sensor stream)

1. Flash CircuitPython to Pico/Pico W.
2. Copy required libraries to CIRCUITPY/lib.
3. Copy [rpi_pico_code/code.py](rpi_pico_code/code.py) to CIRCUITPY as code.py.
4. Keep Pico connected to Pi over USB before boot whenever possible.

Protocol details, commands, and pin map: [rpi_pico_code/README.md](rpi_pico_code/README.md)

## Main application highlights

- Entry point: [main_application/master_script.cpp](main_application/master_script.cpp)
- Buffer bridge source: [main_application/beffer_process_wip.cpp](main_application/beffer_process_wip.cpp)
- Build helper: [main_application/build_all.sh](main_application/build_all.sh)
- Database importer: [main_application/db_manager.cpp](main_application/db_manager.cpp)

Current runtime includes 20 mode executables configured in master_script.

## Thermal camera demo

Separate AMG8833 demo is available in [grid-eye-demo](grid-eye-demo/README.md), with:

- [grid-eye-demo/thermal_cam.py](grid-eye-demo/thermal_cam.py)
- [grid-eye-demo/driver.py](grid-eye-demo/driver.py)
- [grid-eye-demo/Seeed_AMG8833.py](grid-eye-demo/Seeed_AMG8833.py)

## Troubleshooting

- Pico not found on /dev/ttyACM0: check if it enumerated as /dev/ttyACM1 and update UART path in main app.
- Modes switch but show no input: verify buffer process binary exists as main_application/buffer_process.
- Build errors around matrix libs: confirm RGBMATRIX_DIR points to valid rpi-rgb-led-matrix checkout.
- Weather/NASA modes fail: re-check Python/network dependencies and API reachability.

## License

This repository includes a top-level project license:

- [LICENSE](LICENSE)

It also contains third-party library code under its own license files (for example in rpi-rgb-led-matrix). Review those directories before redistribution.

