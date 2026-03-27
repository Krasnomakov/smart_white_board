// This C++ program launches a Python script to capture thermal camera data,
// reads the processed data, and displays it on an RGB LED matrix.
// It is designed to work with the rpi-rgb-led-matrix library.
//
#include "led-matrix.h"
#include "graphics.h"
#include <Magick++.h>
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <stdio.h>
#include <sstream>
#include <sys/wait.h>

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;
using rgb_matrix::FrameCanvas;
using rgb_matrix::RuntimeOptions;

using namespace std;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// Launches the Python thermal camera script and reads pixel data from its stdout.
void DisplayThermalData(RGBMatrix *matrix) {
    const int thermal_image_width = 32;
    const int thermal_image_height = 32;

    // Command to execute the python script
    const char* cmd = "python3 thermal_cam_script.py 2>&1";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        cerr << "Could not open pipe to Python script." << endl;
        return;
    }

    FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
    int rendered_frames = 0;
    int non_frame_lines = 0;
    
    // Buffer to read one line of data from the python script
    char buffer[16384]; // 32*32*3 RGB values * up to 4 chars each (e.g. "255 ")

    while (!interrupt_received && fgets(buffer, sizeof(buffer), pipe) != NULL) {
        stringstream ss(buffer);
        vector<unsigned char> pixels;
        pixels.reserve(thermal_image_width * thermal_image_height * 3);
        int r, g, b;

        while (ss >> r >> g >> b) {
            pixels.push_back(r);
            pixels.push_back(g);
            pixels.push_back(b);
        }

        if (pixels.size() != thermal_image_width * thermal_image_height * 3) {
            if (non_frame_lines < 5) {
                cerr << "thermal_cam_script non-frame output: " << buffer;
            }
            ++non_frame_lines;
            // Skip if we didn't get a full frame of data
            continue;
        }

        Magick::Image image(thermal_image_width, thermal_image_height, "RGB", Magick::CharPixel, pixels.data());

        // Rotate the image 90 degrees
        image.rotate(90);

        // Reflect the image horizontally
        image.flop();

        // Scale and crop to fit the matrix
        double image_aspect = (double)image.columns() / image.rows();
        double matrix_aspect = (double)matrix->width() / matrix->height();
        int new_width, new_height;

        if (image_aspect > matrix_aspect) {
            new_height = matrix->height();
            new_width = (int)(image_aspect * new_height);
        } else {
            new_width = matrix->width();
            new_height = (int)(new_width / image_aspect);
        }
        
        image.scale(Magick::Geometry(new_width, new_height));
        image.crop(Magick::Geometry(matrix->width(), matrix->height(), (new_width - matrix->width()) / 2, (new_height - matrix->height()) / 2));

        for (size_t y = 0; y < image.rows(); ++y) {
            for (size_t x = 0; x < image.columns(); ++x) {
                const Magick::Color &c = image.pixelColor(x, y);
                if (c.alphaQuantum() < 256) {
                    offscreen_canvas->SetPixel(x, y, c.redQuantum() / 257, c.greenQuantum() / 257, c.blueQuantum() / 257);
                }
            }
        }
        offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
        ++rendered_frames;
    }

    int status = pclose(pipe);
    if (rendered_frames == 0) {
        cerr << "thermal_display: no frames rendered from thermal_cam_script." << endl;
    }
    if (rendered_frames == 0) {
        if (status == -1) {
            perror("thermal_display: pclose failed");
        } else if (WIFEXITED(status)) {
            cerr << "thermal_cam_script exited with code " << WEXITSTATUS(status) << endl;
        } else if (WIFSIGNALED(status)) {
            cerr << "thermal_cam_script terminated by signal " << WTERMSIG(status) << endl;
        }
    }
}


int main(int argc, char **argv) {
    Magick::InitializeMagick(*argv);

    RGBMatrix::Options options;
    RuntimeOptions rt_options;

    options.hardware_mapping = "adafruit-hat";
    options.cols = 64;
    options.rows = 32;
    options.chain_length = 1;
    options.panel_type = "FM6127";
    options.brightness = 100;

    rt_options.gpio_slowdown = 4;
    rt_options.drop_privileges = 0;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rt_options);
    if (matrix == NULL) {
        return 1;
    }

    DisplayThermalData(matrix);

    matrix->Clear();
    delete matrix;

    return 0;
} 
