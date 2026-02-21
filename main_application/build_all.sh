#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

RGBMATRIX_DIR="${RGBMATRIX_DIR:-/home/pi/Documents/rpi-rgb-led-matrix}"

echo "[1/4] Building Makefile targets (weather_info_panel, nasa_image, updated_ollama, thermal_display)..."
make RGB_LIB_DISTRIBUTION="$RGBMATRIX_DIR"

echo "[2/4] Building master_script..."
g++ -o master_script master_script.cpp -lgpiod -lpthread

echo "[3/4] Building buffer_process from beffer_process_wip.cpp..."
g++ -o buffer_process beffer_process_wip.cpp

echo "[4/4] Building db_manager..."
g++ -std=c++17 -o db_manager db_manager.cpp -lsqlite3

echo "Build complete."
