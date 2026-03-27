#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

# Prefer explicit env var, then local checkout next to main_application, then Pi default.
RGBMATRIX_DIR="${RGBMATRIX_DIR:-$ROOT_DIR/../rpi-rgb-led-matrix}"
if [[ ! -f "$RGBMATRIX_DIR/include/led-matrix.h" ]]; then
	RGBMATRIX_DIR="${RGBMATRIX_DIR_FALLBACK:-/home/pi/Documents/rpi-rgb-led-matrix}"
fi

if [[ ! -f "$RGBMATRIX_DIR/include/led-matrix.h" ]]; then
	echo "Error: led-matrix.h not found under RGBMATRIX_DIR='$RGBMATRIX_DIR'" >&2
	echo "Set RGBMATRIX_DIR to your rpi-rgb-led-matrix checkout path and retry." >&2
	exit 1
fi

echo "[1/2] Building all mode binaries plus controller utilities..."
make RGB_LIB_DISTRIBUTION="$RGBMATRIX_DIR" build-all

echo "[extra] Verifying python helper scripts for mode integrations..."
for script in buienRadarToCsv.py nasa_image.py thermal_cam_script.py; do
	if [[ ! -f "$script" ]]; then
		echo "Error: required helper script '$script' is missing." >&2
		exit 1
	fi
done

echo "[2/2] Build complete."
