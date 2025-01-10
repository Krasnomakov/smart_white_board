//This script places the image to fill all the matrix. 

#include "../include/led-matrix.h"
#include <Magick++.h>
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>  // For sleep() and stat()

std::string GetCurrentDate() {
    time_t t = time(0);
    struct tm *now = localtime(&t);
    char buffer[11]; // Format: YYYY-MM-DD
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", now);
    return std::string(buffer);
}

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;
using rgb_matrix::FrameCanvas;
using rgb_matrix::Color;
using namespace std;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Struct to hold configuration flags
struct Config {
    int brightness = 100;           // Default brightness
    int update_interval = 1000;     // Default update interval in seconds
};

// Function prototypes
void SleepInterruptible(int total_seconds);

// Load and scale the image to fully fill the matrix
using ImageVector = std::vector<Magick::Image>;

static ImageVector LoadImageAndScaleImage(const char *filename, int target_width, int target_height) {
    ImageVector result;
    ImageVector frames;

    try {
        readImages(&frames, filename);
    } catch (std::exception &e) {
        if (e.what())
            fprintf(stderr, "%s\n", e.what());
        return result;
    }

    if (frames.empty()) {
        fprintf(stderr, "No image found.");
        return result;
    }

    if (frames.size() > 1) {
        Magick::coalesceImages(&result, frames.begin(), frames.end());
    } else {
        result.push_back(frames[0]);
    }

    for (Magick::Image &image : result) {
        double image_aspect_ratio = static_cast<double>(image.columns()) / image.rows();
        double target_aspect_ratio = static_cast<double>(target_width) / target_height;

        int new_width, new_height;
        if (image_aspect_ratio > target_aspect_ratio) {
            new_height = target_height;
            new_width = static_cast<int>(image_aspect_ratio * target_height);
        } else {
            new_width = target_width;
            new_height = static_cast<int>(target_width / image_aspect_ratio);
        }

        image.scale(Magick::Geometry(new_width, new_height));
        image.crop(Magick::Geometry(target_width, target_height, (new_width - target_width) / 2, (new_height - target_height) / 2));
    }

    return result;
}

void CopyImageToCanvas(const Magick::Image &image, Canvas *canvas) {
    for (size_t y = 0; y < image.rows(); ++y) {
        for (size_t x = 0; x < image.columns(); ++x) {
            const Magick::Color &c = image.pixelColor(x, y);
            if (c.alphaQuantum() < 256) {
                canvas->SetPixel(x, y,
                                 ScaleQuantumToChar(c.redQuantum()),
                                 ScaleQuantumToChar(c.greenQuantum()),
                                 ScaleQuantumToChar(c.blueQuantum()));
            }
        }
    }
}

void ShowImage(RGBMatrix *matrix, const std::string &image_file) {
    FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
    int matrix_height = matrix->height();
    int matrix_width = matrix->width();

    ImageVector images = LoadImageAndScaleImage(image_file.c_str(), matrix_width, matrix_height);
    matrix->Clear();

    if (!images.empty()) {
        CopyImageToCanvas(images[0], offscreen_canvas);
    }

    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
}

void DisplayImageLoop(const Config &config, RGBMatrix *matrix) {
    std::string image_file = "nasa_image_" + GetCurrentDate() + ".png";

    // Wait until the image file exists
    while (!interrupt_received) {
        struct ::stat buffer;
        if (::stat(image_file.c_str(), &buffer) == 0) {
            // File exists, proceed to display
            break;
        } else {
            // File doesn't exist yet, wait for a short period
            sleep(1);  // Wait for 1 second
        }
    }

    // Now display the image
    while (!interrupt_received) {
        ShowImage(matrix, image_file);
        SleepInterruptible(config.update_interval);
    }
}

void SleepInterruptible(int total_seconds) {
    int slept = 0;
    while (slept < total_seconds && !interrupt_received) {
        sleep(1);
        slept += 1;
    }
}

int main(int argc, char **argv) {
    Magick::InitializeMagick(*argv);

    Config config;

    rgb_matrix::RGBMatrix::Options options = {};
    rgb_matrix::RuntimeOptions rt_options = {};

    options.hardware_mapping = "adafruit-hat";
    options.cols = 64;
    options.rows = 32;
    options.chain_length = 1;
    options.panel_type = "FM6127";
    options.brightness = config.brightness;

    rt_options.gpio_slowdown = 4;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    RGBMatrix *matrix = rgb_matrix::RGBMatrix::CreateFromOptions(options, rt_options);
    if (matrix == NULL) {
        return 1;
    }

    DisplayImageLoop(config, matrix);

    matrix->Clear();
    delete matrix;

    return 0;
}
