#include "led-matrix-c.h"
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
#include <chrono>
#include <errno.h>

// Define the path to your FIFO (named pipe)
#define DATA_FIFO_PATH "/tmp/mode_fifo"

const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

struct RGB {
    unsigned char r, g, b;
};

// Global variables for sensor data
float light_intensity = 0.0f; // Light intensity (300–2000 normalized to 0.0–1.0)
float sound_intensity = 0.0f; // Sound intensity (50–1200 normalized to 0.0–1.0)
int rotary_direction = 0;     // Adjusts firework horizontal position
volatile bool interrupt_received = false;

// Smoothed values for light and sound
float smoothed_light = 0.0f;
float smoothed_sound = 0.0f;
const float LIGHT_SMOOTHING_FACTOR = 0.2f;
const float SOUND_SMOOTHING_FACTOR = 0.1f;

// Handles program interrupts for graceful shutdown
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Generates a starry night background
void generateStarryBackground(struct RGB pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT], float light_level) {
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            if (rand() % 100 < 5) { // 5% chance to place a star
                unsigned char brightness = static_cast<unsigned char>(255 * light_level);
                pixelMatrix[x][y] = {brightness, brightness, brightness};
            } else {
                pixelMatrix[x][y] = {0, 0, 0}; // Empty sky
            }
        }
    }
}

// Creates a firework explosion
void createFirework(struct RGB pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT], int x, int y, float intensity) {
    int radius = std::max(1, static_cast<int>(intensity * 10)); // Scale radius based on intensity
    RGB color = {
        static_cast<unsigned char>(rand() % 256), // Random red
        static_cast<unsigned char>(rand() % 256), // Random green
        static_cast<unsigned char>(rand() % 256)  // Random blue
    };

    for (int i = -radius; i <= radius; ++i) {
        for (int j = -radius; j <= radius; ++j) {
            int draw_x = x + i;
            int draw_y = y + j;

            // Ensure we stay within matrix bounds
            if (draw_x >= 0 && draw_x < MATRIX_WIDTH && draw_y >= 0 && draw_y < MATRIX_HEIGHT) {
                float dist = sqrt(i * i + j * j);
                if (dist <= radius) {
                    pixelMatrix[draw_x][draw_y] = color;
                }
            }
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

        // Parse Light Intensity
        size_t light_pos = data.find("ALS:");
        if (light_pos != std::string::npos) {
            size_t end_pos = data.find("lx", light_pos);
            if (end_pos != std::string::npos) {
                light_intensity = atof(data.substr(light_pos + 4, end_pos - (light_pos + 4)).c_str());
                light_intensity = std::clamp((light_intensity - 100) / (600 - 100), 0.0f, 1.0f);
                printf("Parsed Light Intensity: %.2f\n", light_intensity);
            }
        }

        // Parse Sound Intensity
        size_t sound_pos = data.find("Sound Magnitude:");
        if (sound_pos != std::string::npos) {
            sound_intensity = atof(data.substr(sound_pos + 17).c_str());
            sound_intensity = std::clamp((sound_intensity - 20) / (300 - 20), 0.0f, 1.0f);
            printf("Parsed Sound Intensity: %.2f\n", sound_intensity);
        }

        // Parse Rotary Direction
        size_t rotary_pos = data.find("Rotary Encoder Position:");
        if (rotary_pos != std::string::npos) {
            rotary_direction = atoi(data.substr(rotary_pos + 25).c_str());
            printf("Parsed Rotary Direction: %d\n", rotary_direction);
        }
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }

    // No need to flush FIFO
}

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
    options.brightness = 50;
    rt_options.gpio_slowdown = 4;
    options.panel_type = "FM6127";

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }

    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);
    led_canvas_get_size(offscreen_canvas, &width, &height);

    struct RGB pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT];
    srand(time(NULL));

    auto last_firework_time = std::chrono::steady_clock::now();
    const int FIREWORK_INTERVAL_MS = 500;

    while (!interrupt_received) {
        readSensors(fifo_fd);

        // Smooth the sensor values
        smoothed_light = (LIGHT_SMOOTHING_FACTOR * light_intensity) + ((1 - LIGHT_SMOOTHING_FACTOR) * smoothed_light);
        smoothed_sound = (SOUND_SMOOTHING_FACTOR * sound_intensity) + ((1 - SOUND_SMOOTHING_FACTOR) * smoothed_sound);

        printf("Smoothed Light: %.2f, Smoothed Sound: %.2f, Rotary: %d\n", smoothed_light, smoothed_sound, rotary_direction);

        // Generate starry background
        generateStarryBackground(pixelMatrix, smoothed_light);

        // Trigger fireworks based on sound intensity
        if (smoothed_sound > 0.2f) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_firework_time).count() >= FIREWORK_INTERVAL_MS) {
                last_firework_time = now;

                int x = MATRIX_WIDTH / 2 + rotary_direction; // Adjust horizontal position
                int y = rand() % (MATRIX_HEIGHT / 2);        // Random height in the top half
                printf("Creating firework at (%d, %d) with intensity: %.2f\n", x, y, smoothed_sound);

                createFirework(pixelMatrix, x, y, smoothed_sound);
            }
        }

        // Display the updated matrix
        outputMatrix(matrix, offscreen_canvas, pixelMatrix);
        usleep(160000); // 160ms delay
    }

    led_matrix_delete(matrix);
    close(fifo_fd);
    return 0;
}
