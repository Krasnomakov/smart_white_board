IMPORTANT COMPILATION COMMANDS 

find more readable information below

-----

Install rpi-rgb-led-matrix library first! 

cd rpi-rgb-led-matrix

cd examples-api-use

rm -rf *

copy all files from main_application into examples-api-use

---

Various libraries might be required to install for different modes:

sudo apt-get install graphicsmagick libgraphicsmagick++-dev

sudo apt-get install libcurl4-openssl-dev

sudo apt update 
sudo apt install g++

sudo apt-get install libgpiod-dev

cd ~
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
git pull origin master
./build

---

Compiling files with g++ (considering that installation of rpi-rgb-led-matrix library was in Documents) 
Change paths according to your user name and location 

g++ -o master_script_with_buffer master_script_with_buffer.cpp -lgpiod

g++ buffer_process.cpp -o buffer_process

g++ db_manager.cpp -o db_manager

g++ 2_player_pong.cpp -o 2_player_pong -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ 3d_labyrinth_control.cpp -o 3d_labyrinth_control -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ brick_breaker.cpp -o brick_breaker -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ -o falling_sand falling_sand.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o falling_sand_rotary_color falling_sand_rotary_color.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o interactive_weather_rotary interactive_weather_rotary.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -v -o main_mode_rotary main_mode_rotary.cpp -I/home/pi/WiringPi/wiringPi -I/home/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrt -lm -pthread -lrgbmatrix -lwiringPi

g++ -o sensor_driven_visuals_7 sensor_driven_visuals_7.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ -o snake_in_labyrinth snake_in_labyrinth.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lm -pthread

g++ updated_pong_with_rotary.cpp -o updated_pong_with_rotary -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ wave_gen.cpp -o wave_gen -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ -o 2_mice_1_cat 2_mice_1_cat.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

g++ -o cat_n_mouse cat_n_mouse.cpp -I/Lhome/pi/Documents/rpi-rgb-led-matrix/include -L/home/pi/Documents/rpi-rgb-led-matrix/lib -lrgbmatrix -lrt -lpthread -lm

----

Then make ollama and nasa image output files, and weather info panel with the makefile: 

make

---

Python dependancies 

buienradar

---

Deploy ollama server on the same network and run it accept requests 

---

For db_manager to work install SQLite
Create logs.db
In it create SensorReadings table 

Then it will be possible to process .txt logs from dataLogs dir into db 

For db cloning need to set up ssh keys - request help 


-----

This repository implements a **multi-mode** LED-matrix application controlled by a **master script** (`master_script.cpp`). The master script uses:
1. A **buffer process** (`buffer_process.cpp`) to handle data flow from various sensors/inputs (UART, rotary encoders, etc.).
2. One of several **modes** (e.g., `2_player_pong.cpp`) that reads sensor data from a FIFO and draws to the LED matrix.

The system is set up so that:
- **`master_script.cpp`** orchestrates launching/killing modes and configures data streams (like UART commands).
- **`buffer_process.cpp`** routes data from sensors (read via UART) to the running mode through a **data FIFO** (`DATA_FIFO_PATH`).
- Each **mode** (like `2_player_pong.cpp`) reads from the **data FIFO** to receive sensor or encoder input, updates game logic, then draws to the matrix.

---

## Table of Contents

1. [High-Level Flow](#high-level-flow)
2. [File Overviews](#file-overviews)
   - [master_script.cpp](#master_scriptcpp)
   - [buffer_process.cpp](#buffer_processcpp)
   - [2_player_pong.cpp (mode example)](#2_player_pongcpp-mode-example)
3. [Detailed Function Explanations](#detailed-function-explanations)
   - [Functions in master_script.cpp](#functions-in-masterscriptcpp)
   - [Functions in buffer_process.cpp](#functions-in-buffer_processcpp)
   - [Functions in 2_player_pong.cpp](#functions-in-2_player_pongcpp)
4. [How to Add a New Mode](#how-to-add-a-new-mode)
5. [Compilation & Running](#compilation--running)

---

## High-Level Flow

1. **master_script.cpp** starts up:
   - Sets up logging, FIFOs, and the UART (if needed).
   - Launches **buffer_process** in the background.
   - Decides which mode to run initially (e.g., `main_mode_rotary` or another).
   - Spawns the chosen mode as a child process (or specialized launch if the mode has extra scripts, like `weather_info_panel` + `buienRadarToCsv.py`).
   
2. **buffer_process.cpp** opens:
   - The UART device (e.g., `/dev/ttyACM0`) for reading sensor data.
   - The **data FIFO** (`DATA_FIFO_PATH`) for writing that data to the mode.
   - The **command FIFO** (`COMMAND_FIFO_PATH`) for receiving “commands” from the master script (and possibly forwarding them to the UART).

3. **The active mode** (example: `2_player_pong.cpp`):
   - Opens the **data FIFO** in read mode, receives sensor/encoder data from `buffer_process`.
   - Uses the LED matrix (via the [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) C library) to draw the game or visualization.

4. **Mode switching**:
   - The master script monitors a hardware button (or receives a code from `main_mode_rotary`).
   - On a mode switch, the old mode is killed; the new mode is forked/exec’d.
   - If required, the master script sends different **commands** through the command FIFO to reconfigure sensor reading.

---

## File Overviews

### `master_script.cpp`
**Responsibilities**:
- Creates and manages FIFOs (`COMMAND_FIFO_PATH` and `DATA_FIFO_PATH`).
- Launches **buffer_process** (`buffer_process.cpp`).
- Houses an array of mode executables (e.g., `./2_player_pong`, `./weather_info_panel`, etc.).
- Monitors a hardware button (GPIO) or listens for signals to switch modes.
- Sends configuration commands to the buffer via the **command FIFO** (e.g. “RESET ROTARY STREAM”).
- Spawns/terminates child processes that run the chosen mode.
- Maintains a simple “keep alive” read on UART so that the device remains active.

### `buffer_process.cpp`
**Responsibilities**:
- Opens the UART device at `UART_DEVICE` (`/dev/ttyACM0` by default).
- Reads incoming sensor/rotary data from UART, logs it, and writes relevant data lines to **`DATA_FIFO_PATH`**.
- Reads “commands” from **`COMMAND_FIFO_PATH`** and writes them back to the UART if needed.
- Creates a daily log of sensor data (e.g., in `data_logs/`) but filters out certain lines (`<END_OF_DATA>`).
- Persists in a loop until killed by the master script.

### `2_player_pong.cpp` (mode example)
**Responsibilities**:
- Showcases how a **mode** uses the LED matrix.
- Contains game logic for a 2-player Pong game, controlling paddles, ball movement, and scoring.
- Reads encoder data from the **data FIFO** for user paddle movement.
- Renders content to the LED matrix with the relevant library calls.
- Illustrates the pattern: **readFromFIFO(...)** → Update game state → **renderMatrix(...)** → **outputMatrix(...)**.

---

## Detailed Function Explanations

### Functions in `master_script.cpp`

1. **`logTimestampedMessage(const char *format, ...)`**  
   - Logs messages to stdout (which is redirected to a daily log file).  
   - Automatically prepends a `[HH:MM:SS]` timestamp.  
   - Uses `va_list` for printf-style variable arguments.

2. **`createLogDirectory()`** and **`getLogFilePath(char *logFilePath, size_t size)`**  
   - Utility functions to ensure a log directory (`./logs`) exists and to build a daily log filepath string.

3. **`setupLogging()`**  
   - Redirects `stdout` and `stderr` to the daily log file.  
   - Sets line-buffered mode so logs are flushed line by line.

4. **`createFIFOs()`**  
   - Creates both `COMMAND_FIFO_PATH` and `DATA_FIFO_PATH` if they don’t exist.  
   - Sets them to 0666 permissions.

5. **`launchBufferProcess()`** and **`terminateBufferProcess()`**  
   - Spawns or terminates the `buffer_process.cpp` executable (by default called `buffer_process`).  
   - Keeps track of the buffer process PID.

6. **`sendCommandToFIFO(const char* command)`**  
   - Opens (if necessary) and writes a text command (plus newline) to **`COMMAND_FIFO_PATH`**.  
   - Typical usage is sending commands like `"RESET ROTARY STREAM"`.

7. **`setupUARTMaster(const char* device)`**  
   - Opens the UART device in the master script (non-blocking) and configures baud rate.  
   - Primarily to keep the port open.  
   - Returns a file descriptor for the *reader thread*.

8. **`uartReaderThread(void* arg)`**  
   - A separate POSIX thread that continuously reads from the master’s open UART FD.  
   - Discards read data (actual reading is in `buffer_process`).

9. **`configureUARTCommands(int mode, int totalModes, const int modes_need_uart[])`**  
   - Sends the appropriate “RESET” command to the buffer to enable streams for sensors, rotary, etc.  
   - Decides which commands to send based on the `mode` index.

10. **`runMode(const char* mode)`**  
   - Calls `execlp` to replace the current child process image with the new mode.

11. **`terminateMode(pid_t child_pid)`**  
   - Kills the current mode process (SIGTERM) and waits for it to exit.

12. **`launchOllamaMode()`, `terminateOllamaMode()`, `launchWeatherMode()`, `terminateWeatherMode()`, `launchNasaImageMode()`, `terminateNasaImageMode()`**  
   - Specialized handling for certain modes that spawn Python scripts or do extra steps.

13. **`handle_signal(int sig)`**  
   - Simple signal handler that sets a global flag (`exit_requested`) to indicate graceful shutdown.

14. **`main()`**  
   - Sets up logging, FIFOs, and permissions.  
   - Launches the **buffer_process**.  
   - Opens the `COMMAND_FIFO_PATH` for writing.  
   - Initializes a background thread for the UART.  
   - Chooses an initial mode, calls `configureUARTCommands(...)`, and forks the child mode.  
   - Opens GPIO lines for a hardware button/LED.  
   - **Main loop**:  
     - Reads the hardware button to switch modes.  
     - Waits for child processes with `waitpid(...)`.  
     - Periodically sends `GET_ALL_SENSORS` to gather sensor data.  
   - On exit, cleans up everything (kills child modes, closes FIFOs, etc.).

### Functions in `buffer_process.cpp`

1. **`openSerial()`**  
   - Configures and opens the UART device (e.g., `/dev/ttyACM0`) with 115200 baud.  
   - Returns the UART file descriptor or `-1` on failure.

2. **`createFIFOs()`**  
   - Creates `COMMAND_FIFO_PATH` and `DATA_FIFO_PATH` if needed.

3. **`createDataLogDirectory()`** and **`getDataLogFilePath(char *logFilePath, size_t size)`**  
   - Create `./data_logs` if needed; build daily log filename.

4. **`logWithTimestamp(FILE *log_file, const char *message)`**  
   - Writes a `[HH:MM:SS] ` prefix plus the message to the data log file.

5. **`isLineEmpty(const char *line)`**  
   - Utility that checks if a line is purely whitespace.

6. **`reopenCommandFIFO(int *cmd_fifo_fd)`**  
   - Reopens the command FIFO if it closes (EOF).

7. **`main()`**  
   - Creates FIFOs, data log directory, and opens the UART.  
   - Opens `COMMAND_FIFO_PATH` in non-blocking read mode.  
   - Opens `DATA_FIFO_PATH` in write mode.  
   - Reads raw data from the UART, line-buffers it, and:
     - For lines with `Rotary Encoder Position:` or `Rotary Encoder Right Position:`, writes them to `DATA_FIFO_PATH`.  
     - For lines with sensor data keywords, logs them **and** writes them to `DATA_FIFO_PATH`.  
   - Also reads **commands** from `COMMAND_FIFO_PATH` and writes them to the UART if needed.

### Functions in `2_player_pong.cpp`

1. **`checkCollision(const Ball& ball, const Paddle& paddle)`**  
   - Returns `true` if the ball intersects the given paddle rectangle.

2. **`resetGame(Paddle& aiPaddle, Paddle& userPaddle, Ball& ball)`**  
   - Resets paddle positions and the ball to center.

3. **`predictBallY(const Ball& ball)`** and **`aiMove(Paddle& aiPaddle, const Ball& ball)`**  
   - Simple AI logic. Predicts ball path and moves AI paddle.

4. **`flashGreenScreen(...)`**  
   - Temporary fill to flash green on scoring.

5. **`displayScore(...)`**  
   - Draws the current score (left vs. right) on the LED matrix buffer.

6. **`renderMatrix(...)`**  
   - Clears `pixelMatrix` and draws the paddles, ball, and score.

7. **`outputMatrix(...)`**  
   - Sends `pixelMatrix` to the actual LED matrix hardware.

8. **`readFromFIFO(...)`**  
   - Reads from `DATA_FIFO_PATH`, checks for `Rotary Encoder Position:` lines, and updates paddle positions.

9. **`main(int argc, char** argv)`**  
   - Sets up the LED matrix.  
   - Opens `DATA_FIFO_PATH`.  
   - Initializes game objects.  
   - Game loop reads FIFO, updates ball/paddles, checks collisions, outputs to matrix until the program is signaled to stop.

---

## How to Add a New Mode

Below is the **step-by-step** to integrate a brand-new mode:

1. **Create Your Mode File**  
   - For example, `awesome_mode.cpp`.  
   - The mode should:
     - Have a `main()` function.  
     - Open **`DATA_FIFO_PATH`** in read mode (to get sensor/encoder data).  
     - Use the LED matrix library for output.

2. **Implement Data Handling**  
   - If your mode needs sensors or rotary encoders, parse lines from the FIFO with a function like `readFromFIFO()`.  
   - If you only want certain lines (like sensor data), handle them specifically.

3. **Decide if the Mode Needs UART**  
   - In `master_script.cpp`, there is an array `modes_need_uart[]`.  
   - If your mode **requires** data from sensors or encoders, set `modes_need_uart[yourIndex] = 1`. Otherwise set it to 0.  

4. **Add the Mode to `master_script.cpp`**  
   1. In the `modes[]` array:
      ```cpp
      const char* modes[] = {
          ...
          "./awesome_mode",  // <== Add this line
      };
      ```
   2. In the `modes_need_uart[]` array (same index):
      ```cpp
      const int modes_need_uart[] = {
          ...
          1, // or 0, matching "./awesome_mode"
      };
      ```
   3. Rebuild the `totalModes` or rely on `sizeof(...) / sizeof(...)`.

5. **(Optional) Specialized Launch**  
   - If your mode needs an additional script (Python, etc.), see `launchWeatherMode()` or `launchNasaImageMode()` in the code.  
   - Make a custom function if necessary, then call it when `modes[currentMode]` matches `"./awesome_mode"`.

6. **Compile & Run**  
   - Rebuild all executables (`master_script`, `buffer_process`, your new mode, etc.).  
   - Run `./master_script`.  
   - Switch modes either by the hardware button or returning exit codes from `main_mode_rotary`.

---

## Compilation & Running

1. **Compile**  
   - For example:
     ```bash
     g++ master_script.cpp -o master_script -lpthread -lgpiod
     g++ buffer_process.cpp -o buffer_process
     g++ 2_player_pong.cpp -o 2_player_pong -lrt -lpthread
     # ...
     ```
   - Make sure to link against the **rpi-rgb-led-matrix** library for modes that use it.  
   - Adjust includes/paths as needed.

2. **Run**  
   - Typically run with sudo if accessing `/dev/ttyACM0` or GPIO:
     ```bash
     sudo ./master_script
     ```
   - The master script will launch `buffer_process` and the initial mode.

3. **Switching Modes**  
   - If not in `main_mode_rotary`, press the hardware button to cycle to the next mode.  
   - If in `main_mode_rotary`, exit codes 100–199 from that mode can trigger direct mode switches.

4. **Logs & Data**  
   - Check `./logs/` for the master script’s daily log.  
   - Check `./data_logs/` for the buffer’s sensor logs.

---

**Enjoy building new interactive LED-matrix visuals!**
```
