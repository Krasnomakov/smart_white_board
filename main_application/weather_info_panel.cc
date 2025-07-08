#include "led-matrix.h"
#include "graphics.h"    // For fonts and graphics
#include <Magick++.h>               // Include Magick++ header
#include <vector>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <algorithm>                // For std::remove

using namespace rgb_matrix;
using namespace std;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

// Struct to hold configuration flags
struct Config {
    std::string csv_file;
    std::string font_file;
    int brightness = 50;           // Default brightness
    int update_interval = 10;      // Default update interval in seconds
};

// Helper function to write the downloaded image data to a file
size_t WriteCallback(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fwrite(ptr, size, nmemb, stream);
}

// Function to download an image using libcurl
bool DownloadImage(const std::string &url, const std::string &output_file) {
  CURL *curl;
  FILE *fp;
  CURLcode res;
  curl = curl_easy_init();
  if (curl) {
    fp = fopen(output_file.c_str(), "wb");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);
    return (res == CURLE_OK);
  }
  return false;
}

// Helper function to trim leading/trailing whitespace from strings
std::string Trim(const std::string &str) {
  size_t first = str.find_first_not_of(' ');
  if (first == std::string::npos)
    return "";
  size_t last = str.find_last_not_of(' ');
  return str.substr(first, last - first + 1);
}

// CSV parser to handle comma-separated values, including quoted fields
std::vector<std::string> ParseCSVLine(const std::string &line) {
  std::vector<std::string> tokens;
  std::stringstream ss(line);
  std::string token;
  bool in_quotes = false;

  while (std::getline(ss, token, ',')) {
    if (in_quotes) {
      tokens.back() += ',' + token;  // append the token to the previous field
    } else {
      tokens.push_back(token);
    }

    // Toggle in_quotes if there is an odd number of double quotes in the token
    in_quotes = std::count(token.begin(), token.end(), '"') % 2 == 1;
  }

  // Remove quotes from quoted tokens
  for (auto &tok : tokens) {
    tok.erase(std::remove(tok.begin(), tok.end(), '"'), tok.end());
  }

  return tokens;
}

// Image loading functions
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
    image.scale(Magick::Geometry(target_width, target_height));
  }

  return result;
}

void CopyImageToCanvas(const Magick::Image &image, Canvas *canvas, int offset_x = 0, int offset_y = 0) {
  for (size_t y = 0; y < image.rows(); ++y) {
    for (size_t x = 0; x < image.columns(); ++x) {
      const Magick::Color &c = image.pixelColor(x, y);
      if (c.alphaQuantum() < 256) {
        canvas->SetPixel(x + offset_x, y + offset_y,
                         ScaleQuantumToChar(c.redQuantum()),
                         ScaleQuantumToChar(c.greenQuantum()),
                         ScaleQuantumToChar(c.blueQuantum()));
      }
    }
  }
}

// Function to display the image and temperature on the matrix
// Function to display the clock, image, and temperature on the matrix
void ShowImageAndTemperature(RGBMatrix *matrix, const std::string &image_file, const std::string &temperature, const Config &config) {
    FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();

    // Load image
    int image_width = 30;  // Width for small image like weather icon
    int matrix_height = matrix->height();
    ImageVector images = LoadImageAndScaleImage(image_file.c_str(), image_width, matrix_height);

    // Clear the canvas first
    matrix->Clear();
    
    // Display the small image on the left
    if (!images.empty()) {
        CopyImageToCanvas(images[0], offscreen_canvas, 2, 1);  // Offset slightly to center vertically
    }

    // Display clock and temperature
    Font font;
    if (!font.LoadFont(config.font_file.c_str())) {
        fprintf(stderr, "Couldn't load font '%s'\n", config.font_file.c_str());
        return;
    }

    Color text_color(255, 255, 0);  // Yellow color for text
    Color bg_color(0, 0, 0);        // Black background

    // Get current time for the clock
    char time_buffer[64];
    time_t current_time = time(NULL);
    struct tm *tm_info = localtime(&current_time);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M", tm_info);

    int y_clock = 8;  // Y-position for clock (above the temperature)
    int x_clock = image_width + 2;  // Right after the image

    // Draw clock text above the temperature
    DrawText(offscreen_canvas, font, x_clock, y_clock + font.baseline(), text_color, &bg_color, time_buffer, 0);

    // Adjust the Y-position for the temperature
    int y_temperature = y_clock + font.height() + 4;  // Move temperature down slightly
    const int x_temperature = image_width + 2;  // Right after the image

    // Draw temperature text below the clock
    DrawText(offscreen_canvas, font, x_temperature, y_temperature + font.baseline(), text_color, &bg_color, temperature.c_str(), 0);

    // Swap the offscreen canvas to display clock, image, and temperature
    offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
}


// Function to perform interruptible sleep
void SleepInterruptible(int total_seconds) {
    const int sleep_step = 1; // Sleep in 1-second increments
    int slept = 0;
    while (slept < total_seconds && !interrupt_received) {
        sleep(1);
        slept += 1;
    }
}

// Function to read the CSV file and extract image URL and temperature
// Function to read the CSV file and extract image URL and temperature from the last line
void ProcessCSVAndDisplay(const Config &config, RGBMatrix *matrix) {
    while (!interrupt_received) {
        std::ifstream file(config.csv_file);
        if (!file.is_open()) {
            std::cerr << "Failed to open CSV file: " << config.csv_file << std::endl;
            SleepInterruptible(config.update_interval);
            continue;
        }

        std::string line;
        std::getline(file, line); // Read the header to find the index of 'Image'

        // Find the column index for the "Image" and "Temperature" columns
        std::vector<std::string> header_tokens = ParseCSVLine(line);
        int image_column = -1;
        int temperature_column = -1;

        for (size_t i = 0; i < header_tokens.size(); ++i) {
            if (header_tokens[i] == "Image") {
                image_column = i;
            } else if (header_tokens[i] == "Temperature") {
                temperature_column = i;
            }
        }

        if (image_column == -1 || temperature_column == -1) {
            std::cerr << "Failed to find 'Image' or 'Temperature' columns in the CSV header" << std::endl;
            file.close();
            SleepInterruptible(config.update_interval);
            continue;
        }

        // Move to the last line of the CSV
        std::string last_line;
        while (std::getline(file, line)) {
            last_line = line;  // Keep updating until we reach the last line
        }
        file.close();

        if (last_line.empty()) {
            std::cerr << "No data found in CSV file." << std::endl;
            SleepInterruptible(config.update_interval);
            continue;
        }

        // Parse the last line
        std::vector<std::string> tokens = ParseCSVLine(last_line);
        if (tokens.size() <= static_cast<size_t>(std::max(image_column, temperature_column))) {
            std::cerr << "Malformed CSV line: " << last_line << std::endl;
            SleepInterruptible(config.update_interval);
            continue;
        }

        // Extract the image URL and temperature from the last line
        std::string temperature = tokens[temperature_column];
        std::string image_url = tokens[image_column];

        // Trim whitespace from the image URL
        image_url = Trim(image_url);

        // Debug: print extracted image URL
        std::cout << "Extracted image URL: " << image_url << std::endl;

        // Download the image
        std::string output_image_file = "/tmp/current_image.png";
        if (!DownloadImage(image_url, output_image_file)) {
            fprintf(stderr, "Failed to download image from URL: %s\n", image_url.c_str());
            continue;
        }

        // Display image and temperature on the matrix
        ShowImageAndTemperature(matrix, output_image_file, temperature, config);

        // Sleep for a while before updating
        SleepInterruptible(config.update_interval);  // Adjust the sleep interval as necessary
        if (interrupt_received) break;

        // Sleep before re-reading the CSV file
        SleepInterruptible(config.update_interval);
    }
}


int main(int argc, char **argv) {
    // Initialize the Magick++ library for image processing
    Magick::InitializeMagick(*argv);

    // Configuration struct for user-defined settings
    Config config;
    config.font_file = "6x9.bdf";  // Default font
    config.csv_file = "weather_data.csv";  // Default CSV file

    // Matrix configuration options
    RGBMatrix::Options options = {};
    RuntimeOptions rt_options = {};

    // Mandatory flag configuration
    options.hardware_mapping = "adafruit-hat";  // Hardware mapping specific to the matrix setup
    options.cols = 64;                          // Number of columns in the LED panel
    options.rows = 32;                          // Number of rows in the LED panel
    options.chain_length = 1;                   // Number of daisy-chained panels
    options.panel_type = "FM6127";              // Type of panel, specific to FM6127 controller
    options.brightness = config.brightness;     // Set the initial brightness (0-100)

    // Runtime options, for example GPIO slowdown
    rt_options.gpio_slowdown = 4;               // GPIO slowdown for stability

    // Signal handlers for graceful termination (Ctrl+C)
    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    // Create and configure the RGB LED matrix based on options
    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rt_options);
    if (matrix == NULL) {
        return 1;  // If matrix creation fails, exit with an error
    }

    // Process the CSV file and display weather information (image + temperature) on the matrix
    ProcessCSVAndDisplay(config, matrix);

    // Clean up resources and turn off the matrix display on exit
    matrix->Clear();
    delete matrix;

    return 0;
}
