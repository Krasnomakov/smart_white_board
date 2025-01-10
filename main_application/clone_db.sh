#!/bin/bash

#chmod +x clone_db.sh
#./clone_db.sh

################################################################################
# clone_db.sh
# 
# Author: Artur Kraskov for 3Beam
# 
# Description:
# This script automates the cloning of an SQLite database from a source Raspberry Pi 
# to a target Raspberry Pi over SSH, using Tailscale IPs for secure communication.
# The process includes compressing the database on the source Pi, transferring 
# the compressed file, decompressing it on the target Pi, and performing clean-up 
# to ensure minimal storage use and seamless database cloning.
# 
# Usage:
# 1. Ensure SSH keys are set up between the two Pis for passwordless access.
# 2. Modify the configuration section with the correct IP addresses and database 
#    paths for both the source and target Pis.
# 3. Run this script from the system where you want to initiate the transfer with:
#       ./clone_db.sh
#
# Requirements:
# - SSH access enabled on both Raspberry Pis with public keys for passwordless SSH.
# - `gzip` installed on both Pis for compression and decompression.
# - Sufficient permissions to read and write database files on both source and 
#   target Pis.
# 
# Configuration:
# - SOURCE_PI_IP: The Tailscale IP address of the source Pi.
# - TARGET_PI_IP: The Tailscale IP address of the target Pi.
# - DB_PATH: Absolute path to the SQLite database on the source Pi.
# - REMOTE_DB_PATH: Absolute path where the database should be stored on the target Pi.
# 
################################################################################

# Configuration
SOURCE_PI_IP="100.76.32.76"             # Replace with source Pi Tailscale IP
TARGET_PI_IP="100.72.145.90"             # Replace with target Pi Tailscale IP
DB_PATH="/home/pi/Documents/rpi-rgb-led-matrix/examples-api-use/logs.db"              # Path to the database on the source Pi
REMOTE_DB_PATH="/home/centralprocessor/Documents/logs.db" # Path to store DB on the target Pi

# Step 1: Compress the database on the source Pi
echo "Compressing database on source Pi..."
ssh pi@$SOURCE_PI_IP "gzip -c $DB_PATH" > logs.db.gz
echo "Database compressed to logs.db.gz."

# Step 2: Transfer the compressed database to the target Pi
echo "Transferring database to target Pi..."
scp logs.db.gz centralprocessor@$TARGET_PI_IP:/tmp/
echo "Database transferred."

# Step 3: Decompress the database on the target Pi
echo "Decompressing database on target Pi..."
ssh centralprocessor@$TARGET_PI_IP "gunzip -c /tmp/logs.db.gz > $REMOTE_DB_PATH && rm /tmp/logs.db.gz"
echo "Database decompressed to $REMOTE_DB_PATH on target Pi."

# Step 4: Clean up local compressed file
rm logs.db.gz
echo "Local compressed file removed."

echo "Database clone complete!"
