#include "/home/beamboard/rpi-rgb-led-matrix/include/led-matrix-c.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cmath>

const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;
const int PADDLE_WIDTH = 14;  // Increased paddle width
const int PADDLE_HEIGHT = 2;
const int BALL_SIZE = 1;
const int BRICK_ROWS = 4;
const int BRICK_COLUMNS = 16;
const int BRICK_WIDTH = 4;
const int BRICK_HEIGHT = 2;
const char* DATA_FIFO_PATH = "/tmp/mode_fifo";

// RGB color structure for the matrix
struct RGB {
    unsigned char r, g, b;
};

// Paddle structure
struct Paddle {
    int x, y;
    int width, height;
    int speed;
};

// Ball structure
struct Ball {
    int x, y;
    int size;
    int speedX, speedY;
};

// Brick structure
struct Brick {
    int x, y;
    bool alive;
    RGB color;
};

// Paddle and ball colors
RGB paddle_color = {255, 255, 255};
RGB ball_color = {255, 255, 255};

// Interrupt definition for Ctrl+C
volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Assign fixed colors per row (not random)
static RGB row_colors[BRICK_ROWS] = {
    {255, 0, 0},    // Red for row 0
    {0, 255, 0},    // Green for row 1
    {0, 0, 255},    // Blue for row 2
    {255, 255, 0}   // Yellow for row 3
};

void initBricks(std::vector<Brick>& bricks) {
    bricks.clear();
    for (int i = 0; i < BRICK_ROWS; ++i) {
        for (int j = 0; j < BRICK_COLUMNS; ++j) {
            Brick brick;
            brick.x = j * BRICK_WIDTH;
            brick.y = MATRIX_HEIGHT - BRICK_HEIGHT - (i * BRICK_HEIGHT);
            brick.alive = true;
            brick.color = row_colors[i]; // Set color based on the row
            bricks.push_back(brick);
        }
    }
}

// Function to render the matrix with game objects
void renderMatrix(struct Color pixelMatrix[64][32], const Paddle& paddle, const Ball& ball, const std::vector<Brick>& bricks) {
    // Clear the matrix
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            pixelMatrix[x][y].r = 0;
            pixelMatrix[x][y].g = 0;
            pixelMatrix[x][y].b = 0;
        }
    }

    // Render bricks, paddle, and ball
    for (const auto& brick : bricks) {
        if (brick.alive) {
            for (int i = 0; i < BRICK_WIDTH; ++i) {
                for (int j = 0; j < BRICK_HEIGHT; ++j) {
                    int x = brick.x + i;
                    int y = brick.y + j;
                    if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
                        pixelMatrix[x][y].r = brick.color.r;
                        pixelMatrix[x][y].g = brick.color.g;
                        pixelMatrix[x][y].b = brick.color.b;
                    }
                }
            }
        }
    }

    for (int i = 0; i < PADDLE_WIDTH; ++i) {
        for (int j = 0; j < PADDLE_HEIGHT; ++j) {
            int x = paddle.x + i;
            int y = paddle.y + j;
            if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
                pixelMatrix[x][y].r = paddle_color.r;
                pixelMatrix[x][y].g = paddle_color.g;
                pixelMatrix[x][y].b = paddle_color.b;
            }
        }
    }

    if (ball.x >= 0 && ball.x < MATRIX_WIDTH && ball.y >= 0 && ball.y < MATRIX_HEIGHT) {
        pixelMatrix[ball.x][ball.y].r = ball_color.r;
        pixelMatrix[ball.x][ball.y].g = ball_color.g;
        pixelMatrix[ball.x][ball.y].b = ball_color.b;
    }
}

void outputMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, struct Color pixelMatrix[64][32]) {
    int width, height, r, g, b;
    led_canvas_get_size(offscreen_canvas, &width, &height);

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            r = pixelMatrix[x][y].r;
            g = pixelMatrix[x][y].g;
            b = pixelMatrix[x][y].b;
            led_canvas_set_pixel(offscreen_canvas, x, MATRIX_HEIGHT - 1 - y, r, g, b);
        }
    }
    offscreen_canvas = led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

// Read rotary encoder position from FIFO
void readFromFIFO(int fifo_fd, Paddle& paddle, int& last_encoder_value) {
    char buffer[256];
    int bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';

        int encoder_value;
        if (sscanf(buffer, "Rotary Encoder Position: %d", &encoder_value) == 1) {
            int diff = encoder_value - last_encoder_value;
            paddle.x = std::min(std::max(0, paddle.x + diff), MATRIX_WIDTH - PADDLE_WIDTH);
            last_encoder_value = encoder_value;
        }
    }
}

int main(int argc, char** argv) {
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    struct RGBLedMatrix* matrix;
    struct LedCanvas* offscreen_canvas;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

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

    int fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fifo_fd == -1) {
        perror("Failed to open FIFO");
        return 1;
    }

    srand(static_cast<unsigned int>(time(0)));
    Paddle paddle = {MATRIX_WIDTH / 2 - PADDLE_WIDTH / 2, 0, PADDLE_WIDTH, PADDLE_HEIGHT, 2};
    Ball ball = {MATRIX_WIDTH / 2 - BALL_SIZE / 2, PADDLE_HEIGHT + 1, BALL_SIZE, 1, 1};
    std::vector<Brick> bricks;
    initBricks(bricks);

    int last_encoder_value = 0;
    struct Color pixelMatrix[64][32];

    while (!interrupt_received) {
        readFromFIFO(fifo_fd, paddle, last_encoder_value);
        ball.x += ball.speedX;
        ball.y += ball.speedY;

        if (ball.x <= 0 || ball.x + BALL_SIZE >= MATRIX_WIDTH) ball.speedX *= -1;
        if (ball.y + BALL_SIZE >= MATRIX_HEIGHT) ball.speedY *= -1;

        if (ball.x >= paddle.x && ball.x <= paddle.x + PADDLE_WIDTH && ball.y <= paddle.y + PADDLE_HEIGHT) ball.speedY *= -1;

        for (auto& brick : bricks) {
            if (brick.alive && ball.x >= brick.x && ball.x <= brick.x + BRICK_WIDTH && ball.y + BALL_SIZE >= brick.y && ball.y <= brick.y + BRICK_HEIGHT) {
                brick.alive = false;
                ball.speedY *= -1;
                break;
            }
        }

        // Check if all bricks are destroyed
        bool all_destroyed = std::all_of(bricks.begin(), bricks.end(), [](const Brick& b){ return !b.alive; });
        if (all_destroyed) {
            // Restart game when all bricks are destroyed
            initBricks(bricks);
            ball.x = MATRIX_WIDTH / 2;
            ball.y = PADDLE_HEIGHT + 1;
            ball.speedX = 1;
            ball.speedY = 1;
        }

        if (ball.y <= 0) {
            // If the ball is lost from the bottom, reset ball and bricks
            ball.x = MATRIX_WIDTH / 2;
            ball.y = PADDLE_HEIGHT + 1;
            ball.speedX = 1;
            ball.speedY = 1;
            initBricks(bricks);
        }

        renderMatrix(pixelMatrix, paddle, ball, bricks);
        outputMatrix(matrix, offscreen_canvas, pixelMatrix);
        usleep(20000);
    }

    led_matrix_delete(matrix);
    close(fifo_fd);

    return 0;
}
