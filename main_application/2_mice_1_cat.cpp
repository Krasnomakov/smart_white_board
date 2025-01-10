#include "../include/led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <ctime>
#include <cmath>
#include <cstring> // For memset
#include <unistd.h> // For usleep

// Matrix dimensions
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

// RGB structure to represent a pixel color
struct RGB {
    unsigned char r, g, b;
};

// Colors for agents and particles
RGB emitter1Color = {255, 0, 0};   // Red for emitter 1
RGB emitter2Color = {0, 0, 255};   // Blue for emitter 2
RGB catcherColor = {0, 255, 0};    // Green for the catcher
RGB particle1Color = {255, 165, 0}; // Orange for particles from emitter 1
RGB particle2Color = {128, 0, 128}; // Purple for particles from emitter 2
RGB laserColor = {255, 255, 0};    // Yellow for the laser

// Structure for particles
struct Particle {
    int x, y;    // Position of the particle
    int lifetime; // Remaining lifetime (number of hits to destroy)
    RGB color;   // Color of the particle
};

// Structure for lasers
struct Laser {
    int x, y;  // Current position
    int dx, dy; // Direction of the laser
};

// Agent structure
struct Agent {
    int x, y; // Current position
    RGB color; // Agent color
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

// Emit particles from an emitter agent
void emitParticles(Agent& emitter, std::vector<Particle>& particles, RGB particleColor) {
    int numParticles = rand() % 5 + 1; // 1 to 5 particles
    for (int i = 0; i < numParticles; ++i) {
        particles.push_back({
            (emitter.x + (rand() % 3 - 1) + MATRIX_WIDTH) % MATRIX_WIDTH, // Wrap-around position
            (emitter.y + (rand() % 3 - 1) + MATRIX_HEIGHT) % MATRIX_HEIGHT,
            numParticles, // Lifetime depends on the size of the structure
            particleColor  // Assign particle color
        });
    }
}

// Move the catcher agent towards the nearest particle and return direction
std::pair<int, int> moveCatcher(Agent& catcher, const std::vector<Particle>& particles) {
    if (particles.empty()) return {0, 0};

    // Find the nearest particle
    const Particle* nearest = nullptr;
    int minDist = MATRIX_WIDTH * MATRIX_HEIGHT;
    for (const auto& particle : particles) {
        int dist = std::abs(catcher.x - particle.x) + std::abs(catcher.y - particle.y);
        if (dist < minDist) {
            minDist = dist;
            nearest = &particle;
        }
    }

    // Determine movement direction
    int dx = 0, dy = 0;
    if (nearest) {
        if (catcher.x < nearest->x) dx = 1;
        else if (catcher.x > nearest->x) dx = -1;
        if (catcher.y < nearest->y) dy = 1;
        else if (catcher.y > nearest->y) dy = -1;
    }

    // Move the catcher
    catcher.x = (catcher.x + dx + MATRIX_WIDTH) % MATRIX_WIDTH;
    catcher.y = (catcher.y + dy + MATRIX_HEIGHT) % MATRIX_HEIGHT;

    return {dx, dy};
}

// Shoot a laser in the specified direction
void shootLaser(Agent& catcher, std::vector<Laser>& lasers, int dx, int dy) {
    if (dx != 0 || dy != 0) {
        lasers.push_back({catcher.x, catcher.y, dx, dy});
    }
}

// Update lasers: move them and remove if out of bounds
void updateLasers(std::vector<Laser>& lasers) {
    for (auto it = lasers.begin(); it != lasers.end();) {
        it->x += it->dx;
        it->y += it->dy;

        // Remove lasers that go out of bounds
        if (it->x < 0 || it->x >= MATRIX_WIDTH || it->y < 0 || it->y >= MATRIX_HEIGHT) {
            it = lasers.erase(it);
        } else {
            ++it;
        }
    }
}

// Handle collisions between lasers and particles
void handleCollisions(std::vector<Laser>& lasers, std::vector<Particle>& particles) {
    for (auto laserIt = lasers.begin(); laserIt != lasers.end();) {
        bool hit = false;
        for (auto particleIt = particles.begin(); particleIt != particles.end();) {
            if (laserIt->x == particleIt->x && laserIt->y == particleIt->y) {
                particleIt->lifetime--; // Reduce particle lifetime on hit
                if (particleIt->lifetime <= 0) {
                    particleIt = particles.erase(particleIt); // Remove particle if destroyed
                } else {
                    ++particleIt;
                }
                hit = true;
                break;
            } else {
                ++particleIt;
            }
        }

        // Remove laser if it hit a particle
        if (hit) {
            laserIt = lasers.erase(laserIt);
        } else {
            ++laserIt;
        }
    }
}

// Handle collision between the catcher and particles
void handleCatcherCollisions(Agent& catcher, std::vector<Particle>& particles) {
    for (auto it = particles.begin(); it != particles.end();) {
        if (catcher.x == it->x && catcher.y == it->y) {
            it = particles.erase(it); // Destroy particle on collision
        } else {
            ++it;
        }
    }
}

// Render the matrix with agents, particles, and lasers
void renderMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, const std::vector<Particle>& particles, const std::vector<Laser>& lasers, const std::vector<Agent>& emitters, const Agent& catcher) {
    struct Color pixelMatrix[MATRIX_WIDTH][MATRIX_HEIGHT];

    // Clear the pixel matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            pixelMatrix[x][y] = {0, 0, 0}; // Black background
        }
    }

    // Render particles
    for (const auto& particle : particles) {
        pixelMatrix[particle.x][particle.y] = {particle.color.r, particle.color.g, particle.color.b};
    }

    // Render lasers
    for (const auto& laser : lasers) {
        pixelMatrix[laser.x][laser.y] = {laserColor.r, laserColor.g, laserColor.b};
    }

    // Render the emitters
    for (const auto& emitter : emitters) {
        pixelMatrix[emitter.x][emitter.y] = {emitter.color.r, emitter.color.g, emitter.color.b};
    }

    // Render the catcher agent
    pixelMatrix[catcher.x][catcher.y] = {catcher.color.r, catcher.color.g, catcher.color.b};

    // Output pixel matrix to the LED matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            led_canvas_set_pixel(offscreen_canvas, x, y, pixelMatrix[x][y].r, pixelMatrix[x][y].g, pixelMatrix[x][y].b);
        }
    }

    // Swap the offscreen canvas to display the updated pixels
    led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

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
    rt_options.gpio_slowdown = 4;

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }
    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

    // Initialize random seed
    initRandom();

    // Initialize agents, particles, and lasers
    Agent emitter1 = {MATRIX_WIDTH / 4, MATRIX_HEIGHT / 4, emitter1Color};
    Agent emitter2 = {3 * MATRIX_WIDTH / 4, 3 * MATRIX_HEIGHT / 4, emitter2Color};
    Agent catcher = {MATRIX_WIDTH / 2, MATRIX_HEIGHT / 2, catcherColor};
    std::vector<Particle> particles;
    std::vector<Laser> lasers;
    std::vector<Agent> emitters = {emitter1, emitter2};

    int actionCounter = 0;

    while (!interrupt_received) {
        // Move emitters and emit particles
        moveEmitter(emitter1);
        moveEmitter(emitter2);
        if (rand() / (float)RAND_MAX < 0.1f) {
            emitParticles(emitter1, particles, particle1Color);
        }
        if (rand() / (float)RAND_MAX < 0.1f) {
            emitParticles(emitter2, particles, particle2Color);
        }

        // Alternate between moving and shooting
        if (actionCounter % 3 == 0) {
            auto direction = moveCatcher(catcher, particles);
            shootLaser(catcher, lasers, direction.first, direction.second);
        }

        // Update lasers and handle collisions
        updateLasers(lasers);
        handleCollisions(lasers, particles);

        // Handle collisions between catcher and particles
        handleCatcherCollisions(catcher, particles);

        // Render everything
        renderMatrix(matrix, offscreen_canvas, particles, lasers, emitters, catcher);

        // Control frame rate
        usleep(50000); // 20 FPS
        actionCounter++;
    }

    // Cleanup
    led_matrix_delete(matrix);
    return 0;
}
