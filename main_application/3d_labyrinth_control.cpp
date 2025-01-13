#include "../include/led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctime>
#include <cmath>
#include <vector>
#include <stack>
#include <algorithm>
#include <random>
#include <map>
#include <functional>
#include <climits>
#include <fcntl.h>    // For open(), O_RDONLY, etc.
#include <errno.h>    // For error codes like EAGAIN, EWOULDBLOCK

// =======================================
// 3D Labyrinth on an LED Matrix
// Controls are from a FIFO:
//   - "Rotary Encoder Position: X": turn left/right
//   - "Button Pressed": move forward once
//   - "Button Released": no action
// No keyboard control, no auto-movement.
//
// The player starts in the top-left cell of a generated maze.
// =======================================

// Constants
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

// Maze dimensions in cells
// Each cell maps to a 1x1 in logical coords, but rendered with raycasting.
const int CELLS_X = MATRIX_WIDTH / 2;
const int CELLS_Y = MATRIX_HEIGHT / 2;

// FIFO path (must exist and be written to by some external rotary/button program)
static const char* DATA_FIFO_PATH = "/tmp/mode_fifo";

// RGB color structure
struct RGB {
    unsigned char r, g, b;
};

// Directions
enum Direction { TOP, RIGHT, BOTTOM, LEFT };

// Cell structure for the maze
struct Cell {
    bool visited = false;
    bool walls[4] = {true, true, true, true}; 
};

// Maze section
using Maze = std::vector<std::vector<Cell>>;

// Player structure
struct Player {
    double px, py;   // precise (x,y) within the maze (floats)
    double angle;    // viewing angle
    double fov;      // field of view
    double speed;
    double rotationSpeed;
} player;

// LED matrix array
RGB led_matrix[MATRIX_WIDTH][MATRIX_HEIGHT];

// Maze
Maze maze;

// Random generator
std::mt19937 rng;

// Signal handler for graceful shutdown
volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Check if cell coords are in-bounds
bool isInBoundsCell(int x, int y) {
    return x >= 0 && x < CELLS_X && y >= 0 && y < CELLS_Y;
}

// Opposite direction
Direction opposite(Direction dir) {
    switch (dir) {
        case TOP:    return BOTTOM;
        case RIGHT:  return LEFT;
        case BOTTOM: return TOP;
        case LEFT:   return RIGHT;
    }
    return TOP; // default fallback
}

// Generate a maze using DFS
void generateMaze(Maze& maze, std::mt19937& rng) {
    std::function<void(int, int)> dfs = [&](int cx, int cy) {
        maze[cx][cy].visited = true;
        std::vector<Direction> directions = {TOP, RIGHT, BOTTOM, LEFT};
        std::shuffle(directions.begin(), directions.end(), rng);

        for (auto dir : directions) {
            int nx = cx, ny = cy;
            switch (dir) {
                case TOP:    ny = cy - 1; break;
                case RIGHT:  nx = cx + 1; break;
                case BOTTOM: ny = cy + 1; break;
                case LEFT:   nx = cx - 1; break;
            }

            if (isInBoundsCell(nx, ny) && !maze[nx][ny].visited) {
                maze[cx][cy].walls[dir] = false;
                maze[nx][ny].walls[opposite(dir)] = false;
                dfs(nx, ny);
            }
        }
    };

    // Initialize maze
    for (int x = 0; x < CELLS_X; x++) {
        for (int y = 0; y < CELLS_Y; y++) {
            maze[x][y].visited = false;
            for (int i=0; i<4; i++) maze[x][y].walls[i] = true;
        }
    }

    // Start from top-left cell
    dfs(0,0);
}

// Output the LED matrix array
void outputMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas) {
    int width, height;
    led_canvas_get_size(offscreen_canvas, &width, &height);

    // Draw from our led_matrix[] into the offscreen canvas
    for (int x = 0; x < width && x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < height && y < MATRIX_HEIGHT; ++y) {
            RGB color = led_matrix[x][y];
            // Flip vertical: top in led_matrix -> highest row on LED
            led_canvas_set_pixel(offscreen_canvas, x, MATRIX_HEIGHT - 1 - y, 
                                 color.r, color.g, color.b);
        }
    }

    led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

// Check if a move is blocked by a wall (or out of bounds)
bool isBlocked(double new_px, double new_py) {
    int cellX = (int)floor(new_px);
    int cellY = (int)floor(new_py);
    // If out of maze bounds, blocked
    if (!isInBoundsCell(cellX, cellY)) {
        return true;
    }
    // Otherwise free
    return false;
}

// Raycasting 3D view
void render3DView() {
    // Fill background with ceiling/floor
    for (int x = 0; x < MATRIX_WIDTH; x++) {
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            led_matrix[x][y] = (y < MATRIX_HEIGHT/2) 
                ? RGB{50,50,50}    // ceiling
                : RGB{100,100,100}; // floor
        }
    }

    double fov = player.fov;
    double px = player.px;
    double py = player.py;
    double angle = player.angle;

    // Ray casting
    for (int x=0; x<MATRIX_WIDTH; x++) {
        // For each column, compute the ray angle across the FOV
        double rayAngle = angle + ((double)x/(double)MATRIX_WIDTH -0.5)*fov;

        double rayDirX = cos(rayAngle);
        double rayDirY = sin(rayAngle);

        int mapX = (int)floor(px);
        int mapY = (int)floor(py);

        double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1.0 / rayDirX);
        double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1.0 / rayDirY);

        int stepX = (rayDirX < 0) ? -1 : 1;
        int stepY = (rayDirY < 0) ? -1 : 1;

        double distToBoundaryX = (stepX < 0) ? (px - mapX) : (mapX + 1 - px);
        double distToBoundaryY = (stepY < 0) ? (py - mapY) : (mapY + 1 - py);

        double sideDistX = (rayDirX == 0) ? 1e30 : distToBoundaryX * deltaDistX;
        double sideDistY = (rayDirY == 0) ? 1e30 : distToBoundaryY * deltaDistY;

        bool hit = false;
        Direction hitSide = TOP;

        while (!hit) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                hitSide = (stepX>0)? LEFT : RIGHT;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                hitSide = (stepY>0)? TOP : BOTTOM;
            }

            if (!isInBoundsCell(mapX, mapY)) {
                // Ray left the maze, treat as a wall
                hit = true;
            } else {
                Cell &c = maze[mapX][mapY];
                bool wallHit = false;
                switch(hitSide) {
                    case LEFT:   if (c.walls[LEFT])   wallHit = true; break;
                    case RIGHT:  if (c.walls[RIGHT])  wallHit = true; break;
                    case TOP:    if (c.walls[TOP])    wallHit = true; break;
                    case BOTTOM: if (c.walls[BOTTOM]) wallHit = true; break;
                }
                if (wallHit) hit = true;
            }
        }

        double perpDist;
        if (hitSide == LEFT || hitSide == RIGHT) {
            perpDist = (sideDistX - deltaDistX);
        } else {
            perpDist = (sideDistY - deltaDistY);
        }

        int lineHeight = (int)(MATRIX_HEIGHT / (perpDist + 0.0001));
        int drawStart = -lineHeight/2 + MATRIX_HEIGHT/2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight/2 + MATRIX_HEIGHT/2;
        if (drawEnd >= MATRIX_HEIGHT) drawEnd = MATRIX_HEIGHT - 1;

        // Slight shading if side is TOP/BOTTOM vs LEFT/RIGHT
        RGB wallC = {200,200,200}; 
        if (hitSide == TOP || hitSide == BOTTOM) {
            wallC = {180,180,200}; 
        }

        // Draw the vertical wall slice
        for (int y=drawStart; y<=drawEnd; y++) {
            if (y >= 0 && y < MATRIX_HEIGHT && x >= 0 && x < MATRIX_WIDTH) {
                led_matrix[x][y] = wallC;
            }
        }
    }
}

// -----------------------------------------------------------------------------
//  FIFO handling for rotary/button
// -----------------------------------------------------------------------------
int fifo_fd = -1;
int last_encoder_value = 0;
bool buttonPressedFlag = false;

// Opens the FIFO in non-blocking mode (similar to previous examples).
bool openFIFO() {
    int attempts = 10;
    while ((fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK)) == -1 && attempts > 0) {
        perror("Failed to open FIFO for reading");
        sleep(1);
        attempts--;
    }
    if (fifo_fd == -1) {
        fprintf(stderr, "Could not open FIFO after multiple attempts.\n");
        return false;
    }
    return true;
}

// Reads from FIFO; parses lines for "Rotary Encoder Position: X", "Button Pressed", etc.
void readFromFIFO() {
    if (fifo_fd == -1) return;

    char buffer[256];
    int bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        // We can have multiple lines, but let's assume a single line per read
        // or parse line-by-line if needed. Simple approach for example:
        if (strncmp(buffer, "Rotary Encoder Position:", 24) == 0) {
            int encoder_value;
            if (sscanf(buffer, "Rotary Encoder Position: %d", &encoder_value) == 1) {
                int diff = encoder_value - last_encoder_value;
                last_encoder_value = encoder_value;

                // If diff > 0 => rotate right; diff < 0 => rotate left
                // We'll rotate 0.1 * diff
                double rotation_amount = 0.1 * diff;

                // Update player's angle
                player.angle += rotation_amount;
                // Keep angle in [0..2π]
                while (player.angle < 0)      player.angle += 2*M_PI;
                while (player.angle > 2*M_PI) player.angle -= 2*M_PI;
            }
        }
        else if (strncmp(buffer, "Button is pressed", 14) == 0) {
            // We'll move forward once in the main loop when we see this flag
            buttonPressedFlag = true;
        }
        else if (strncmp(buffer, "Button is released", 15) == 0) {
            // Currently do nothing
        }
        else {
            fprintf(stderr, "Unexpected FIFO data: '%s'\n", buffer);
        }
    }
    else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // 1) Setup signals
    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    // 2) Prepare LED matrix
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    struct RGBLedMatrix* matrix;
    struct LedCanvas* offscreen_canvas;

    memset(&options, 0, sizeof(options));
    memset(&rt_options, 0, sizeof(rt_options));
    options.hardware_mapping = "adafruit-hat";
    options.rows = 32;
    options.cols = 64;
    options.chain_length = 1;
    options.parallel = 1;
    options.show_refresh_rate = false;
    options.brightness = 80;
    options.disable_hardware_pulsing = 1;
    options.panel_type = "FM6127";
    rt_options.gpio_slowdown = 4;

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }
    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

    // 3) Initialize random + Maze
    std::random_device rd;
    rng = std::mt19937(rd());
    generateMaze(maze = Maze(CELLS_X, std::vector<Cell>(CELLS_Y)), rng);

    // 4) Init player (top-left, center of cell)
    player.px = 0.5;
    player.py = 0.5;
    player.angle = 0.0;
    player.fov = M_PI / 3.0;
    player.speed = 0.1;
    player.rotationSpeed = 0.1;

    // 5) Open FIFO
    if (!openFIFO()) {
        // If we couldn't open FIFO, we can continue with no control, or just exit
        fprintf(stderr, "Exiting since FIFO can't be opened.\n");
        led_matrix_delete(matrix);
        return 1;
    }

    // 6) Main loop
    while (!interrupt_received) {
        // Read from FIFO
        readFromFIFO();

        // If the button is pressed, do one forward move
        if (buttonPressedFlag) {
            buttonPressedFlag = false; // reset
            double nx = player.px + cos(player.angle) * player.speed;
            double ny = player.py + sin(player.angle) * player.speed;
            if (!isBlocked(nx, ny)) {
                player.px = nx;
                player.py = ny;
            }
        }

        // Render the 3D scene
        render3DView();
        // Output to the LED matrix
        outputMatrix(matrix, offscreen_canvas);

        // Slight delay
        usleep(100000); // 100 ms
    }

    // Cleanup
    if (fifo_fd != -1) close(fifo_fd);
    led_matrix_delete(matrix);
    return 0;
}

