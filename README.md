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
![GitHub Actions](https://img.shields.io/badge/workflow-build--main--application.yml-2088FF?style=for-the-badge&logo=githubactions&logoColor=white)
![GitHub Actions](https://img.shields.io/badge/workflow-sync--upstream--fork.yml-2088FF?style=for-the-badge&logo=githubactions&logoColor=white)

Multi-component Raspberry Pi + Pico project for sensor-driven LED matrix visuals.

- Raspberry Pi 4 runs the multi-mode LED renderer and process orchestration.
- Raspberry Pi Pico/Pico W streams live sensor and input data over USB serial.
- Modes include games, visualizations, weather/NASA panels, and thermal display.

Demo: https://youtu.be/y5JOEidExKE

## Repository layout

- [main_application](main_application/README.md): primary runtime, process orchestration, mode binaries.
- [rpi_pico_code](rpi_pico_code/README.md): CircuitPython firmware for USB sensor stream.
- [rpi-rgb-led-matrix](rpi-rgb-led-matrix/README.md): included LED matrix driver library source.

## GitHub workflows

- [build-main-application.yml](.github/workflows/build-main-application.yml): GitHub-hosted build runner that installs dependencies, installs `libgpiod` v2.2.1 from source, clones `rpi-rgb-led-matrix`, and runs `main_application/build_all.sh`.
- [sync-upstream-fork.yml](.github/workflows/sync-upstream-fork.yml): fork sync workflow that can run on manual dispatch, schedule, and push updates to `main`.

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
sudo apt install -y g++ make libsqlite3-dev libcurl4-openssl-dev graphicsmagick libgraphicsmagick++-dev python3-pip
pip3 install requests buienradar
```

1b) Install `libgpiod` v2 (required by `master_script`):

Check installed version:

```bash
pkg-config --modversion libgpiod
```

If version is older than `2.x`, install from source:

```bash
curl -fsSL https://mirrors.edge.kernel.org/pub/software/libs/libgpiod/libgpiod-2.2.1.tar.xz -o /tmp/libgpiod-2.2.1.tar.xz
cd /tmp
tar -xf libgpiod-2.2.1.tar.xz
cd libgpiod-2.2.1
./configure --prefix=/usr/local
make -j"$(nproc)"
sudo make install
sudo ldconfig
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


## Troubleshooting

- Pico not found on /dev/ttyACM0: check if it enumerated as /dev/ttyACM1 and update UART path in main app.
- Modes switch but show no input: verify buffer process binary exists as main_application/buffer_process.
- Build errors around matrix libs: confirm RGBMATRIX_DIR points to valid rpi-rgb-led-matrix checkout.
- Weather/NASA modes fail: re-check Python/network dependencies and API reachability. NASA disabled space photos...

## License

This repository includes a top-level project license:

- [LICENSE](LICENSE)

It also contains third-party library code under its own license files (for example in rpi-rgb-led-matrix). Review those directories before redistribution.

