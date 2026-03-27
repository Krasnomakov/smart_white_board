#include "led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <ctime>
#include <cmath>
#include <unistd.h>
#include <cstring> // For memset

// Matrix dimensions
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

// RGB structure to represent a pixel color
struct RGB {
    unsigned char r, g, b;
};

// Color palette for agents and particles
RGB agent1Color = {255, 0, 0};    // Red for the emitter
RGB agent2Color = {0, 255, 0};    // Green for the follower
RGB particleColor = {0, 0, 255};  // Blue for particles

// Structure for particles
struct Particle {
    int x, y;
    int lifetime; // Lifetime remaining for larger particles
};

// Agent structure
struct Agent {
    int x, y; // Current position
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

// Move the emitter agent randomly
void moveEmitter(Agent& agent) {
    int dx = (rand() % 3) - 1; // Random step: -1, 0, or 1
    int dy = (rand() % 3) - 1;
    agent.x = (agent.x + dx + MATRIX_WIDTH) % MATRIX_WIDTH;
    agent.y = (agent.y + dy + MATRIX_HEIGHT) % MATRIX_HEIGHT;
}

// Emit particles from the emitter agent
void emitParticles(Agent& emitter, std::vector<Particle>& particles) {
    int numParticles = rand() % 5 + 1; // 1 to 5 particles
    for (int i = 0; i < numParticles; ++i) {
        particles.push_back({
            (emitter.x + (rand() % 3 - 1) + MATRIX_WIDTH) % MATRIX_WIDTH, // Slightly random position
            (emitter.y + (rand() % 3 - 1) + MATRIX_HEIGHT) % MATRIX_HEIGHT,
            numParticles // Lifetime depends on the size of the structure
        });
    }
}

// Move the follower agent towards the nearest particle
void moveFollower(Agent& follower, const std::vector<Particle>& particles) {
    if (particles.empty()) return;

    // Find the nearest particle
    const Particle* nearest = nullptr;
    int minDist = MATRIX_WIDTH * MATRIX_HEIGHT;
    for (const auto& particle : particles) {
        int dist = std::abs(follower.x - particle.x) + std::abs(follower.y - particle.y);
        if (dist < minDist) {
            minDist = dist;
            nearest = &particle;
        }
    }

    // Move towards the nearest particle
    if (nearest) {
        if (follower.x < nearest->x) follower.x++;
        else if (follower.x > nearest->x) follower.x--;
        if (follower.y < nearest->y) follower.y++;
        else if (follower.y > nearest->y) follower.y--;
    }
}

// Check for collisions and update particles
void handleCollisions(Agent& follower, std::vector<Particle>& particles) {
    for (auto it = particles.begin(); it != particles.end();) {
        if (follower.x == it->x && follower.y == it->y) {
            it->lifetime--; // Reduce lifetime on collision
            if (it->lifetime <= 0) {
                it = particles.erase(it); // Remove particle if lifetime is 0
                continue;
            }
        }
        ++it;
    }
}

// Render the matrix with agents and particles
void renderMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, const std::vector<Particle>& particles, const Agent& agent1, const Agent& agent2) {
    struct Color pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT];

    // Clear the pixel matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            pixelMatrix[x][y] = {0, 0, 0}; // Black background
        }
    }

    // Render particles
    for (const auto& particle : particles) {
        pixelMatrix[particle.x][particle.y] = {particleColor.r, particleColor.g, particleColor.b};
    }

    // Render the emitter agent
    pixelMatrix[agent1.x][agent1.y] = {agent1Color.r, agent1Color.g, agent1Color.b};

    // Render the follower agent
    pixelMatrix[agent2.x][agent2.y] = {agent2Color.r, agent2Color.g, agent2Color.b};

    // Output pixel matrix to the LED matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
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

    // Initialize random seed
    initRandom();

    // Initialize agents and particles
    Agent emitter = {MATRIX_WIDTH / 4, MATRIX_HEIGHT / 2};
    Agent follower = {3 * MATRIX_WIDTH / 4, MATRIX_HEIGHT / 2};
    std::vector<Particle> particles;

    while (!interrupt_received) {
        // Move agents and update particles
        moveEmitter(emitter);
        if (rand() / (float)RAND_MAX < 0.3f) { // Emit particles with a probability
            emitParticles(emitter, particles);
        }
        moveFollower(follower, particles);
        handleCollisions(follower, particles);

        // Render everything
        renderMatrix(matrix, offscreen_canvas, particles, emitter, follower);

        // Control the speed of the simulation
        usleep(100000); // 10 FPS
    }

    // Cleanup
    led_matrix_delete(matrix);
    return 0;
}
