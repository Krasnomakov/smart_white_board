# LED Board Main Application

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Python](https://img.shields.io/badge/Python-3.x-3776AB?style=for-the-badge&logo=python&logoColor=white)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-4-C51A4A?style=for-the-badge&logo=raspberrypi&logoColor=white)
![rpi-rgb-led-matrix](https://img.shields.io/badge/rpi--rgb--led--matrix-Rendering-111111?style=for-the-badge)
![GPIO](https://img.shields.io/badge/libgpiod-GPIO%20Control-2E7D32?style=for-the-badge)
![WiringPi](https://img.shields.io/badge/WiringPi-Rotary%20Input-006064?style=for-the-badge)
![SQLite](https://img.shields.io/badge/SQLite-Logs%20%26%20Progress-003B57?style=for-the-badge&logo=sqlite&logoColor=white)
![GraphicsMagick](https://img.shields.io/badge/GraphicsMagick-Image%20Processing-5C2D91?style=for-the-badge)
![libcurl](https://img.shields.io/badge/libcurl-Weather%20%2F%20NASA%20Fetch-0F4C81?style=for-the-badge)

This folder contains the Raspberry Pi LED-matrix runtime, including:

- `master_script.cpp` (mode orchestrator)
- `buffer_process.cpp` (UART/FIFO sensor buffer process)
- game/visualization modes (`*.cpp`, `*.cc`)
- helper Python scripts for weather/NASA/Grid-Eye data

## Architecture (runtime flow)

1. `master_script` creates FIFOs:
   - command FIFO: `/tmp/uart_fifo`
   - data FIFO: `/tmp/mode_fifo`
2. `master_script` launches `buffer_process` (expects binary named exactly `./buffer_process`).
3. The active mode reads from `/tmp/mode_fifo` and renders to the LED matrix.
4. Mode switching happens via GPIO button or through `main_mode_rotary` exit code signaling.

Logs are written to:

- `logs/master_log_YYYY-MM-DD.txt`
- `data_logs/data_log_YYYY-MM-DD.txt`

## Prerequisites (Raspberry Pi)

- `rpi-rgb-led-matrix` checked out locally (see install/location section below)
- C/C++ toolchain (`g++`, `make`)
- `libgpiod` (for GPIO in `master_script`)
- `WiringPi` (used by `main_mode_rotary.cpp`)
- GraphicsMagick++ and libcurl (for image/weather modes)
- SQLite3 (for `db_manager.cpp`)
- Python 3 with required packages (`requests`, `buienradar`, `numpy`, `scipy`, `colour`, `smbus`)

### Install `rpi-rgb-led-matrix` and required location

This project depends on the external open-source repository:

- https://github.com/hzeller/rpi-rgb-led-matrix

Clone it as a sibling of `main_application` inside your `LED-Board` folder:

```bash
cd /home/pi/Documents/LED-Board
git clone https://github.com/hzeller/rpi-rgb-led-matrix.git
```

Expected layout:

```text
/home/pi/Documents/LED-Board/
  main_application/
  rpi-rgb-led-matrix/
```

The default build scripts in this folder expect:

```bash
export RGBMATRIX_DIR=/home/pi/Documents/LED-Board/rpi-rgb-led-matrix
```

If you place `rpi-rgb-led-matrix` somewhere else, set `RGBMATRIX_DIR` to that absolute path before building.

Example Debian/Raspberry Pi OS packages:

```bash
sudo apt update
sudo apt install -y g++ make libgpiod-dev libsqlite3-dev libcurl4-openssl-dev graphicsmagick libgraphicsmagick++-dev python3-pip python3-scipy python3-colour python3-smbus
```

Optional (for `main_mode_rotary.cpp`):

```bash
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build
```

Python dependencies:

```bash
pip3 install requests buienradar
```

## Build

Set your LED matrix library location (adjust path as needed):

```bash
export RGBMATRIX_DIR=/home/pi/Documents/LED-Board/rpi-rgb-led-matrix
```

### 1) Build everything (recommended)

```bash
bash build_all.sh
```

This builds all mode binaries listed in `master_script.cpp` plus:

- `master_script`
- `buffer_process`
- `db_manager`

### 2) Makefile-only builds

```bash
make
```

This builds all mode binaries listed in `master_script.cpp`.

Additional useful targets:

- `make extras` (builds `master_script`, `buffer_process`, `db_manager`)
- `make build-all` (equivalent to `make all extras`)
- `make clean` (cleans mode objects/binaries)
- `make clean-all` (cleans everything including `master_script`, `buffer_process`, `db_manager`)

### 3) Clean and rebuild from scratch

```bash
make clean-all
bash build_all.sh
```

## Run

From this directory:

```bash
sudo ./master_script
```

Notes:

- `master_script` starts in mode index `17` (`./main_mode_rotary`) by default.
- It expects UART device `/dev/ttyACM0`.
- It uses `sudo chmod 666 /dev/ttyACM0` at startup.

## Mode list in `master_script.cpp`

Current order (index → executable):

0. `updated_pong_with_rotary`
1. `brick_breaker`
2. `wave_gen`
3. `sensor_driven_visuals_7`
4. `falling_sand`
5. `snake_in_labyrinth`
6. `updated_ollama`
7. `stars`
8. `cat_n_mouse`
9. `2_mice_1_cat`
10. `weather_info_panel`
11. `nasa_image`
12. `falling_sand_rotary_color`
13. `dynamic_fireworks_rotary`
14. `interactive_weather_rotary`
15. `coral_garden_rotary`
16. `2_player_pong`
17. `main_mode_rotary`
18. `3d_labyrinth_control`
19. `thermal_display`

## Weather and NASA helper scripts

- `buienRadarToCsv.py` updates `weather_data.csv` every 10 minutes.
- `nasa_image.py` downloads APOD image + title into:
  - `nasa_image_output.jpg`
  - `nasa_image_title.txt`

## Thermal (Grid-Eye) mode notes

- Thermal mode executable: `./thermal_display`
- Python helper: `thermal_cam_script.py`
- Run `master_script` with `sudo` so matrix timing and I2C access are available.
- The runtime uses `python3 thermal_cam_script.py` (no nested `sudo`).

## Database utility

`db_manager.cpp` parses `data_logs/*.txt` and writes to `logs.db`:

```bash
g++ -std=c++17 -o db_manager db_manager.cpp -lsqlite3
./db_manager
```

It creates/uses tables:

- `SensorReadings`
- `LogProgress`

## Known caveats

- Several legacy compile commands in older docs had path typos (`/Lhome/...`). Use the commands in this README instead.
