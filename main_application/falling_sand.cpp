#include "../include/led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <vector>
#include <unistd.h>
#include <algorithm>
#include <cstring>  // Include this for memset

// Matrix dimensions
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;
const int NUM_COLORS = 6;
const int BLACK_INDEX = NUM_COLORS;

// RGB structure
struct RGB {
    unsigned char r, g, b;
};

// Color palette
RGB colors[NUM_COLORS] = {
    {255, 0, 0},   // Red
    {0, 255, 0},   // Green
    {0, 0, 255},   // Blue
    {255, 255, 0}, // Yellow
    {255, 0, 255}, // Magenta
    {0, 255, 255}  // Cyan
};

// Particle structure
struct Particle {
    int x, y;
    int color_index;
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

// Add a new particle at the top
void addParticle(std::vector<Particle>& particles) {
    int x = rand() % MATRIX_WIDTH;           // Random x position
    int color_index = rand() % NUM_COLORS;   // Random color
    particles.push_back({x, 0, color_index}); // Add particle to the list
}

// Check if the top row of the bitmap is completely filled
bool isTopRowFilled(const std::vector<std::vector<int>>& bitmap) {
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        if (bitmap[x][0] == BLACK_INDEX) {
            return false; // If any pixel in the top row is black, it's not fully filled
        }
    }
    return true; // All pixels in the top row are filled
}

// Reset the animation
void resetAnimation(std::vector<std::vector<int>>& bitmap, std::vector<Particle>& particles) {
    // Clear the bitmap
    for (int y = 0; y < MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < MATRIX_WIDTH; ++x) {
            bitmap[x][y] = BLACK_INDEX;
        }
    }
    // Clear all particles
    particles.clear();
}

// Update particle positions
void updateParticles(std::vector<Particle>& particles, std::vector<std::vector<int>>& bitmap) {
    std::vector<Particle> new_particles;
    for (auto& particle : particles) {
        int x = particle.x;
        int y = particle.y;
        int color_index = particle.color_index;

        // Move the particle down if possible
        if (y < MATRIX_HEIGHT - 1 && bitmap[x][y + 1] == BLACK_INDEX) {
            new_particles.push_back({x, y + 1, color_index});
        } else {
            new_particles.push_back(particle); // Stay in place if blocked
        }
    }

    // Clear the bitmap (fill it with black)
    for (int y = 0; y < MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < MATRIX_WIDTH; ++x) {
            bitmap[x][y] = BLACK_INDEX;
        }
    }

    // Update the bitmap with new particle positions
    for (auto& particle : new_particles) {
        bitmap[particle.x][particle.y] = particle.color_index;
    }

    particles = new_particles; // Update particle list for the next frame
}

// Render the bitmap to the LED matrix
void renderMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, const std::vector<std::vector<int>>& bitmap) {
    struct Color pixelMatrix[64][32];
    int width, height;

    led_canvas_get_size(offscreen_canvas, &width, &height);

    // Render each pixel in the bitmap
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            int color_index = bitmap[x][y];
            RGB color = (color_index == BLACK_INDEX) ? RGB{0, 0, 0} : colors[color_index];
            pixelMatrix[x][y].r = color.r;
            pixelMatrix[x][y].g = color.g;
            pixelMatrix[x][y].b = color.b;
        }
    }

    // Output pixel matrix to the LED matrix without inverting the y-coordinate
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
    options.brightness = 50;

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }
    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

    // Initialize particles and bitmap
    std::vector<std::vector<int>> bitmap(MATRIX_WIDTH, std::vector<int>(MATRIX_HEIGHT, BLACK_INDEX));
    std::vector<Particle> particles;
    initRandom();

    while (!interrupt_received) {
        // Add new particles with a certain probability
        if (rand() / static_cast<float>(RAND_MAX) < 0.1f) {
            addParticle(particles);
        }

        // Update particle positions
        updateParticles(particles, bitmap);

        // Check if the top row is filled
        if (isTopRowFilled(bitmap)) {
            resetAnimation(bitmap, particles);
        }

        // Render the matrix
        renderMatrix(matrix, offscreen_canvas, bitmap);

        // Control the speed of the simulation
        usleep(50000);
    }

    // Cleanup
    led_matrix_delete(matrix);
    return 0;
}
