#include "/home/beamboard/rpi-rgb-led-matrix/include/led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <errno.h>

// Define the path to your FIFO (named pipe)
#define DATA_FIFO_PATH "/tmp/mode_fifo"

const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

struct RGB {
    unsigned char r, g, b;
};

struct Raindrop {
    int x, y;
    int speed;
};

const int MAX_RAINDROPS = 100;
Raindrop raindrops[MAX_RAINDROPS];
int active_drops = 0;

// Global variables for sensor data
float temperature = 20.0f;     // Affects rain speed
float humidity = 50.0f;        // Affects rain intensity (number of drops)
int rotary_direction = 1;      // Controls wind direction (-1 for left, 1 for right)

volatile bool interrupt_received = false;

// Handles program interrupts for graceful shutdown
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Initializes raindrops at random positions
void initializeRaindrops() {
    for (int i = 0; i < MAX_RAINDROPS; ++i) {
        raindrops[i] = {rand() % MATRIX_WIDTH, rand() % MATRIX_HEIGHT, 1};
    }
}

// Updates raindrop positions based on temperature and wind direction
void updateRaindrops(float temp, int wind_direction) {
    float normalized_temp = (temp - 22.0f) / (26.0f - 22.0f);
    float base_speed = 1.0f + normalized_temp * 2.0f;

    printf("Wind direction: %d\n", wind_direction);

    for (int i = 0; i < active_drops; ++i) {
        raindrops[i].speed = static_cast<int>(base_speed) + rand() % 2; // Add some randomness
        raindrops[i].x += wind_direction / 2; // Apply wind direction
        raindrops[i].y += raindrops[i].speed; // Move raindrop downward

        // Reset raindrop if it goes out of bounds
        if (raindrops[i].y >= MATRIX_HEIGHT || raindrops[i].x < 0 || raindrops[i].x >= MATRIX_WIDTH) {
            raindrops[i].x = rand() % MATRIX_WIDTH;
            raindrops[i].y = 0;
        }
    }
}

// Generates the rain effect on the LED matrix
void generateRain(struct RGB pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT], int intensity) {
    // Clear the matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            pixelMatrix[x][y] = {0, 0, 0};
        }
    }
    
    // Normalize humidity to 0.0-1.0 based on 40%-80% range
    float normalized_humidity = (humidity - 50.0f) / (70.0f - 50.0f);
    normalized_humidity = std::clamp(normalized_humidity, 0.0f, 1.0f);

    // Activate raindrops based on humidity
    active_drops = std::min(MAX_RAINDROPS, intensity);

    // Draw raindrops
    for (int i = 0; i < active_drops; ++i) {
        if (raindrops[i].y >= 0 && raindrops[i].y < MATRIX_HEIGHT &&
            raindrops[i].x >= 0 && raindrops[i].x < MATRIX_WIDTH) {
            pixelMatrix[raindrops[i].x][raindrops[i].y] = {0, 0, 255}; // Blue raindrops
        }
    }
}

// Outputs the matrix to the LED display
void outputMatrix(struct RGBLedMatrix *matrix, struct LedCanvas *offscreen_canvas, struct RGB pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT]) {
    int width, height;
    led_canvas_get_size(offscreen_canvas, &width, &height);

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            led_canvas_set_pixel(offscreen_canvas, x, y, pixelMatrix[x][y].r, pixelMatrix[x][y].g, pixelMatrix[x][y].b);
        }
    }

    offscreen_canvas = led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

// Reads sensor data via FIFO
void readSensors(int fifo_fd) {
    char buffer[512];
    int bytes_read;

    while ((bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';

        std::string data(buffer);
        printf("Raw Data: %s\n", data.c_str());

        // Parse "Temperature:"
        size_t temp_pos = data.find("Temperature:");
        if (temp_pos != std::string::npos) {
            size_t end_pos = data.find("C", temp_pos);
            if (end_pos != std::string::npos) {
                temperature = atof(data.substr(temp_pos + 12, end_pos - (temp_pos + 12)).c_str());
                temperature = std::clamp(temperature, -10.0f, 50.0f); // Ensure temperature is reasonable
                printf("Parsed Temperature: %f\n", temperature);
            }
        }

        // Parse "Humidity:"
        size_t hum_pos = data.find("Humidity:");
        if (hum_pos != std::string::npos) {
            size_t end_pos = data.find("%", hum_pos);
            if (end_pos != std::string::npos) {
                humidity = atof(data.substr(hum_pos + 9, end_pos - (hum_pos + 9)).c_str());
                humidity = std::clamp(humidity, 0.0f, 100.0f); // Ensure humidity is reasonable
                printf("Parsed Humidity: %f\n", humidity);
            }
        }

        // Parse "Rotary Encoder Position:"
        size_t rotary_pos = data.find("Rotary Encoder Position:");
        if (rotary_pos != std::string::npos) {
            rotary_direction = atoi(data.substr(rotary_pos + 25).c_str());
            printf("Parsed Rotary Encoder Position: %d\n", rotary_direction);
        }
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }

    // No need to flush FIFO
}

// Main function
int main(int argc, char **argv) {
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    struct RGBLedMatrix *matrix;
    struct LedCanvas *offscreen_canvas;
    int width, height;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    // Open FIFO
    int fifo_fd;
    while ((fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK)) == -1) {
        perror("Failed to open FIFO for reading");
        sleep(1);
    }

    memset(&options, 0, sizeof(options));
    memset(&rt_options, 0, sizeof(rt_options));
    options.hardware_mapping = "adafruit-hat";
    options.cols = MATRIX_WIDTH;
    options.rows = MATRIX_HEIGHT;
    options.brightness = 70;
    rt_options.gpio_slowdown = 4;
    options.disable_hardware_pulsing = 1;
    options.panel_type = "FM6127";

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }

    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);
    led_canvas_get_size(offscreen_canvas, &width, &height);

    struct RGB pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT];

    srand(time(NULL)); // Seed for random behavior
    initializeRaindrops();

    while (!interrupt_received) {
        readSensors(fifo_fd);

        // Update raindrops based on temperature and wind direction
        updateRaindrops(temperature, rotary_direction);

        // Generate rain effect based on humidity
        generateRain(pixelMatrix, static_cast<int>(humidity));

        // Display the updated matrix
        outputMatrix(matrix, offscreen_canvas, pixelMatrix);

        usleep(20000); // 20ms delay
    }

    led_matrix_delete(matrix);
    close(fifo_fd);
    return 0;
}
