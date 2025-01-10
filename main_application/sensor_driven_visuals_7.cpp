// Include necessary libraries
#include "../include/led-matrix-c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <vector>
#include <ctime>
#include <string>
#include <pthread.h>
#include <mutex>

#define DATA_FIFO_PATH "/tmp/mode_fifo" // FIFO path

// Constants for matrix size
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;

// Define the RGB struct
struct RGB {
    unsigned char r, g, b;
};

// Class to represent a single blob
class Blob {
public:
    int x, y;               // Position of the blob
    float current_size;      // Smooth size transition
    float target_size;       // Target size for smooth transition
    float velocity_x, velocity_y; // Velocity of the blob (for movement)
    RGB color;              // Color of the blob
    float alpha;            // Opacity value (0-255 for fading effect)
    bool fading_out;        // To track if the blob is fading out

    Blob(int init_x, int init_y, int init_size, RGB init_color)
        : x(init_x), y(init_y), current_size(init_size), target_size(init_size), color(init_color), alpha(0), fading_out(false) {
        velocity_x = ((float)(rand() % 100) / 100.0f) - 0.5f;
        velocity_y = ((float)(rand() % 100) / 100.0f) - 0.5f;
    }

    void updateSize(float smoothing_factor) {
        current_size += (target_size - current_size) * smoothing_factor;
    }

    void move(int speed) {
        if (speed > 0) {
            x += (int)(velocity_x * speed);
            y += (int)(velocity_y * speed);
        }

        if (x < 0 || x >= MATRIX_WIDTH) velocity_x *= -1;
        if (y < 0 || y >= MATRIX_HEIGHT) velocity_y *= -1;
    }

    void updateAlpha() {
        if (!fading_out) {
            alpha = fmin(alpha + 10, 255);
        } else {
            alpha = fmax(alpha - 10, 0);
        }
    }
};

// Function Prototypes
RGB generateDynamicColor(float als_value);
void readFIFO(int fifo_fd, float &sound_magnitude, float &als_value, int &rotary_position, float &temperature, float &humidity);
void generateBlobs(float sound_magnitude, float als_value, float temperature);
void updateMatrix(struct RGBLedMatrix *matrix, struct LedCanvas *offscreen_canvas, int speed);

// Shared variables
std::vector<Blob> blobs;
std::mutex data_mutex;
float sound_magnitude = 0;
float als_value = 0;
int rotary_position = 3;
float temperature = 0;
float humidity = 0;

// Function Definitions
RGB generateDynamicColor(float als_value) {
    float normalized_als = fminf(als_value / 10000.0f, 1.0f);
    float hue = (1.0f - normalized_als) * 240.0f;
    float saturation = 1.0f;
    float value = 1.0f;

    float c = value * saturation;
    float x = c * (1 - fabs(fmod(hue / 60.0f, 2) - 1));
    float m = value - c;
    float r_prime, g_prime, b_prime;

    if (hue >= 0 && hue < 60) {
        r_prime = c;
        g_prime = x;
        b_prime = 0;
    } else if (hue >= 60 && hue < 120) {
        r_prime = x;
        g_prime = c;
        b_prime = 0;
    } else if (hue >= 120 && hue < 180) {
        r_prime = 0;
        g_prime = c;
        b_prime = x;
    } else if (hue >= 180 && hue < 240) {
        r_prime = 0;
        g_prime = x;
        b_prime = c;
    } else {
        r_prime = c;
        g_prime = 0;
        b_prime = x;
    }

    RGB color;
    color.r = (unsigned char)((r_prime + m) * 255);
    color.g = (unsigned char)((g_prime + m) * 255);
    color.b = (unsigned char)((b_prime + m) * 255);
    return color;
}

void readFIFO(int fifo_fd, float &sound_magnitude, float &als_value, int &rotary_position, float &temperature, float &humidity) {
    char buffer[512];
    int bytes_read;

    while ((bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';

        std::string data(buffer);
        printf("Raw FIFO Data: %s\n", data.c_str()); // Log raw FIFO data for debugging

        // Parse for "Temperature:"
        size_t temp_pos = data.find("Temperature:");
        if (temp_pos != std::string::npos) {
            size_t end_pos = data.find("C", temp_pos);
            if (end_pos != std::string::npos) {
                temperature = atof(data.substr(temp_pos + 12, end_pos - (temp_pos + 12)).c_str());
            }
        }

        // Parse for "Humidity:"
        size_t hum_pos = data.find("Humidity:");
        if (hum_pos != std::string::npos) {
            size_t end_pos = data.find("%", hum_pos);
            if (end_pos != std::string::npos) {
                humidity = atof(data.substr(hum_pos + 9, end_pos - (hum_pos + 9)).c_str());
            }
        }

        // Parse for "ALS:"
        size_t als_pos = data.find("ALS:");
        if (als_pos != std::string::npos) {
            size_t end_pos = data.find("lx", als_pos);
            if (end_pos != std::string::npos) {
                als_value = atof(data.substr(als_pos + 4, end_pos - (als_pos + 4)).c_str());
            }
        }

        // Parse for "Sound Magnitude:"
        size_t sound_pos = data.find("Sound Magnitude:");
        if (sound_pos != std::string::npos) {
            sound_magnitude = atof(data.substr(sound_pos + 17).c_str());
        }

        // Parse for "Rotary Encoder Position:"
        size_t rotary_pos = data.find("Rotary Encoder Position:");
        if (rotary_pos != std::string::npos) {
            rotary_position = atoi(data.substr(rotary_pos + 25).c_str());
        }

        // Log parsed values for verification
        printf("Parsed Data - Sound: %.2f, ALS: %.2f, Temp: %.2f, Humidity: %.2f, Rotary: %d\n",
               sound_magnitude, als_value, temperature, humidity, rotary_position);
    }

    if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }
}



void generateBlobs(float sound_magnitude, float als_value, float temperature) {
    int num_blobs = (int)(sound_magnitude / 10.0f);
    float target_size = fmax(1, fmin(temperature - 20, 10));

    printf("Generating blobs: sound=%.2f, als=%.2f, temp=%.2f\n", sound_magnitude, als_value, temperature);

    if (blobs.size() > num_blobs) {
        for (size_t i = num_blobs; i < blobs.size(); ++i) {
            blobs[i].fading_out = true;
        }
    }

    while (blobs.size() < num_blobs) {
        int x = rand() % MATRIX_WIDTH;
        int y = rand() % MATRIX_HEIGHT;
        RGB color = generateDynamicColor(als_value);
        blobs.emplace_back(x, y, target_size, color);
        printf("New blob: x=%d, y=%d, size=%.2f\n", x, y, target_size);
    }

    for (auto &blob : blobs) {
        blob.target_size = target_size;
        blob.updateSize(0.1f);
        blob.color = generateDynamicColor(als_value);
        printf("Blob updated: x=%d, y=%d, size=%.2f, alpha=%.2f\n",
               blob.x, blob.y, blob.current_size, blob.alpha);
    }
}

void updateMatrix(struct RGBLedMatrix *matrix, struct LedCanvas *offscreen_canvas, int speed) {
    int width, height;
    led_canvas_get_size(offscreen_canvas, &width, &height);
    led_canvas_clear(offscreen_canvas);

    for (auto it = blobs.begin(); it != blobs.end();) {
        Blob &blob = *it;

        blob.move(speed);
        blob.updateAlpha();

        if (blob.fading_out && blob.alpha == 0) {
            it = blobs.erase(it);
            continue;
        }

        for (int dx = -blob.current_size / 2; dx <= blob.current_size / 2; ++dx) {
            for (int dy = -blob.current_size / 2; dy <= blob.current_size / 2; ++dy) {
                int draw_x = blob.x + dx;
                int draw_y = blob.y + dy;
                if (draw_x >= 0 && draw_x < MATRIX_WIDTH && draw_y >= 0 && draw_y < MATRIX_HEIGHT) {
                    int r = (blob.color.r * blob.alpha) / 255;
                    int g = (blob.color.g * blob.alpha) / 255;
                    int b = (blob.color.b * blob.alpha) / 255;
                    led_canvas_set_pixel(offscreen_canvas, draw_x, draw_y, r, g, b);
                }
            }
        }
        ++it;
    }

    led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

void *fifo_thread_function(void *arg) {
    int fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fifo_fd == -1) {
        perror("Failed to open FIFO");
        return nullptr;
    }

    while (1) {
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            readFIFO(fifo_fd, sound_magnitude, als_value, rotary_position, temperature, humidity);
        }
        usleep(50000);
    }
    close(fifo_fd);
    return nullptr;
}

void *matrix_thread_function(void *arg) {
    struct RGBLedMatrix *matrix = (RGBLedMatrix *)arg;
    struct LedCanvas *offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

    while (1) {
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            generateBlobs(sound_magnitude, als_value, temperature);
        }
        updateMatrix(matrix, offscreen_canvas, rotary_position);
        usleep(20000);
    }
    return nullptr;
}

int main(int argc, char **argv) {
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    memset(&options, 0, sizeof(options));
    memset(&rt_options, 0, sizeof(rt_options));
    options.hardware_mapping = "adafruit-hat";
    options.cols = 64;
    options.rows = 32;
    options.chain_length = 1;
    options.panel_type = "FM6127";
    options.brightness = 50;
    rt_options.gpio_slowdown = 4;

    RGBLedMatrix *matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) return 1;

    pthread_t fifo_thread, matrix_thread;

    if (pthread_create(&fifo_thread, NULL, fifo_thread_function, NULL)) return 1;
    if (pthread_create(&matrix_thread, NULL, matrix_thread_function, (void *)matrix)) return 1;

    pthread_join(fifo_thread, NULL);
    pthread_join(matrix_thread, NULL);

    led_matrix_delete(matrix);
    return 0;
}
