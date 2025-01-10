#include "led-matrix.h"
#include "graphics.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <memory>
#include <signal.h>
#include <sys/wait.h>
#include <mutex>
#include <thread>

using namespace rgb_matrix;

volatile bool interrupt_received = false;
pid_t child_pid = -1;

// Mutex for updating words dynamically
std::mutex words_mutex;
std::vector<std::string> current_words = {"Loading..."};  // Default placeholder text
std::vector<std::string> pending_words;  // Holds incoming sentences

static void InterruptHandler(int signo) {
  interrupt_received = true;
  if (child_pid > 0) {
    kill(child_pid, SIGTERM);
  }
}

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s [options]\n", progname);
  fprintf(stderr, "Reads text from a Python script and displays it with scrolling.\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr,
          "\t-f <font-file>    : Use given font.\n"
          "\t-x <x-origin>     : X-Origin of displaying text (Default: 0)\n"
          "\t-S <spacing>      : Spacing pixels between letters (Default: 0)\n"
          "\t-C <r,g,b>        : Color. Default 255,255,0\n"
          "\t-B <r,g,b>        : Font Background-Color. Default 0,0,0\n"
          "\t-s <speed>        : Scrolling speed in pixels per frame (Default: 1)\n"
          "\n");
  rgb_matrix::PrintMatrixFlags(stderr);
  return 1;
}

static bool parseColor(Color *c, const char *str) {
  return sscanf(str, "%hhu,%hhu,%hhu", &c->r, &c->g, &c->b) == 3;
}

// Helper function to split a sentence into words
std::vector<std::string> SplitWords(const std::string &sentence) {
  std::vector<std::string> words;
  std::istringstream stream(sentence);
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  return words;
}

// Thread to continuously read from the script and update pending words
void ReadSentences(FILE *pipe_stream) {
  char line[1024];
  while (fgets(line, sizeof(line), pipe_stream) != nullptr && !interrupt_received) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

    std::vector<std::string> new_words = SplitWords(line);

    if (!new_words.empty()) {
      std::lock_guard<std::mutex> lock(words_mutex);
      pending_words = std::move(new_words);  // Update pending words
    }
  }
}

int main(int argc, char *argv[]) {
  RGBMatrix::Options matrix_options;
  rgb_matrix::RuntimeOptions runtime_opt;

  matrix_options.hardware_mapping = "adafruit-hat";
  matrix_options.cols = 64;
  matrix_options.rows = 32;
  matrix_options.chain_length = 1;
  matrix_options.panel_type = "FM6127";
  matrix_options.brightness = 50;

  runtime_opt.gpio_slowdown = 4;

  if (!rgb_matrix::ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
    return usage(argv[0]);
  }

  Color color(255, 255, 0);
  Color bg_color(0, 0, 0);
  const char *bdf_font_file = NULL;
  int x_orig = 0;
  int letter_spacing = 0;
  int speed = 1;

  int opt;
  while ((opt = getopt(argc, argv, "x:f:C:B:S:s:")) != -1) {
    switch (opt) {
      case 'x': x_orig = atoi(optarg); break;
      case 'f': bdf_font_file = strdup(optarg); break;
      case 'S': letter_spacing = atoi(optarg); break;
      case 's': speed = atoi(optarg); break;
      case 'C':
        if (!parseColor(&color, optarg)) {
          fprintf(stderr, "Invalid color spec: %s\n", optarg);
          return usage(argv[0]);
        }
        break;
      case 'B':
        if (!parseColor(&bg_color, optarg)) {
          fprintf(stderr, "Invalid background color spec: %s\n", optarg);
          return usage(argv[0]);
        }
        break;
      default:
        return usage(argv[0]);
    }
  }

  if (bdf_font_file == NULL) {
    fprintf(stderr, "Need to specify BDF font-file with -f\n");
    return usage(argv[0]);
  }

  rgb_matrix::Font font;
  if (!font.LoadFont(bdf_font_file)) {
    fprintf(stderr, "Couldn't load font '%s'\n", bdf_font_file);
    return 1;
  }

  RGBMatrix *canvas = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
  if (canvas == NULL) return 1;

  signal(SIGTERM, InterruptHandler);
  signal(SIGINT, InterruptHandler);

  FrameCanvas *offscreen_canvas = canvas->CreateFrameCanvas();
  int canvas_height = canvas->height();

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    perror("pipe");
    return 1;
  }

  child_pid = fork();
  if (child_pid == -1) {
    perror("fork");
    return 1;
  }

  if (child_pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    const char* python_command = "/home/pi/venv/bin/python";
    const char* script_path = "/home/pi/Documents/rpi-rgb-led-matrix/examples-api-use/ollama_no_input.py";
    execlp(python_command, python_command, script_path, (char*)NULL);

    perror("execlp");
    exit(1);
  } else {
    close(pipefd[1]);
  }

  FILE* pipe_stream = fdopen(pipefd[0], "r");
  if (!pipe_stream) {
    perror("fdopen");
    return 1;
  }

  // Start the thread to read sentences from the script
  std::thread reader_thread(ReadSentences, pipe_stream);

  int y = canvas_height;  // Initial y-coordinate for the entire sentence.

  while (!interrupt_received) {
    offscreen_canvas->Fill(bg_color.r, bg_color.g, bg_color.b);

    // Safely update the current words
    {
      std::lock_guard<std::mutex> lock(words_mutex);
      if (!pending_words.empty()) {
        current_words = std::move(pending_words);  // Update current words
        y = canvas_height;  // Reset scrolling position
      }
    }

    // Draw all words in the sentence, spaced by font height
    int current_y = y;
    for (const auto &word : current_words) {
      rgb_matrix::DrawText(offscreen_canvas, font, x_orig, current_y + font.baseline(),
                           color, NULL, word.c_str(), letter_spacing);
      current_y += font.height();  // Move down for the next word.
    }

    // Update y position
    y -= speed;

    // Reset y when the entire sentence is out of view
    if (y + (current_words.size() * font.height()) <= 0) {
      y = canvas_height;  // Restart from the bottom.
    }

    // Swap buffers for the next frame
    offscreen_canvas = canvas->SwapOnVSync(offscreen_canvas);
    usleep(50000);  // Adjust frame rate for smooth scrolling
  }

  // Clean up
  reader_thread.join();
  if (child_pid > 0) {
    int status;
    kill(child_pid, SIGTERM);
    waitpid(child_pid, &status, 0);
  }

  delete canvas;
  return 0;
}
