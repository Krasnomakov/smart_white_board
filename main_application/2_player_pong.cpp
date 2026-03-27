#include "led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <algorithm>
#include <ctime>
#include <errno.h>

#define DATA_FIFO_PATH "/tmp/mode_fifo"

// Constants
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;
const int PADDLE_WIDTH = 2;
const int PADDLE_HEIGHT = 10;
const int BALL_SIZE = 1;
const int PADDLE_Y_MIN = 0;
const int PADDLE_Y_MAX = MATRIX_HEIGHT - PADDLE_HEIGHT;

// Structures
struct Paddle {
    int x, y;
    int width, height;
    int speed;
};

struct Ball {
    int x, y;
    int size;
    int speedX, speedY;
};

struct RGB {
    unsigned char r, g, b;
};

// Colors
RGB paddle_color = {255, 255, 255};
RGB ball_color = {255, 255, 255};
RGB score_color = {255, 255, 0};
RGB green_color = {0, 255, 0};

// Game variables
int leftScore = 0;
int rightScore = 0;
volatile bool interrupt_received = false;

// Signal handler
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Function prototypes
bool checkCollision(const Ball& ball, const Paddle& paddle);
void resetGame(Paddle& aiPaddle, Paddle& userPaddle, Ball& ball);
void aiMove(Paddle& aiPaddle, const Ball& ball);
void renderMatrix(struct Color pixelMatrix[64][32], const Paddle& aiPaddle, const Paddle& userPaddle, const Ball& ball);
void flashGreenScreen(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas);
void displayScore(struct Color pixelMatrix[64][32]);
void outputMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, struct Color pixelMatrix[64][32]);
void readFromFIFO(int fifo_fd, Paddle& userPaddle, int& last_encoder_value, Paddle& aiPaddle, int& last_encoder_value_right);
int predictBallY(const Ball& ball);

// Check collision between ball and paddle
bool checkCollision(const Ball& ball, const Paddle& paddle) {
    return ball.x + ball.size > paddle.x && ball.x < paddle.x + paddle.width &&
           ball.y + ball.size > paddle.y && ball.y < paddle.y + paddle.height;
}

// Reset the game state
void resetGame(Paddle& aiPaddle, Paddle& userPaddle, Ball& ball) {
    aiPaddle.x = 0;
    aiPaddle.y = MATRIX_HEIGHT / 2 - PADDLE_HEIGHT / 2;
    userPaddle.x = MATRIX_WIDTH - PADDLE_WIDTH;
    userPaddle.y = MATRIX_HEIGHT / 2 - PADDLE_HEIGHT / 2;

    ball.x = MATRIX_WIDTH / 2;
    ball.y = MATRIX_HEIGHT / 2;
    ball.speedX = (rand() % 2 == 0) ? 1 : -1;
    ball.speedY = (rand() % 2 == 0) ? 1 : -1;
}

// Predict ball's Y position at AI paddle's X-coordinate
int predictBallY(const Ball& ball) {
    Ball tempBall = ball; // Create a copy of the ball
    int predictedY = tempBall.y;
    int ballSpeedY = tempBall.speedY;

    while (tempBall.x > 0) {
        predictedY += ballSpeedY;

        if (predictedY <= 0 || predictedY >= MATRIX_HEIGHT - BALL_SIZE) {
            ballSpeedY *= -1; // Bounce off walls
        }

        tempBall.x -= 1; // Move the copy of the ball toward AI paddle
    }

    return predictedY;
}

// Move AI paddle based on ball prediction
void aiMove(Paddle& aiPaddle, const Ball& ball) {
    int predictedY = predictBallY(ball);

    if (predictedY > aiPaddle.y + aiPaddle.height / 2) {
        aiPaddle.y += aiPaddle.speed;
    } else if (predictedY < aiPaddle.y + aiPaddle.height / 2) {
        aiPaddle.y -= aiPaddle.speed;
    }

    aiPaddle.y = std::min(PADDLE_Y_MAX, std::max(PADDLE_Y_MIN, aiPaddle.y));
}

// Render a green screen for scoring
void flashGreenScreen(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas) {
    int width, height;
    led_canvas_get_size(offscreen_canvas, &width, &height);

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            led_canvas_set_pixel(offscreen_canvas, x, y, green_color.r, green_color.g, green_color.b);
        }
    }

    led_matrix_swap_on_vsync(matrix, offscreen_canvas);
    usleep(500000); // Display green screen for 500ms
}

// Display scores on the matrix
void displayScore(struct Color pixelMatrix[64][32]) {
    for (int y = 0; y < leftScore; ++y) {
        pixelMatrix[2][MATRIX_HEIGHT - 1 - y].r = score_color.r;
        pixelMatrix[2][MATRIX_HEIGHT - 1 - y].g = score_color.g;
        pixelMatrix[2][MATRIX_HEIGHT - 1 - y].b = score_color.b;
    }

    for (int y = 0; y < rightScore; ++y) {
        pixelMatrix[MATRIX_WIDTH - 3][MATRIX_HEIGHT - 1 - y].r = score_color.r;
        pixelMatrix[MATRIX_WIDTH - 3][MATRIX_HEIGHT - 1 - y].g = score_color.g;
        pixelMatrix[MATRIX_WIDTH - 3][MATRIX_HEIGHT - 1 - y].b = score_color.b;
    }
}

// Render game objects onto the LED matrix
void renderMatrix(struct Color pixelMatrix[64][32], const Paddle& aiPaddle, const Paddle& userPaddle, const Ball& ball) {
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            pixelMatrix[x][y].r = 0;
            pixelMatrix[x][y].g = 0;
            pixelMatrix[x][y].b = 0;
        }
    }

    for (int i = 0; i < PADDLE_HEIGHT; ++i) {
        if (aiPaddle.y + i >= 0 && aiPaddle.y + i < MATRIX_HEIGHT) {
            for (int w = 0; w < PADDLE_WIDTH; ++w) {
                pixelMatrix[aiPaddle.x + w][aiPaddle.y + i] = {paddle_color.r, paddle_color.g, paddle_color.b};
            }
        }
    }

    for (int i = 0; i < PADDLE_HEIGHT; ++i) {
        if (userPaddle.y + i >= 0 && userPaddle.y + i < MATRIX_HEIGHT) {
            for (int w = 0; w < PADDLE_WIDTH; ++w) {
                pixelMatrix[userPaddle.x + w][userPaddle.y + i] = {paddle_color.r, paddle_color.g, paddle_color.b};
            }
        }
    }

    if (ball.x >= 0 && ball.x < MATRIX_WIDTH && ball.y >= 0 && ball.y < MATRIX_HEIGHT) {
        pixelMatrix[ball.x][ball.y] = {ball_color.r, ball_color.g, ball_color.b};
    }

    displayScore(pixelMatrix); // Add score display
}

// Output matrix data to the LED matrix
void outputMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas, struct Color pixelMatrix[64][32]) {
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
}

// Read rotary encoder position from FIFO (now handling two encoders)
void readFromFIFO(int fifo_fd, Paddle& userPaddle, int& last_encoder_value, Paddle& aiPaddle, int& last_encoder_value_right) {
    char buffer[256];
    int bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Received data from FIFO: %s\n", buffer);

        int encoder_value;
        // Handle first encoder (e.g. "Rotary Encoder Position: %d")
        if (sscanf(buffer, "Rotary Encoder Position: %d", &encoder_value) == 1) {
            int diff = encoder_value - last_encoder_value;
            // Move user paddle based on first encoder
            userPaddle.y = std::min(PADDLE_Y_MAX, std::max(PADDLE_Y_MIN, userPaddle.y - diff));
            last_encoder_value = encoder_value;
        }
        // Handle second encoder (e.g. "Rotary Encoder Right Position: %d")
        else if (sscanf(buffer, "Rotary Encoder Right Position: %d", &encoder_value) == 1) {
            int diff = encoder_value - last_encoder_value_right;
            // Move AI paddle (or second player paddle) based on second encoder
            // If you want the second encoder to control the left paddle:
            aiPaddle.y = std::min(PADDLE_Y_MAX, std::max(PADDLE_Y_MIN, aiPaddle.y - diff));
            last_encoder_value_right = encoder_value;
        } else {
            printf("Received unexpected data: %s\n", buffer);
        }
    } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }
}

// Main function
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

    int fifo_fd;
    int attempts = 10;
    while ((fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK)) == -1 && attempts > 0) {
        perror("Failed to open FIFO for reading");
        sleep(1);
        attempts--;
    }
    if (fifo_fd == -1) {
        fprintf(stderr, "Could not open FIFO after multiple attempts. Exiting.\n");
        return 1;
    }

    srand(static_cast<unsigned int>(time(0)));

    Paddle aiPaddle = {0, MATRIX_HEIGHT / 2 - PADDLE_HEIGHT / 2, PADDLE_WIDTH, PADDLE_HEIGHT, 1};
    Paddle userPaddle = {MATRIX_WIDTH - PADDLE_WIDTH, MATRIX_HEIGHT / 2 - PADDLE_HEIGHT / 2, PADDLE_WIDTH, PADDLE_HEIGHT, 1};
    Ball ball = {MATRIX_WIDTH / 2, MATRIX_HEIGHT / 2, BALL_SIZE, 1, 1};

    int last_encoder_value = 0;
    int last_encoder_value_right = 0;
    struct Color pixelMatrix[64][32];

    while (!interrupt_received) {
        readFromFIFO(fifo_fd, userPaddle, last_encoder_value, aiPaddle, last_encoder_value_right);

        // If you still want AI movement (comment out if both paddles are human-controlled)
        // aiMove(aiPaddle, ball);

        ball.x += ball.speedX;
        ball.y += ball.speedY;

        if (ball.y <= 0 || ball.y + BALL_SIZE >= MATRIX_HEIGHT) {
            ball.speedY *= -1;
        }

        if (checkCollision(ball, aiPaddle) || checkCollision(ball, userPaddle)) {
            ball.speedX *= -1;
        }

        if (ball.x <= 0) {
            rightScore++;
            flashGreenScreen(matrix, offscreen_canvas);
            resetGame(aiPaddle, userPaddle, ball);
        } else if (ball.x + BALL_SIZE >= MATRIX_WIDTH) {
            leftScore++;
            flashGreenScreen(matrix, offscreen_canvas);
            resetGame(aiPaddle, userPaddle, ball);
        }

        renderMatrix(pixelMatrix, aiPaddle, userPaddle, ball);
        outputMatrix(matrix, offscreen_canvas, pixelMatrix);
        usleep(20000);
    }

    led_matrix_delete(matrix);
    close(fifo_fd);

    return 0;
}
