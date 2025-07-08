#include "/home/beamboard/rpi-rgb-led-matrix/include/led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <algorithm>
#include <string>

#define UART_DEVICE "/dev/ttyACM0"
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

struct RGB {
    unsigned char r, g, b;
};

// Global variables for sensor data
float light_intensity = 0.0f; // From 0.0 (dark) to 1.0 (bright), affects coral growth
float sound_intensity = 0.0f; // From 0.0 to 1.0, affects wave speed
int rotary_direction = 0;    // Controls wave flow direction (-1 for left, 1 for right)

volatile bool interrupt_received = false;

// Handles interrupts for graceful shutdown
static void InterruptHandler(int signo) {
    printf("Interrupt received. Exiting...\n");
    interrupt_received = true;
}

// Generate coral growth based on light intensity
void generateCoral(struct RGB pixelMatrix[64][32], float light_level) {
    printf("Generating coral growth with light level: %f\n", light_level);
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            float growth_factor = light_level * (sin(x * 0.2f) + cos(y * 0.1f));
            growth_factor = std::max(0.0f, std::min(1.0f, growth_factor));

            pixelMatrix[x][y] = {
                static_cast<unsigned char>(growth_factor * 255),
                static_cast<unsigned char>(growth_factor * 150),
                static_cast<unsigned char>(growth_factor * 100)
            };
        }
    }
}

// Simulates water waves flowing through the coral
void simulateWaterFlow(struct RGB pixelMatrix[64][32], float wave_speed, int direction) {
    static float wave_phase = 0.0f;
    printf("Simulating water flow with the wave speed: %f and direction: %d\n", wave_speed, direction);
    wave_phase += wave_speed * 0.1f * direction;

    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        float wave = sin((x + wave_phase) * 0.3f) * 2.0f;

        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            if (fabs(y - (MATRIX_HEIGHT / 2 + wave)) < 2.0) {
                // Add blue highlight for wave
                pixelMatrix[x][y] = {
                    static_cast<unsigned char>(std::min(255, pixelMatrix[x][y].r + 50)),
                    static_cast<unsigned char>(std::min(255, pixelMatrix[x][y].g + 50)),
                    static_cast<unsigned char>(std::min(255, pixelMatrix[x][y].b + 255))
                };
            }
        }
    }
}

// Outputs the matrix to the LED display
void outputMatrix(struct RGBLedMatrix *matrix, struct LedCanvas *offscreen_canvas, struct RGB pixelMatrix[64][32]) {
    printf("Outputting matrix to the LED display...\n");
    int width, height, r, g, b;
    led_canvas_get_size(offscreen_canvas, &width, &height);

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            r = pixelMatrix[x][y].r;
            g = pixelMatrix[x][y].g;
            b = pixelMatrix[x][y].b;
            led_canvas_set_pixel(offscreen_canvas, x, y, r, g, b);
        }
    }

    offscreen_canvas = led_matrix_swap_on_vsync(matrix, offscreen_canvas);
    printf("Matrix output complete.\n");
}

// Reads sensor data from UART
void readSensors(int uart_fd) {
    char buffer[512];
    int bytes_read;

    printf("Reading sensors from UART...\n");

    while ((bytes_read = read(uart_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';

        std::string data(buffer);
        printf("Raw UART Data: %s\n", data.c_str()); // Log raw UART data for debugging

        // Parse for "ALS:"
        size_t als_pos = data.find("ALS:");
        if (als_pos != std::string::npos) {
            size_t end_pos = data.find("lx", als_pos);
            if (end_pos != std::string::npos) {
                light_intensity = atof(data.substr(als_pos + 4, end_pos - (als_pos + 4)).c_str());
                light_intensity = std::min(1.0f, std::max(0.0f, light_intensity / 1000.0f));
                printf("Parsed ALS value: %f\n", light_intensity);
            }
        }

        // Parse for "Sound Magnitude:"
        size_t sound_pos = data.find("Sound Magnitude:");
        if (sound_pos != std::string::npos) {
            sound_intensity = atof(data.substr(sound_pos + 17).c_str());
            sound_intensity = std::min(1.0f, std::max(0.0f, sound_intensity / 500.0f));
            printf("Parsed Sound Magnitude: %f\n", sound_intensity);
        }

        // Parse for "Rotary Encoder Position:"
        size_t rotary_pos = data.find("Rotary Encoder Position:");
        if (rotary_pos != std::string::npos) {
            rotary_direction = atoi(data.substr(rotary_pos + 25).c_str());
            printf("Parsed Rotary Encoder Position: %d\n", rotary_direction);
        }

        printf("Sensor data -> Light: %f, Sound: %f, Rotary: %d\n",
               light_intensity, sound_intensity, rotary_direction);
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from UART");
    }

    tcflush(uart_fd, TCIFLUSH); // Flush the UART buffer to avoid stale data
}

// Sets up UART for sensor data
int setupUART(const char *device) {
    printf("Setting up UART...\n");
    int uart_fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        perror("Failed to open UART");
        return -1;
    }

    struct termios options;
    tcgetattr(uart_fd, &options);

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

    tcsetattr(uart_fd, TCSANOW, &options);
    fcntl(uart_fd, F_SETFL, O_NONBLOCK);
    tcflush(uart_fd, TCIFLUSH);
    printf("UART setup complete.\n");
    return uart_fd;
}

int main(int argc, char **argv) {
    printf("Program starting...\n");
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    struct RGBLedMatrix *matrix;
    struct LedCanvas *offscreen_canvas;
    int width, height;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    printf("Set up UART...\n");
    int uart_fd = setupUART(UART_DEVICE);
    if (uart_fd == -1) {
        fprintf(stderr, "Failed to initialize UART. Exiting.\n");
        return 1;
    }

    memset(&options, 0, sizeof(options));
    memset(&rt_options, 0, sizeof(rt_options));
    options.hardware_mapping = "adafruit-hat";
    options.cols = 64;
    options.rows = 32;
    options.brightness = 70;
    rt_options.gpio_slowdown = 4;
    options.disable_hardware_pulsing = 1;
    options.panel_type = "FM6127";

    printf("Initialize matrix...\n");
    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }

    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);
    led_canvas_get_size(offscreen_canvas, &width, &height);

    struct RGB pixelMatrix[64][32];

    srand(time(NULL)); // Seed for random behavior

    while (!interrupt_received) {
        printf("Main loop iteration start.\n");
        readSensors(uart_fd);

        // Generate coral growth based on light intensity
        generateCoral(pixelMatrix, light_intensity);

        // Simulate water flow based on sound intensity and rotary direction
        simulateWaterFlow(pixelMatrix, sound_intensity, rotary_direction);

        // Display the updated coral reef
        outputMatrix(matrix, offscreen_canvas, pixelMatrix);

        usleep(20000); // 20ms delay
        printf("Main loop iteration end.\n");
    }

    printf("Cleaning up and exiting...\n");
    led_matrix_delete(matrix);
    close(uart_fd);
    return 0;
}
