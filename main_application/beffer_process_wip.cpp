#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#define UART_DEVICE "/dev/ttyACM0"
#define COMMAND_FIFO_PATH "/tmp/uart_fifo"
#define DATA_FIFO_PATH "/tmp/mode_fifo"
#define DATA_LOG_DIR "./data_logs"

#define READ_BUFFER_SIZE 256

// Function to open and configure the UART device
int openSerial() {
    int uart_fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        perror("Failed to open UART");
        return -1;
    }

    struct termios options;
    tcgetattr(uart_fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag = CS8 | CLOCAL | CREAD;
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;

    tcsetattr(uart_fd, TCSANOW, &options);
    return uart_fd;
}

// Function to ensure FIFOs exist
void createFIFOs() {
    if (mkfifo(COMMAND_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create command FIFO");
        exit(1);
    }
    if (mkfifo(DATA_FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create data FIFO");
        exit(1);
    }
}

// Helper functions to create unique data log filenames
void getDataLogFilePath(char *logFilePath, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(logFilePath, size, "%s/data_log_%04d-%02d-%02d.txt",
             DATA_LOG_DIR, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

// Ensure data log directory exists
void createDataLogDirectory() {
    struct stat st = {0};
    if (stat(DATA_LOG_DIR, &st) == -1) {
        mkdir(DATA_LOG_DIR, 0777);
    }
}

// Add timestamped log entries
void logWithTimestamp(FILE *log_file, const char *message) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log_file, "[%02d:%02d:%02d] %s\n",
            t->tm_hour, t->tm_min, t->tm_sec, message);
    fflush(log_file);
}

// Function to check if a line contains non-whitespace characters
int isLineEmpty(const char *line) {
    while (*line != '\0') {
        if (!isspace((unsigned char)*line)) {
            return 0; // Line is not empty
        }
        line++;
    }
    return 1; // Line is empty
}

void reopenCommandFIFO(int *cmd_fifo_fd) {
    close(*cmd_fifo_fd);
    *cmd_fifo_fd = open(COMMAND_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (*cmd_fifo_fd == -1) {
        perror("Failed to reopen command FIFO");
        exit(1); // Or handle the error as appropriate
    }
}

int main() {
    // Create FIFOs and ensure data log directory exists
    createFIFOs();
    createDataLogDirectory();

    // Open UART device
    int uart_fd = openSerial();
    if (uart_fd == -1) return 1;

    // Open command FIFO for reading commands from master process (non-blocking)
    int cmd_fifo_fd = open(COMMAND_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (cmd_fifo_fd == -1) {
        perror("Failed to open command FIFO");
        close(uart_fd);
        return 1;
    }

    // Open data FIFO for writing data to modes
    int data_fifo_fd = open(DATA_FIFO_PATH, O_WRONLY);
    if (data_fifo_fd == -1) {
        perror("Failed to open data FIFO");
        close(uart_fd);
        close(cmd_fifo_fd);
        return 1;
    }

    // Open data log file
    char dataLogFilePath[256];
    getDataLogFilePath(dataLogFilePath, sizeof(dataLogFilePath));
    FILE *data_log_file = fopen(dataLogFilePath, "a");
    if (!data_log_file) {
        perror("Failed to open data log file");
        close(uart_fd);
        close(cmd_fifo_fd);
        close(data_fifo_fd);
        return 1;
    }

    // Buffers for UART and commands
    char uart_read_buf[READ_BUFFER_SIZE];
    char uart_line_buf[1024]; // Buffer to hold accumulated UART data
    size_t uart_line_buf_len = 0;

    char cmd_read_buf[READ_BUFFER_SIZE];

    // -----------------------------------------------------------------------
    // FLAG to control logging of sensor data. We enable it when we see
    // <START_OF_LOG>, and disable it when we see <END_OF_DATA>.
    int loggingAllSensors = 0;
    // -----------------------------------------------------------------------

    while (1) {
        // Check if a new data log file is needed (daily rotation)
        char currentDataLogFilePath[256];
        getDataLogFilePath(currentDataLogFilePath, sizeof(currentDataLogFilePath));
        if (strcmp(currentDataLogFilePath, dataLogFilePath) != 0) {
            fclose(data_log_file);
            strcpy(dataLogFilePath, currentDataLogFilePath);
            data_log_file = fopen(dataLogFilePath, "a");
            if (!data_log_file) {
                perror("Failed to open new data log file");
                break;
            }
        }

        // Read from UART
        int bytes_read = read(uart_fd, uart_read_buf, sizeof(uart_read_buf) - 1);
        if (bytes_read > 0) {
            // Process UART data
            for (int i = 0; i < bytes_read; ++i) {
                char ch = uart_read_buf[i];
                uart_line_buf[uart_line_buf_len++] = ch;

                // Either we hit a newline or we've filled the buffer
                if (ch == '\n' || uart_line_buf_len >= sizeof(uart_line_buf) - 1) {
                    uart_line_buf[uart_line_buf_len] = '\0';

                    // Trim leading and trailing whitespace
                    char *line = uart_line_buf;
                    while (isspace((unsigned char)*line)) line++; // Trim leading
                    char *end = uart_line_buf + uart_line_buf_len - 1;
                    while (end > line && isspace((unsigned char)*end)) end--; // Trim trailing
                    *(end + 1) = '\0';

                    if (!isLineEmpty(line)) {
                        // Check for <START_OF_LOG> or <END_OF_DATA>
                        if (strstr(line, "<START_OF_LOG>") != NULL) {
                            loggingAllSensors = 1;
                            // We do not log or forward the marker line itself
                        }
                        else if (strstr(line, "<END_OF_DATA>") != NULL) {
                            loggingAllSensors = 0;
                            // Also do not log the marker line
                        }
                        else {
                            // Check for rotary encoder lines
                            if (strstr(line, "Rotary Encoder Position:") != NULL ||
                                strstr(line, "Rotary Encoder Right Position:") != NULL) {
                                // Forward to modes but do not log
                                if (write(data_fifo_fd, line, strlen(line)) == -1) {
                                    perror("Failed to write to data FIFO");
                                } else {
                                    printf("Data written to data FIFO: %s\n", line);
                                }
                            }
                            // Check for sensor lines (ALS, Temperature, Humidity, etc.)
                            else if (strstr(line, "SENSOR_DATA:") != NULL ||
                                     strstr(line, "ALS:") != NULL ||
                                     strstr(line, "Temperature:") != NULL ||
                                     strstr(line, "Humidity:") != NULL ||
                                     strstr(line, "Sound Magnitude:") != NULL ||
                                     strstr(line, "Button is") != NULL) {

                                // Only log if loggingAllSensors == 1
                                if (loggingAllSensors) {
                                    logWithTimestamp(data_log_file, line);
                                }

                                // Forward to modes
                                if (write(data_fifo_fd, line, strlen(line)) == -1) {
                                    perror("Failed to write to data FIFO");
                                } else {
                                    printf("Data written to data FIFO: %s\n", line);
                                }
                            }
                            // Otherwise ignore or handle other data if needed
                        }
                    }

                    // Reset UART line buffer
                    uart_line_buf_len = 0;
                }
            }
        }

        // Read commands from command FIFO
        int cmd_bytes_read = read(cmd_fifo_fd, cmd_read_buf, sizeof(cmd_read_buf) - 1);
        if (cmd_bytes_read > 0) {
            cmd_read_buf[cmd_bytes_read] = '\0';
            // Send command to UART (unchanged)
            if (write(uart_fd, cmd_read_buf, cmd_bytes_read) == -1) {
                perror("Failed to write command to UART");
            }
        } else if (cmd_bytes_read == 0) {
            // EOF detected, reopen FIFO
            close(cmd_fifo_fd);
            cmd_fifo_fd = open(COMMAND_FIFO_PATH, O_RDONLY | O_NONBLOCK);
            if (cmd_fifo_fd == -1) {
                perror("Failed to reopen command FIFO");
                break;
            }
        } else if (cmd_bytes_read == -1 && errno != EAGAIN) {
            perror("Failed to read from command FIFO");
            break;
        }

        usleep(1000); // Sleep for 1ms
    }

    // Cleanup
    close(uart_fd);
    close(cmd_fifo_fd);
    close(data_fifo_fd);
    fclose(data_log_file);

    return 0;
