#!/bin/bash

#chmod +x setup_ssh_keys.sh
#./setup_ssh_keys.sh


################################################################################
# setup_ssh_keys.sh
# 
# Author: Artur Kraskov for 3Beam
# 
# Description:
# This script automates the process of setting up SSH key-based authentication 
# between two Raspberry Pis in a local network. It handles the following steps:
# 
# 1. Checks for an existing SSH key on the sending Pi. If not found, it generates a 
#    new SSH key.
# 2. Copies the public key from the sending Pi to the receiving Pi, enabling 
#    passwordless SSH access.
# 3. Verifies that passwordless SSH access is successful.
# 4. Configures SSH settings on the receiving Pi to allow only key-based 
#    authentication, thereby disabling password-based login for added security.
# 5. Ensures correct permissions are set on the receiving Pi for the `.ssh` 
#    directory and `authorized_keys` file.
# 
# Usage:
# 1. Customize the configuration section with the IP addresses and usernames for
#    the sending and receiving Pis.
# 2. Ensure `sshpass` is installed on the sending Pi if you need to avoid manual 
#    password input.
# 3. Run the script with `./setup_ssh_keys.sh` from the sending Pi.
# 
# Requirements:
# - SSH access enabled on both Raspberry Pis.
# - `sshpass` installed on the sending Pi if automating password entry is required.
# - Run with permissions to access and modify SSH files.
#
# Note:
# This script is tailored for Raspberry Pi usage in a secure network environment.
# Modify user names and IPs as needed.
################################################################################

# Configuration
SENDING_PI_USER="pi"
SENDING_PI_HOST="pi"                         # Adjust if the sending Pi has a specific hostname
SENDING_PI_IP="100.76.32.76"                 # Replace with the actual IP of the sending Pi
RECEIVING_PI_USER="centralprocessor"
RECEIVING_PI_HOST="centralprocessor"         # Adjust if the receiving Pi has a specific hostname
RECEIVING_PI_IP="100.72.145.90"              # Replace with the actual IP of the receiving Pi

# SSH key location
SSH_KEY_PATH="/home/$SENDING_PI_USER/.ssh/id_rsa"

# Step 1: Generate SSH key on the sending Pi if not already present
if [[ ! -f "$SSH_KEY_PATH" ]]; then
  echo "Generating SSH key on $SENDING_PI_HOST..."
  ssh-keygen -t rsa -b 4096 -f "$SSH_KEY_PATH" -N ""
else
  echo "SSH key already exists on $SENDING_PI_HOST."
fi

# Step 2: Copy public key to the receiving Pi
# Uses ssh-copy-id to transfer the public key securely
echo "Copying SSH public key to $RECEIVING_PI_HOST..."
sshpass -p 'your_password' ssh-copy-id -i "$SSH_KEY_PATH.pub" $RECEIVING_PI_USER@$RECEIVING_PI_IP

# Step 3: Test SSH connection
# Verifies if passwordless SSH access is functional
echo "Testing SSH access from $SENDING_PI_HOST to $RECEIVING_PI_HOST without password..."
if ssh -o BatchMode=yes -i "$SSH_KEY_PATH" $RECEIVING_PI_USER@$RECEIVING_PI_IP "echo 'SSH key-based access confirmed'"; then
  echo "SSH key-based access is successful."
  
  # Step 4: Configure receiving Pi to disable password-based login
  # Modifies SSH configuration to only allow key-based authentication if the test is successful
  echo "Configuring SSH settings on $RECEIVING_PI_HOST to allow only key-based authentication..."
  ssh $RECEIVING_PI_USER@$RECEIVING_PI_IP << 'EOF'
    sudo sed -i 's/^#*\(PubkeyAuthentication\) no/\1 yes/' /etc/ssh/sshd_config
    sudo sed -i 's/^#*\(PasswordAuthentication\) yes/\1 no/' /etc/ssh/sshd_config
    sudo systemctl restart ssh
EOF
  echo "SSH configuration updated on $RECEIVING_PI_HOST."
else
  echo "SSH key-based access failed. Please check SSH configuration and try again."
fi

# Step 5: Adjust permissions for .ssh directory and authorized_keys file
# Ensures the necessary permissions on the receiving Pi for security
echo "Setting permissions on .ssh directory and files on $RECEIVING_PI_HOST..."
ssh $RECEIVING_PI_USER@$RECEIVING_PI_IP << 'EOF'
  chmod 700 ~/.ssh
  chmod 600 ~/.ssh/authorized_keys
EOF
echo "Permissions set on $RECEIVING_PI_HOST."

echo "SSH setup script completed."
