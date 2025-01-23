#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>    // For file operations
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <gpiod.h>    // Include the libgpiod header for GPIO operations
#include <termios.h>
#include <atomic>
#include <pthread.h>

// Your existing definitions and includes
#define UART_DEVICE "/dev/ttyACM0"
#define COMMAND_FIFO_PATH "/tmp/uart_fifo"
#define DATA_FIFO_PATH "/tmp/mode_fifo"
#define BUFFER_PROCESS "./buffer_process"
#define LOG_DIR "./logs"

// Global variables for process management
pid_t buffer_pid = -1;
pid_t ollama_pid = -1;        // Process ID for updated_ollama
pid_t weather_py_pid = -1;    // Process ID for weather_info_panel's Python script
pid_t nasa_py_pid = -1;       // Process ID for nasa_image's Python script

int fifo_fd = -1; // Global variable to keep FIFO open

// UART variables
static int master_uart_fd = -1;
static std::atomic<bool> uart_reader_running{true};

void createLogDirectory() {
    struct stat st = {0};
    if (stat(LOG_DIR, &st) == -1) {
        mkdir(LOG_DIR, 0777);
    }
}

void getLogFilePath(char *logFilePath, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(logFilePath, size, "%s/master_log_%04d-%02d-%02d.txt",
             LOG_DIR, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

void setupLogging() {
    createLogDirectory();

    char logFilePath[256];
    getLogFilePath(logFilePath, sizeof(logFilePath));

    int log_fd = open(logFilePath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (log_fd == -1) {
        perror("Failed to open log file");
        exit(1);
    }

    if (dup2(log_fd, STDOUT_FILENO) == -1) {
        perror("Failed to redirect stdout to log file");
        exit(1);
    }
    if (dup2(log_fd, STDERR_FILENO) == -1) {
        perror("Failed to redirect stderr to log file");
        exit(1);
    }

    close(log_fd);
}

void createFIFOs() {
    umask(0);  // <-- so mkfifo(…, 0666) actually becomes 0666 on disk
    if (mkfifo(COMMAND_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create command FIFO");
        exit(1);
    }
    if (mkfifo(DATA_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create data FIFO");
        exit(1);
    }

    // If you really want to be sure:
    chmod(COMMAND_FIFO_PATH, 0666);
    chmod(DATA_FIFO_PATH, 0666);
}


void launchBufferProcess() {
    buffer_pid = fork();
    if (buffer_pid == 0) {
        setpgid(0, 0);
        execlp(BUFFER_PROCESS, BUFFER_PROCESS, NULL);
        perror("Failed to launch buffer process");
        exit(1);
    } else if (buffer_pid < 0) {
        perror("Failed to fork for buffer process");
    } else {
        printf("Buffer process launched with PID %d.\n", buffer_pid);
    }
}

void terminateBufferProcess() {
    if (buffer_pid > 0) {
        kill(buffer_pid, SIGTERM);
        waitpid(buffer_pid, NULL, 0);
        printf("Buffer process terminated.\n");
        buffer_pid = -1;
    }
}

void sendCommandToFIFO(const char* command) {
    if (fifo_fd == -1) {
        fifo_fd = open(COMMAND_FIFO_PATH, O_WRONLY | O_CLOEXEC);
        if (fifo_fd == -1) {
            perror("Failed to open FIFO for writing");
            return;
        }
    }
    if (write(fifo_fd, command, strlen(command)) == -1) {
        perror("Failed to write command to FIFO");
        if (errno == EPIPE) {
            close(fifo_fd);
            fifo_fd = -1;
        }
        return;
    }
    if (write(fifo_fd, "\n", 1) == -1) {
        perror("Failed to write newline to FIFO");
        if (errno == EPIPE) {
            close(fifo_fd);
            fifo_fd = -1;
        }
        return;
    }
    printf("Sent command to FIFO: %s\n", command);
}

// Setup UART in the master like labyrinth did
int setupUARTMaster(const char* device) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("Failed to open UART in master");
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    tcflush(fd, TCIFLUSH);
    return fd;
}

// UART reader thread function to mimic labyrinth continuous read
void* uartReaderThread(void* arg) {
    char buffer[256];
    while (uart_reader_running.load()) {
        if (master_uart_fd >= 0) {
            int bytes_read = read(master_uart_fd, buffer, sizeof(buffer)-1);
            if (bytes_read > 0) {
                // Just discard data
                // This keeps UART "alive" as labyrinth did
            }
        }
        usleep(30000); // Sleep to reduce CPU usage
    }
    return NULL;
}

void configureUARTCommands(int mode, int totalModes, const int modes_need_uart[]) {
    if (mode < 0 || mode >= totalModes) {
        fprintf(stderr, "Invalid mode index: %d\n", mode);
        return;
    }

    // After removing labyrinth, here are the modes and indexes:
    // 0: ./updated_pong_with_rotary
    // 1: ./brick_breaker
    // 2: ./wave_gen
    // 3: ./sensor_driven_visuals_7
    // 4: ./falling_sand
    // 5: ./snake_in_labyrinth
    // 6: ./updated_ollama
    // 7: ./stars
    // 8: ./cat_n_mouse
    // 9: ./2_mice_1_cat
    // 10: ./weather_info_panel
    // 11: ./nasa_image
    // 12: ./falling_sand_rotary_color
    // 13: ./dynamic_fireworks_rotary
    // 14: ./interactive_weather_rotary
    // 15: ./coral_garden_rotary
    // 16: ./2_player_pong
    // 17: ./main_mode_rotary
    // 18: ./3d_labyrinth_control

    // modes_need_uart updated accordingly:
    // {1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1}

    if (modes_need_uart[mode] == 0) {
        sendCommandToFIFO("RESET");
    } else if (mode == 3 || mode == 13 || mode == 15) {
        sendCommandToFIFO("RESET OPTICAL TEMPERATURE MICROPHONE ROTARY STREAM");
    } else if (mode == 5 || mode == 16 || mode == 2) {
        sendCommandToFIFO("RESET ROTARY RIGHT_ENCODER STREAM");
    } else if (mode == 17 || mode == 18) {
        sendCommandToFIFO("RESET ROTARY BUTTON STREAM");
    } else {
        sendCommandToFIFO("RESET ROTARY STREAM");
    }
}

void runMode(const char* mode) {
    printf("Switching to %s mode...\n", mode);
    setpgid(0, 0);
    execlp(mode, mode, NULL);
    perror("Failed to launch mode");
    exit(1);
}

void terminateMode(pid_t child_pid) {
    if (child_pid > 0) {
        printf("Attempting to terminate child process with PID %d\n", child_pid);
        if (kill(child_pid, 0) == 0) {
            if (kill(child_pid, SIGTERM) == 0) {
                waitpid(child_pid, NULL, 0);
                printf("Child process terminated successfully.\n");
            } else {
                perror("Failed to terminate child process");
            }
        } else {
            printf("Child process with PID %d is not running.\n", child_pid);
        }
    } else {
        printf("No active child process to terminate.\n");
    }
}

void setUartPermissions() {
    printf("Setting permissions for %s...\n", UART_DEVICE);
    int ret = system("sudo chmod 666 /dev/ttyACM0");
    if (ret != 0) {
        fprintf(stderr, "Failed to set permissions for %s. Exiting.\n", UART_DEVICE);
        exit(1);
    }
    printf("Permissions set successfully.\n");
}

void logModeSwitch(const char* modeName) {
    char logEntry[256];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(logEntry, sizeof(logEntry), "[%02d:%02d:%02d] Mode switched to: %s",
             t->tm_hour, t->tm_min, t->tm_sec, modeName);
    printf("%s\n", logEntry);
}

void launchOllamaMode() {
    ollama_pid = fork();
    if (ollama_pid == 0) {
        setpgid(0, 0);
        execlp("./updated_ollama", "./updated_ollama",
               "-f", "/home/pi/Documents/rpi-rgb-led-matrix/fonts/6x10.bdf",
               NULL);
        perror("Failed to launch updated_ollama");
        exit(1);
    } else if (ollama_pid < 0) {
        perror("Failed to fork for updated_ollama");
    } else {
        printf("updated_ollama launched with PID %d.\n", ollama_pid);
    }
}

void terminateOllamaMode() {
    if (ollama_pid > 0) {
        kill(ollama_pid, SIGTERM);
        waitpid(ollama_pid, NULL, 0);
        printf("updated_ollama terminated.\n");
        ollama_pid = -1;
    }
}

void launchWeatherMode(pid_t *child_pid) {
    weather_py_pid = fork();
    if (weather_py_pid == 0) {
        setpgid(0, 0);
        execlp("python3", "python3", "./buienRadarToCsv.py", NULL);
        perror("Failed to launch buienRadarToCsv.py");
        exit(1);
    } else if (weather_py_pid < 0) {
        perror("Failed to fork for buienRadarToCsv.py");
    } else {
        printf("buienRadarToCsv.py launched with PID %d.\n", weather_py_pid);
    }

    *child_pid = fork();
    if (*child_pid == 0) {
        runMode("./weather_info_panel");
        exit(0);
    } else if (*child_pid < 0) {
        perror("Failed to fork for weather_info_panel");
        *child_pid = -1;
    } else {
        printf("weather_info_panel launched with PID %d.\n", *child_pid);
    }
}

void terminateWeatherMode(pid_t child_pid) {
    terminateMode(child_pid);

    if (weather_py_pid > 0) {
        printf("Attempting to terminate buienRadarToCsv.py with PID %d\n", weather_py_pid);
        if (kill(weather_py_pid, 0) == 0) {
            if (kill(weather_py_pid, SIGTERM) == 0) {
                waitpid(weather_py_pid, NULL, 0);
                printf("buienRadarToCsv.py terminated successfully.\n");
            } else {
                perror("Failed to terminate buienRadarToCsv.py");
            }
        } else {
            printf("buienRadarToCsv.py with PID %d is not running.\n", weather_py_pid);
        }
        weather_py_pid = -1;
    } else {
        printf("No active buienRadarToCsv.py process to terminate.\n");
    }
}

void launchNasaImageMode(pid_t *child_pid) {
    nasa_py_pid = fork();
    if (nasa_py_pid == 0) {
        setpgid(0, 0);
        char date_str[11];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

        execlp("python3", "python3", "./nasa_image.py", date_str, NULL);
        perror("Failed to launch nasa_image.py");
        exit(1);
    } else if (nasa_py_pid < 0) {
        perror("Failed to fork for nasa_image.py");
    } else {
        printf("nasa_image.py launched with PID %d.\n", nasa_py_pid);
    }

    *child_pid = fork();
    if (*child_pid == 0) {
        runMode("./nasa_image");
        exit(0);
    } else if (*child_pid < 0) {
        perror("Failed to fork for nasa_image");
        *child_pid = -1;
    } else {
        printf("nasa_image launched with PID %d.\n", *child_pid);
    }
}

void terminateNasaImageMode(pid_t child_pid) {
    terminateMode(child_pid);

    if (nasa_py_pid > 0) {
        printf("Attempting to terminate nasa_image.py with PID %d\n", nasa_py_pid);
        if (kill(nasa_py_pid, 0) == 0) {
            if (kill(nasa_py_pid, SIGTERM) == 0) {
                waitpid(nasa_py_pid, NULL, 0);
                printf("nasa_image.py terminated successfully.\n");
            } else {
                perror("Failed to terminate nasa_image.py");
            }
        } else {
            printf("nasa_image.py with PID %d is not running.\n", nasa_py_pid);
        }
        nasa_py_pid = -1;
    } else {
        printf("No active nasa_image.py process to terminate.\n");
    }
}

volatile sig_atomic_t exit_requested = 0;

void handle_signal(int sig) {
    exit_requested = 1;
}

int main() {
    int currentMode = 17;  // launch main_mode_rotary
    pid_t child_pid = -1;

    const char* modes[] = {
        "./updated_pong_with_rotary",
        "./brick_breaker",
        "./wave_gen",
        "./sensor_driven_visuals_7",
        "./falling_sand",
        "./snake_in_labyrinth",
        "./updated_ollama",
        "./stars",
        "./cat_n_mouse",
        "./2_mice_1_cat",
        "./weather_info_panel",
        "./nasa_image",
        "./falling_sand_rotary_color",
        "./dynamic_fireworks_rotary",
        "./interactive_weather_rotary",
        "./coral_garden_rotary",
        "./2_player_pong",
        "./main_mode_rotary",
        "./3d_labyrinth_control"
    };

    const int modes_need_uart[] = {
        1, // updated_pong_with_rotary
        1, // brick_breaker
        1, // wave_gen
        1, // sensor_driven_visuals_7
        0, // falling_sand
        0, // snake_in_labyrinth
        0, // updated_ollama
        0, // stars
        0, // cat_n_mouse
        0, // 2_mice_1_cat
        0, // weather_info_panel
        0, // nasa_image
        1, // falling_sand_rotary_color
        1, // dynamic_fireworks_rotary
        1, // interactive_weather_rotary
        1, // coral_garden_rotary
        1, // 2_player_pong
        1, // main_mode_rotary
        1  // 3d_labyrinth_control
    };
    const int totalModes = sizeof(modes) / sizeof(modes[0]);

    time_t lastDataCollectionTime = 0;
    const int dataCollectionInterval = 60;  // Collect data every 60 seconds

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    setUartPermissions();
    setupLogging();
    createFIFOs();
    launchBufferProcess();

    fifo_fd = open(COMMAND_FIFO_PATH, O_WRONLY | O_CLOEXEC);
    if (fifo_fd == -1) {
        perror("Failed to open FIFO for writing");
        exit(1);
    }

    // Setup UART in master to keep it active
    master_uart_fd = setupUARTMaster(UART_DEVICE);
    if (master_uart_fd == -1) {
        fprintf(stderr, "Failed to initialize UART in master.\n");
        exit(1);
    }

    // Start the UART reader thread
    pthread_t uart_thread;
    pthread_create(&uart_thread, NULL, uartReaderThread, NULL);

    configureUARTCommands(currentMode, totalModes, modes_need_uart);
    logModeSwitch(modes[currentMode]);

    // Launch the initial mode (main_mode_rotary by default)
    if (strcmp(modes[currentMode], "./updated_ollama") == 0) {
        launchOllamaMode();
        child_pid = -1;
    } else if (strcmp(modes[currentMode], "./weather_info_panel") == 0) {
        launchWeatherMode(&child_pid);
    } else if (strcmp(modes[currentMode], "./nasa_image") == 0) {
        launchNasaImageMode(&child_pid);
    } else {
        child_pid = fork();
        if (child_pid == 0) {
            runMode(modes[currentMode]);
            return 0;
        } else if (child_pid < 0) {
            perror("Failed to fork for mode");
            child_pid = -1;
        } else {
            printf("Forked child process with PID %d for mode '%s'\n", child_pid, modes[currentMode]);
        }
    }

    struct gpiod_chip *chip;
    struct gpiod_line *button_line;
    struct gpiod_line *led_line;

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        perror("Open GPIO chip failed");
        exit(1);
    }

    button_line = gpiod_chip_get_line(chip, 25);
    if (!button_line) {
        perror("Get GPIO line for Button failed");
        gpiod_chip_close(chip);
        exit(1);
    }

    int ret = gpiod_line_request_input_flags(button_line, "master", GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP);
    if (ret < 0) {
        perror("Request line as input failed");
        gpiod_chip_close(chip);
        exit(1);
    }

    led_line = gpiod_chip_get_line(chip, 24);
    if (!led_line) {
        perror("Get GPIO line for LED failed");
        gpiod_line_release(button_line);
        gpiod_chip_close(chip);
        exit(1);
    }

    ret = gpiod_line_request_output(led_line, "master", 0);
    if (ret < 0) {
        perror("Request line as output failed");
        gpiod_line_release(button_line);
        gpiod_chip_close(chip);
        exit(1);
    }

    int prev_val = 1;

    while (!exit_requested) {
        // ---------------------------------------------------------------------
        // MODIFIED: Skip hardware button read if we're currently in main_mode_rotary
        // ---------------------------------------------------------------------
        if (strcmp(modes[currentMode], "./main_mode_rotary") != 0) {
            int val = gpiod_line_get_value(button_line);
            if (val < 0) {
                perror("Read line input failed");
            } else {
                if (val == 0 && prev_val == 1) {
                    // "Hardware" button is pressed => switch to next mode
                    gpiod_line_set_value(led_line, 1);

                    if (strcmp(modes[currentMode], "./updated_ollama") == 0) {
                        terminateOllamaMode();
                    } else if (strcmp(modes[currentMode], "./weather_info_panel") == 0) {
                        terminateWeatherMode(child_pid);
                    } else if (strcmp(modes[currentMode], "./nasa_image") == 0) {
                        terminateNasaImageMode(child_pid);
                    } else {
                        terminateMode(child_pid);
                    }

                    sendCommandToFIFO("RESET");
                    currentMode = (currentMode + 1) % totalModes;
                    configureUARTCommands(currentMode, totalModes, modes_need_uart);
                    logModeSwitch(modes[currentMode]);

                    // Relaunch new mode
                    if (strcmp(modes[currentMode], "./updated_ollama") == 0) {
                        launchOllamaMode();
                        child_pid = -1;
                    } else if (strcmp(modes[currentMode], "./weather_info_panel") == 0) {
                        launchWeatherMode(&child_pid);
                    } else if (strcmp(modes[currentMode], "./nasa_image") == 0) {
                        launchNasaImageMode(&child_pid);
                    } else {
                        child_pid = fork();
                        if (child_pid == 0) {
                            runMode(modes[currentMode]);
                            exit(0);
                        } else if (child_pid < 0) {
                            perror("Failed to fork for mode");
                            child_pid = -1;
                        } else {
                            printf("Forked child process with PID %d for mode '%s'\n",
                                   child_pid, modes[currentMode]);
                        }
                    }
                    gpiod_line_set_value(led_line, 0);
                }
                prev_val = val;
            }
        }

        // Check if any child has died
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            printf("Child process %d exited with status %d\n", pid, status);

            if (pid == child_pid) {
                // -----------------------------------------------------------------
                // ADDED: If currentMode == main_mode_rotary, check exit code [100..199]
                // -----------------------------------------------------------------
                if (strcmp(modes[currentMode], "./main_mode_rotary") == 0) {
                    if (WIFEXITED(status)) {
                        int code = WEXITSTATUS(status);
                        if (code >= 100 && code < 200) {
                            int nextModeIndex = code - 100;
                            printf("Main mode exit => user requested mode index %d.\n", nextModeIndex);

                            sendCommandToFIFO("RESET");
                            currentMode = nextModeIndex;
                            configureUARTCommands(currentMode, totalModes, modes_need_uart);
                            logModeSwitch(modes[currentMode]);

                            // Launch that new mode
                            if (strcmp(modes[currentMode], "./updated_ollama") == 0) {
                                launchOllamaMode();
                                child_pid = -1;
                            } else if (strcmp(modes[currentMode], "./weather_info_panel") == 0) {
                                launchWeatherMode(&child_pid);
                            } else if (strcmp(modes[currentMode], "./nasa_image") == 0) {
                                launchNasaImageMode(&child_pid);
                            } else {
                                child_pid = fork();
                                if (child_pid == 0) {
                                    runMode(modes[currentMode]);
                                    exit(0);
                                } else if (child_pid < 0) {
                                    perror("Failed to fork for next mode");
                                    child_pid = -1;
                                } else {
                                    printf("Forked child process with PID %d for '%s'\n",
                                           child_pid, modes[currentMode]);
                                }
                            }
                        }
                        else {
                            // Otherwise, main mode exited with some different code,
                            // fallback to just staying in main mode or do nothing
                            printf("Main mode child exited with code %d (not in [100..199]).\n", code);
                            child_pid = -1; 
                        }
                    }
                    else {
                        // If it didn't exit normally, just mark child_pid dead
                        printf("Main mode child died abnormally.\n");
                        child_pid = -1;
                    }
                }
                else {
                    // The normal "child died" logic for non-main-mode
                    child_pid = -1;
                }
            }
            else if (pid == buffer_pid) {
                buffer_pid = -1;
                printf("Buffer process exited unexpectedly. Relaunching...\n");
                launchBufferProcess();
            }
            else if (pid == ollama_pid) {
                ollama_pid = -1;
                printf("updated_ollama process exited.\n");
            }
            else if (pid == weather_py_pid) {
                weather_py_pid = -1;
                printf("buienRadarToCsv.py process exited.\n");
            }
            else if (pid == nasa_py_pid) {
                nasa_py_pid = -1;
                printf("nasa_image.py process exited.\n");
            }
        }

        time_t currentTime = time(NULL);
        if (difftime(currentTime, lastDataCollectionTime) >= dataCollectionInterval) {
            lastDataCollectionTime = currentTime;
            // Ping the sensors periodically to ensure data flow
            sendCommandToFIFO("GET_ALL_SENSORS");
        }

        usleep(1000000); // 1000 ms
    }

    printf("Exiting program. Performing cleanup...\n");

    gpiod_line_release(button_line);
    gpiod_line_release(led_line);
    gpiod_chip_close(chip);

    close(fifo_fd);
    terminateBufferProcess();

    if (strcmp(modes[currentMode], "./updated_ollama") == 0) {
        terminateOllamaMode();
    } else if (strcmp(modes[currentMode], "./weather_info_panel") == 0) {
        terminateWeatherMode(child_pid);
    } else if (strcmp(modes[currentMode], "./nasa_image") == 0) {
        terminateNasaImageMode(child_pid);
    } else {
        terminateMode(child_pid);
    }

    unlink(COMMAND_FIFO_PATH);
    unlink(DATA_FIFO_PATH);

    // Stop UART reader thread
    uart_reader_running.store(false);
    pthread_join(uart_thread, NULL);
    if (master_uart_fd >= 0) close(master_uart_fd);

    printf("Cleanup complete. Program exited.\n");
    return 0;
}
