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
#include <stdarg.h>   // <-- ADDED to fix 'va_start' / 'va_end' errors
#include <libgen.h>   // For dirname()
#include <linux/limits.h> // for PATH_MAX

// Your existing definitions and includes
#define UART_DEVICE "/dev/ttyACM0"
#define COMMAND_FIFO_PATH "/tmp/uart_fifo"
#define DATA_FIFO_PATH "/tmp/mode_fifo"
#define BUFFER_PROCESS "./buffer_process"
#define LOG_DIR "./logs"

// Define the correct python path here, this is the key to fixing module/permission issues
#define PYTHON_PATH "/usr/bin/python"

// Global variables for process management
pid_t buffer_pid = -1;
pid_t ollama_pid = -1;        // Process ID for updated_ollama
pid_t weather_py_pid = -1;    // Process ID for weather_info_panel's Python script
pid_t nasa_py_pid = -1;       // Process ID for nasa_image's Python script

int fifo_fd = -1; // Global variable to keep FIFO open

// UART variables
static int master_uart_fd = -1;
static std::atomic<bool> uart_reader_running{true};

// ---------------------------------------------------------------------
// NEW HELPER FUNCTION: Logs a message with a timestamp (hh:mm:ss),
// then flushes immediately to ensure the log writes are not buffered.
// ---------------------------------------------------------------------
void logTimestampedMessage(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    time_t now = time(NULL);
    struct tm* t = localtime(&now);

    // Prepend [HH:MM:SS]
    fprintf(stdout, "[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    fflush(stdout);

    va_end(args);
}

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

    // After redirecting stdout/stderr, set line-buffered mode so each
    // line is flushed immediately.
    setvbuf(stdout, NULL, _IOLBF, 0);

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
        logTimestampedMessage("Buffer process launched with PID %d.", buffer_pid);
    }
}

void terminateBufferProcess() {
    if (buffer_pid > 0) {
        kill(buffer_pid, SIGTERM);
        waitpid(buffer_pid, NULL, 0);
        logTimestampedMessage("Buffer process terminated.");
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
    // We treat this as a system message so it gets timestamped
    logTimestampedMessage("Sent command to FIFO: %s", command);
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
    logTimestampedMessage("Switching to %s mode...", mode);
    setpgid(0, 0);
    execlp(mode, mode, NULL);
    perror("Failed to launch mode");
    exit(1);
}

void terminateMode(pid_t child_pid) {
    if (child_pid > 0) {
        logTimestampedMessage("Attempting to terminate child process with PID %d", child_pid);
        if (kill(child_pid, 0) == 0) {
            if (kill(child_pid, SIGTERM) == 0) {
                waitpid(child_pid, NULL, 0);
                logTimestampedMessage("Child process terminated successfully.");
            } else {
                perror("Failed to terminate child process");
            }
        } else {
            logTimestampedMessage("Child process with PID %d is not running.", child_pid);
        }
    } else {
        logTimestampedMessage("No active child process to terminate.");
    }
}

void setUartPermissions() {
    logTimestampedMessage("Setting permissions for %s...", UART_DEVICE);
    int ret = system("sudo chmod 666 /dev/ttyACM0");
    if (ret != 0) {
        fprintf(stderr, "Failed to set permissions for %s. Exiting.\n", UART_DEVICE);
        exit(1);
    }
    logTimestampedMessage("Permissions set successfully.");
}

void logModeSwitch(const char* modeName) {
    // Reuse a separate function for mode switching logs
    logTimestampedMessage("Mode switched to: %s", modeName);
}

void launchOllamaMode() {
    ollama_pid = fork();
    if (ollama_pid == 0) {
        setpgid(0, 0);
        execlp("./updated_ollama", "./updated_ollama", "-f", "6x9.bdf", NULL);
        perror("Failed to launch updated_ollama");
        exit(1);
    } else if (ollama_pid < 0) {
        perror("Failed to fork for updated_ollama");
    } else {
        logTimestampedMessage("updated_ollama launched with PID %d.", ollama_pid);
    }
}

void terminateOllamaMode() {
    if (ollama_pid > 0) {
        kill(ollama_pid, SIGTERM);
        waitpid(ollama_pid, NULL, 0);
        logTimestampedMessage("updated_ollama terminated.");
        ollama_pid = -1;
    }
}

void launchWeatherMode(pid_t *child_pid) {
    weather_py_pid = fork();
    if (weather_py_pid == 0) {
        setpgid(0, 0);
        execlp(PYTHON_PATH, "python", "./buienRadarToCsv.py", NULL);
        perror("Failed to launch buienRadarToCsv.py");
        exit(1);
    } else if (weather_py_pid < 0) {
        perror("Failed to fork for buienRadarToCsv.py");
    } else {
        logTimestampedMessage("buienRadarToCsv.py launched with PID %d.", weather_py_pid);
    }

    *child_pid = fork();
    if (*child_pid == 0) {
        runMode("./weather_info_panel");
        exit(0);
    } else if (*child_pid < 0) {
        perror("Failed to fork for weather_info_panel");
        *child_pid = -1;
    } else {
        logTimestampedMessage("weather_info_panel launched with PID %d.", *child_pid);
    }
}

void terminateWeatherMode(pid_t child_pid) {
    terminateMode(child_pid);

    if (weather_py_pid > 0) {
        logTimestampedMessage("Attempting to terminate buienRadarToCsv.py with PID %d", weather_py_pid);
        if (kill(weather_py_pid, 0) == 0) {
            if (kill(weather_py_pid, SIGTERM) == 0) {
                waitpid(weather_py_pid, NULL, 0);
                logTimestampedMessage("buienRadarToCsv.py terminated successfully.");
            } else {
                perror("Failed to terminate buienRadarToCsv.py");
            }
        } else {
            logTimestampedMessage("buienRadarToCsv.py with PID %d is not running.", weather_py_pid);
        }
        weather_py_pid = -1;
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

        execlp(PYTHON_PATH, "python", "./nasa_image.py", date_str, NULL);
        perror("Failed to launch nasa_image.py");
        exit(1);
    } else if (nasa_py_pid < 0) {
        perror("Failed to fork for nasa_image.py");
    } else {
        logTimestampedMessage("nasa_image.py launched with PID %d.", nasa_py_pid);
    }

    *child_pid = fork();
    if (*child_pid == 0) {
        runMode("./nasa_image");
        exit(0);
    } else if (*child_pid < 0) {
        perror("Failed to fork for nasa_image");
        *child_pid = -1;
    } else {
        logTimestampedMessage("nasa_image launched with PID %d.", *child_pid);
    }
}

void terminateNasaImageMode(pid_t child_pid) {
    terminateMode(child_pid);

    if (nasa_py_pid > 0) {
        logTimestampedMessage("Attempting to terminate nasa_image.py with PID %d", nasa_py_pid);
        if (kill(nasa_py_pid, 0) == 0) {
            if (kill(nasa_py_pid, SIGTERM) == 0) {
                waitpid(nasa_py_pid, NULL, 0);
                logTimestampedMessage("nasa_image.py terminated successfully.");
            } else {
                perror("Failed to terminate nasa_image.py");
            }
        } else {
            logTimestampedMessage("nasa_image.py with PID %d is not running.", nasa_py_pid);
        }
        nasa_py_pid = -1;
    }
}

volatile sig_atomic_t exit_requested = 0;

void handle_signal(int sig) {
    exit_requested = 1;
}

int main() {
    // --- THIS IS THE FIX ---
    // Change working directory to the location of the executable.
    // This makes all relative paths for modes and scripts work correctly,
    // regardless of where master_script is launched from.
    char executable_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
    if (len != -1) {
        executable_path[len] = '\0';
        char* exec_dir = dirname(executable_path);
        if (chdir(exec_dir) != 0) {
            perror("FATAL: Failed to change working directory");
            return 1; // Exit if we can't cd to the right place.
        }
    } else {
        perror("FATAL: Failed to find executable path");
        return 1;
    }
    // --- END OF FIX ---

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
        "./3d_labyrinth_control",
        "./thermal_display"
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
        1, // 3d_labyrinth_control
        0  // thermal_display 
    };
    const int totalModes = sizeof(modes) / sizeof(modes[0]);

    time_t lastDataCollectionTime = 0;
    const int dataCollectionInterval = 600;  // Collect data every 600 seconds

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
            logTimestampedMessage("Forked child process with PID %d for mode '%s'",
                                  child_pid, modes[currentMode]);
        }
    }

    struct gpiod_chip *chip = NULL;
    struct gpiod_line_request *button_request = NULL;
    struct gpiod_line_request *led_request = NULL;
    const unsigned int button_offset = 25;
    const unsigned int led_offset = 24;

    struct gpiod_request_config *req_cfg = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_line_settings *line_settings = NULL;

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        perror("Open GPIO chip failed");
        exit(1);
    }

    req_cfg = gpiod_request_config_new();
    line_cfg = gpiod_line_config_new();
    line_settings = gpiod_line_settings_new();
    if (!req_cfg || !line_cfg || !line_settings) {
        perror("Failed to allocate GPIO request objects for button line");
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(line_settings);
        gpiod_chip_close(chip);
        exit(1);
    }

    gpiod_request_config_set_consumer(req_cfg, "master");

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_INPUT) < 0 ||
        gpiod_line_settings_set_bias(line_settings, GPIOD_LINE_BIAS_PULL_UP) < 0 ||
        gpiod_line_config_add_line_settings(line_cfg, &button_offset, 1, line_settings) < 0) {
        perror("Failed to configure button GPIO line");
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(line_settings);
        gpiod_chip_close(chip);
        exit(1);
    }

    button_request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(line_settings);
    req_cfg = NULL;
    line_cfg = NULL;
    line_settings = NULL;

    if (!button_request) {
        perror("Request button GPIO line failed");
        gpiod_chip_close(chip);
        exit(1);
    }

    req_cfg = gpiod_request_config_new();
    line_cfg = gpiod_line_config_new();
    line_settings = gpiod_line_settings_new();
    if (!req_cfg || !line_cfg || !line_settings) {
        perror("Failed to allocate GPIO request objects for LED line");
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(line_settings);
        gpiod_line_request_release(button_request);
        gpiod_chip_close(chip);
        exit(1);
    }

    gpiod_request_config_set_consumer(req_cfg, "master");

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0 ||
        gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_INACTIVE) < 0 ||
        gpiod_line_config_add_line_settings(line_cfg, &led_offset, 1, line_settings) < 0) {
        perror("Failed to configure LED GPIO line");
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(line_settings);
        gpiod_line_request_release(button_request);
        gpiod_chip_close(chip);
        exit(1);
    }

    led_request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(line_settings);

    if (!led_request) {
        perror("Request LED GPIO line failed");
        gpiod_line_request_release(button_request);
        gpiod_chip_close(chip);
        exit(1);
    }

    enum gpiod_line_value prev_val = GPIOD_LINE_VALUE_ACTIVE;

    while (!exit_requested) {
        // ---------------------------------------------------------------------
        // MODIFIED: Skip hardware button read if we're currently in main_mode_rotary
        // ---------------------------------------------------------------------
        if (strcmp(modes[currentMode], "./main_mode_rotary") != 0) {
            enum gpiod_line_value val = gpiod_line_request_get_value(button_request, button_offset);
            if (val == GPIOD_LINE_VALUE_ERROR) {
                perror("Read line input failed");
            } else {
                if (val == GPIOD_LINE_VALUE_INACTIVE && prev_val == GPIOD_LINE_VALUE_ACTIVE) {
                    // "Hardware" button is pressed => switch to next mode
                    if (gpiod_line_request_set_value(led_request, led_offset, GPIOD_LINE_VALUE_ACTIVE) < 0) {
                        perror("Set LED active failed");
                    }

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
                            logTimestampedMessage("Forked child process with PID %d for mode '%s'",
                                                  child_pid, modes[currentMode]);
                        }
                    }
                    if (gpiod_line_request_set_value(led_request, led_offset, GPIOD_LINE_VALUE_INACTIVE) < 0) {
                        perror("Set LED inactive failed");
                    }
                }
                prev_val = val;
            }
        }

        // Check if any child has died
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            logTimestampedMessage("Child process %d exited with status %d", pid, status);

            if (pid == child_pid) {
                // -----------------------------------------------------------------
                // ADDED: If currentMode == main_mode_rotary, check exit code [100..199]
                // -----------------------------------------------------------------
                if (strcmp(modes[currentMode], "./main_mode_rotary") == 0) {
                    if (WIFEXITED(status)) {
                        int code = WEXITSTATUS(status);
                        if (code >= 100 && code < 200) {
                            int nextModeIndex = code - 100;
                            logTimestampedMessage("Main mode exit => user requested mode index %d.", nextModeIndex);

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
                                    logTimestampedMessage("Forked child process with PID %d for '%s'",
                                                          child_pid, modes[currentMode]);
                                }
                            }
                        }
                        else {
                            // Otherwise, main mode exited with some different code,
                            // fallback to just staying in main mode or do nothing
                            logTimestampedMessage("Main mode child exited with code %d (not in [100..199]).", code);
                            child_pid = -1;
                        }
                    }
                    else {
                        // If it didn't exit normally, just mark child_pid dead
                        logTimestampedMessage("Main mode child died abnormally.");
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
                logTimestampedMessage("Buffer process exited unexpectedly. Relaunching...");
                launchBufferProcess();
            }
            else if (pid == ollama_pid) {
                ollama_pid = -1;
                logTimestampedMessage("updated_ollama process exited.");
            }
            else if (pid == weather_py_pid) {
                weather_py_pid = -1;
                logTimestampedMessage("buienRadarToCsv.py process exited.");
            }
            else if (pid == nasa_py_pid) {
                nasa_py_pid = -1;
                logTimestampedMessage("nasa_image.py process exited.");
            }
        }

        time_t currentTime = time(NULL);
        if (difftime(currentTime, lastDataCollectionTime) >= dataCollectionInterval) {
            lastDataCollectionTime = currentTime;

            
            sendCommandToFIFO("GET_ALL_SENSORS");
       
        }

        usleep(100000); // 100 ms
    }

    logTimestampedMessage("Exiting program. Performing cleanup...");

    gpiod_line_request_release(button_request);
    gpiod_line_request_release(led_request);
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

    logTimestampedMessage("Cleanup complete. Program exited.");
    return 0;
}
