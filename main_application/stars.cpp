#include "../include/led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <vector>
#include <unistd.h>
#include <cmath>
#include <cstring> // For memset

// Matrix dimensions
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

// Star structure
struct Star {
    float x, y, z; // Position in 3D space
};

// Global variables
volatile bool interrupt_received = false;

// Signal handler for clean exit
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Initialize random seed
void initRandom() {
    srand(static_cast<unsigned int>(time(0)));
}

// Initialize stars with random positions
void initStars(std::vector<Star>& stars, int num_stars) {
    stars.resize(num_stars);
    for (auto& star : stars) {
        star.x = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;
        star.y = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;
        star.z = rand() / (float)RAND_MAX;
    }
}

// Update stars' positions
void updateStars(std::vector<Star>& stars) {
    const float STAR_SPEED = 0.02f;
    for (auto& star : stars) {
        star.z -= STAR_SPEED;
        if (star.z <= 0.0f) {
            // Reset star to a new random position
            star.x = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;
            star.y = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;
            star.z = 1.0f;
        }
    }
}

// Render stars to the LED matrix
void renderStars(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, const std::vector<Star>& stars) {
    struct Color pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT];
    int width, height;

    led_canvas_get_size(offscreen_canvas, &width, &height);

    // Clear the pixel matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            pixelMatrix[x][y] = {0, 0, 0}; // Black background
        }
    }

    // Render the stars
    for (const auto& star : stars) {
        int screenX = static_cast<int>((star.x / star.z + 1.0f) * MATRIX_WIDTH / 2);
        int screenY = static_cast<int>((star.y / star.z + 1.0f) * MATRIX_HEIGHT / 2);
        if (screenX >= 0 && screenX < MATRIX_WIDTH && screenY >= 0 && screenY < MATRIX_HEIGHT) {
            int brightness = static_cast<int>((1.0f - star.z) * 255);
            brightness = std::max(0, std::min(255, brightness));
            pixelMatrix[screenX][screenY] = {static_cast<unsigned char>(brightness),
                                             static_cast<unsigned char>(brightness),
                                             static_cast<unsigned char>(brightness)};
        }
    }

    // Output pixel matrix to the LED matrix
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            led_canvas_set_pixel(offscreen_canvas, x, y, pixelMatrix[x][y].r, pixelMatrix[x][y].g, pixelMatrix[x][y].b);
        }
    }

    // Swap the offscreen canvas to display the updated pixels
    led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

// Main function
int main(int argc, char** argv) {
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    struct RGBLedMatrix* matrix;
    struct LedCanvas* offscreen_canvas;

    // Set up signal handlers
    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    // Initialize options for LED matrix
    memset(&options, 0, sizeof(options));
    memset(&rt_options, 0, sizeof(rt_options));
    options.hardware_mapping = "adafruit-hat";
    options.cols = 64;
    options.rows = 32;
    options.chain_length = 1;
    options.disable_hardware_pulsing = 1;
    options.panel_type = "FM6127"; 
    rt_options.gpio_slowdown = 4;

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }
    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

    // Initialize stars
    std::vector<Star> stars;
    initRandom();
    initStars(stars, 100);

    while (!interrupt_received) {
        // Update stars
        updateStars(stars);

        // Render the starfield
        renderStars(matrix, offscreen_canvas, stars);

        // Control the speed of the simulation
        usleep(50000);
    }

    // Cleanup
    led_matrix_delete(matrix);
    return 0;
}
