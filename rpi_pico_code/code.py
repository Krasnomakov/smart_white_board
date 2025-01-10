import time
import board
import busio
import digitalio
import rotaryio
import array
import math
import usb_cdc

# Import sensor libraries
from ltr381rgb import LTR381RGB
from adafruit_sht31d import SHT31D
import audiobusio

# Initialize I2C for sensors
i2c_optical = busio.I2C(scl=board.GP15, sda=board.GP14)
i2c_temp = busio.I2C(scl=board.GP21, sda=board.GP20)

# Initialize sensors
optical = LTR381RGB(i2c_optical)
optical.mode = "CS"
optical.enable()
sht_sensor = SHT31D(i2c_temp)
mic = audiobusio.PDMIn(board.GP4, board.GP5, sample_rate=16000, bit_depth=16)
samples = array.array('H', [0] * 160)

# Initialize two rotary encoders
encoder_left = rotaryio.IncrementalEncoder(board.GP0, board.GP1)
encoder_right = rotaryio.IncrementalEncoder(board.GP12, board.GP13)

# Initialize button on GP26
button = digitalio.DigitalInOut(board.GP26)
button.direction = digitalio.Direction.INPUT
button.pull = digitalio.Pull.UP
last_button_value = button.value

# Initialize encoder offsets for RESET functionality
left_encoder_offset = 0
right_encoder_offset = 0

# Initialize last positions
last_position_left = encoder_left.position - left_encoder_offset
last_position_right = encoder_right.position - right_encoder_offset

def mean(values):
    return sum(values) / len(values)

def normalized_rms(values):
    minbuf = int(mean(values))
    samples_sum = sum((sample - minbuf) ** 2 for sample in values)
    return math.sqrt(samples_sum / len(values))

# Command functions
def read_optical():
    als_lux = optical.lux
    return f"ALS: {als_lux} lx\n"

def read_temperature(retries=3):
    for attempt in range(retries):
        try:
            temp = sht_sensor.temperature
            humidity = sht_sensor.relative_humidity
            return f"Temperature: {temp:.1f} C\nHumidity: {humidity:.1f} %\n"
        except Exception as e:
            print(f"Temperature sensor read error: {e}")
            time.sleep(0.1)  # Retry delay
    return "Error: Temperature sensor not responding.\n"

def read_microphone():
    mic.record(samples, len(samples))
    magnitude = normalized_rms(samples)
    return f"Sound Magnitude: {magnitude}\n"

def read_rotary_encoder_left():
    global last_position_left
    position_left = -encoder_left.position + left_encoder_offset #inverted
    if position_left != last_position_left:
        last_position_left = position_left
        return f"Rotary Encoder Position: {position_left}\n"
    else:
        return ""

def read_rotary_encoder_right():
    global last_position_right
    position_right = -encoder_right.position + right_encoder_offset
    if position_right != last_position_right:
        last_position_right = position_right
        return f"Rotary Encoder Right Position: {position_right}\n"
    else:
        return ""

def read_button():
    global last_button_value
    current_button_value = button.value
    if current_button_value != last_button_value:
        state_str = "pressed" if not current_button_value else "released"
        last_button_value = current_button_value
        return f"Button is {state_str}\n"
    return ""  # Only send if state has changed

# Command dictionary with new encoders
commands = {
    'OPTICAL': read_optical,
    'TEMPERATURE': read_temperature,
    'MICROPHONE': read_microphone,
    'ROTARY': read_rotary_encoder_left,
    'RIGHT_ENCODER': read_rotary_encoder_right,
    'BUTTON': read_button,
}

def read_all_sensors():
    responses = []
    print("read_all_sensors() called")  # Debugging statement
    for cmd in ['OPTICAL', 'TEMPERATURE', 'MICROPHONE', 'ROTARY', 'RIGHT_ENCODER', 'BUTTON']:
        try:
            #print(f"Reading {cmd}")  # Debugging statement
            response = commands[cmd]()
            if response:
                responses.append(response)
               # print(f"{cmd} response: {response.strip()}")  # Debugging statement
            # Add a small delay between sensor reads
            time.sleep(0.01)  # 50 milliseconds
        except Exception as e:
            error_message = f"Error in {cmd}: {e}\n"
            print(error_message)  # Print to console for debugging
            responses.append(error_message)
    # Append the end-of-data marker
    responses.append("<END_OF_DATA>\n")
    # Return the responses as a single string
    return ''.join(responses)

def main():
    print("Waiting for commands...")

    current_commands = set()  # Keep track of commands to execute during streaming
    streaming = False

    global left_encoder_offset, right_encoder_offset  # Access global offsets

    while True:
        # Check for incoming commands
        if usb_cdc.console.in_waiting > 0:
            # Read the incoming message
            request = usb_cdc.console.readline().decode('utf-8').strip()
            new_commands = request.upper().split()
            # Process commands
            for cmd in new_commands:
                if cmd == "STREAM":
                    streaming = True
                elif cmd == "STOP":
                    streaming = False
                    current_commands.clear()
                    print("Streaming stopped.")
                elif cmd == "RESET":
                    streaming = False
                    current_commands.clear()
                    # Update offsets to reset encoder positions to current values
                    left_encoder_offset = encoder_left.position
                    right_encoder_offset = encoder_right.position
                    # Reset last positions
                    last_position_left = 0
                    last_position_right = 0
                    print("Commands reset, encoder positions reset.")
                elif cmd == "GET_ALL_SENSORS":
                    # Read all sensors and send data back immediately
                    response = read_all_sensors()
                    usb_cdc.console.write(response.encode())
                elif cmd in commands:
                    current_commands.add(cmd)
                    print(f"Added command: {cmd}")
                else:
                    usb_cdc.console.write(f"Unknown command: {cmd}\n".encode())

        # Execute commands if streaming is active
        if streaming and current_commands:
            responses = []
            for cmd in current_commands:
                try:
                    response = commands[cmd]()
                    if response:  # Only add non-empty responses
                        responses.append(response)
                except Exception as e:
                    responses.append(f"Error in {cmd}: {e}\n")
            if responses:
                usb_cdc.console.write(''.join(responses).encode())
            time.sleep(0.01)  # Adjust delay as needed
        else:
            time.sleep(0.01)  # Adjust delay as needed

# Run the main loop
main()
