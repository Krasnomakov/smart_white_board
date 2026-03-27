#include "led-matrix-c.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <cmath>
#include <algorithm>
#include <errno.h>

#define DATA_FIFO_PATH "/tmp/mode_fifo"

const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

struct RGB {
    unsigned char r, g, b;
};

// Wave parameters
float phase_shift = 0.0f;       // Controls horizontal wave movement
float speed = 0.05f;            // Speed of horizontal wave movement
float amplitude = 15.0f;        // Height of the wave
float wavelength = 0.1f;        // Frequency of the wave

// Fade parameters
struct RGB prevMatrix[64][32];  // Stores previous state for fading
float fade_speed = 0.9f;        // Controls fade speed

int last_encoder_value = 0;       // Tracks last value of first encoder
int last_encoder_value_right = 0; // Tracks last value of second encoder

// Wave types
enum WaveType { SIN_WAVE, COS_WAVE, TAN_WAVE, SQUARE_WAVE, SAWTOOTH_WAVE, TRIANGLE_WAVE };
WaveType current_wave_type = SIN_WAVE;

// Base wave color
RGB wave_color = {255, 255, 255};

// Signal handling
volatile bool interrupt_received = false;

static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Wave functions
float getSineWave(float x) {
    return std::sin(x);
}

float getCosineWave(float x) {
    return std::cos(x);
}

float getTangentWave(float x) {
    float val = std::tan(x);
    // Clip to avoid extreme values
    if (val > 1.0f) val = 1.0f;
    if (val < -1.0f) val = -1.0f;
    return val;
}

float getSquareWave(float x) {
    return (std::sin(x) > 0) ? 1.0f : -1.0f;
}

float getSawtoothWave(float x) {
    // Normalize x to range
    float frac = x / (2.0f * M_PI);
    frac = frac - std::floor(frac);
    return (frac * 2.0f) - 1.0f; 
}

float getTriangleWave(float x) {
    float frac = x / (2.0f * M_PI);
    frac = frac - std::floor(frac);
    // Triangle wave formula
    float val = 2.0f * frac;
    if (val > 1.0f) val = 2.0f - val;
    val = (val * 2.0f) - 1.0f;
    return val;
}

// Fade function
RGB fadeColor(RGB color, float factor) {
    return {
        static_cast<unsigned char>(color.r * factor),
        static_cast<unsigned char>(color.g * factor),
        static_cast<unsigned char>(color.b * factor)
    };
}

// Generate the wave pattern
void generateWave(struct RGB pixelMatrix[64][32]) {
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        float wave_value;
        float wave_phase = (x + phase_shift) * wavelength;

        // Select wave function
        switch (current_wave_type) {
            case SIN_WAVE:
                wave_value = getSineWave(wave_phase);
                break;
            case COS_WAVE:
                wave_value = getCosineWave(wave_phase);
                break;
            case TAN_WAVE:
                wave_value = getTangentWave(wave_phase);
                break;
            case SQUARE_WAVE:
                wave_value = getSquareWave(wave_phase);
                break;
            case SAWTOOTH_WAVE:
                wave_value = getSawtoothWave(wave_phase);
                break;
            case TRIANGLE_WAVE:
                wave_value = getTriangleWave(wave_phase);
                break;
        }

        int y = static_cast<int>((wave_value + 1.0f) * 0.5f * amplitude + (MATRIX_HEIGHT - amplitude) / 2.0f);
        // Constrain y
        if (y < 0) y = 0;
        if (y >= MATRIX_HEIGHT) y = MATRIX_HEIGHT - 1;

        for (int row = 0; row < MATRIX_HEIGHT; ++row) {
            if (row == y) {
                // Color gradient: Red at top, Blue at bottom
                wave_color.r = static_cast<unsigned char>(255 * (1.0f - (float)row / (MATRIX_HEIGHT - 1)));
                wave_color.g = 0;
                wave_color.b = static_cast<unsigned char>(255 * ((float)row / (MATRIX_HEIGHT - 1)));
                pixelMatrix[x][row] = wave_color;
            } else {
                pixelMatrix[x][row] = fadeColor(prevMatrix[x][row], fade_speed);
            }
        }
    }

    memcpy(prevMatrix, pixelMatrix, sizeof(struct RGB) * MATRIX_WIDTH * MATRIX_HEIGHT);
}

// Output the matrix to the LED display
void outputMatrix(struct RGBLedMatrix *matrix, struct LedCanvas *offscreen_canvas, struct RGB pixelMatrix[64][32]) {
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

// Attempt to open FIFO
int setupFIFO() {
    int fifo_fd;
    int attempts = 10;
    while ((fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK)) == -1 && attempts > 0) {
        perror("Failed to open FIFO for reading");
        sleep(1);
        attempts--;
    }
    if (fifo_fd == -1) {
        fprintf(stderr, "Could not open FIFO after multiple attempts.\n");
    }
    return fifo_fd;
}

// Read from FIFO, handle two encoders: 
// First encoder adjusts speed/amplitude/wavelength
// Second encoder changes wave type
void readFromFIFO(int fifo_fd) {
    if (fifo_fd == -1) return;

    char buffer[256];
    int bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        // Try parsing first encoder
        int encoder_value;
        if (sscanf(buffer, "Rotary Encoder Position: %d", &encoder_value) == 1) {
            int diff = encoder_value - last_encoder_value;
            // Adjust parameters based on first encoder
            speed = std::min(std::max(0.01f, speed + diff * 0.005f), 1.0f);
            amplitude = std::min(std::max(5.0f, amplitude + diff * 0.5f), 31.0f);
            wavelength = std::min(std::max(0.05f, wavelength + diff * 0.005f), 1.0f);
            last_encoder_value = encoder_value;
            printf("Encoder1: %d, Speed: %f, Amplitude: %f, Wavelength: %f\n", encoder_value, speed, amplitude, wavelength);
        }
        // Try parsing second encoder
        else if (sscanf(buffer, "Rotary Encoder Right Position: %d", &encoder_value) == 1) {
            int diff = encoder_value - last_encoder_value_right;
            // Adjust wave type based on second encoder
            // If diff > 0, increment wave type; if diff < 0, decrement wave type
            if (diff > 0) {
                current_wave_type = static_cast<WaveType>((current_wave_type + 1) % 6);
            } else if (diff < 0) {
                current_wave_type = static_cast<WaveType>((current_wave_type - 1 + 6) % 6);
            }
            last_encoder_value_right = encoder_value;
            printf("Encoder2: %d, Current Wave Type: %d\n", encoder_value, current_wave_type);
        } else {
            printf("Received unexpected data: %s\n", buffer);
        }
    } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }
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

    int fifo_fd = setupFIFO();
    if (fifo_fd == -1) {
        return 1;
    }

    // Set matrix flags
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
    led_canvas_get_size(offscreen_canvas, &width, &height);

    struct RGB pixelMatrix[64][32];
    // Initialize prevMatrix
    for (int x = 0; x < MATRIX_WIDTH; x++) {
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            prevMatrix[x][y] = {0,0,0};
        }
    }

    while (!interrupt_received) {
        readFromFIFO(fifo_fd);

        generateWave(pixelMatrix);
        outputMatrix(matrix, offscreen_canvas, pixelMatrix);

        phase_shift += speed;
        usleep(20000);
    }

    led_matrix_delete(matrix);
    close(fifo_fd);
    return 0;
}
