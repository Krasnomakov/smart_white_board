#!/usr/bin/env python3

import sys
import signal
import time
import requests
import json

# Signal handler to catch SIGTERM and exit gracefully
def signal_handler(sig, frame):
    print('Received SIGTERM, exiting.')
    sys.exit(0)

# Register the signal handler
signal.signal(signal.SIGTERM, signal_handler)

# Base URL and model configuration
base_url = 'http://192.168.88.252:11434/v1/chat/completions'
model_name = "qwen3:1.7b"

# Fixed system and user prompt messages
payload_template = {
    "model": model_name,
    "messages": [
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": "Return a short, simple, and random fact about a topic from your list."}
    ],
    "enable_thinking": False
}

# Continuously generate responses
while True:
    try:
        # Prepare payload
        payload = json.dumps(payload_template)
        headers = {"Content-Type": "application/json"}

        # Make the request to the API
        response = requests.post(base_url, headers=headers, data=payload)

        # Handle the response
        if response.status_code == 200:
            result = response.json()
            assistant_message = result['choices'][0]['message']['content']
            print(assistant_message)
        else:
            print(f"Error: {response.status_code}, {response.text}")

        sys.stdout.flush()  # Ensure the response is sent immediately

    except Exception as e:
        print(f"An error occurred: {e}")

    # Delay before generating the next response
    time.sleep(15)  # Adjust the delay as necessary
