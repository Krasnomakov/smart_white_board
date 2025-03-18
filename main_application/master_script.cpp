#include </home/pi/WiringPi/wiringPi/wiringPi.h>  // For GPIO access
#include "../include/led-matrix.h"
#include "../include/graphics.h"
#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <ctime>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>   // for rand()
#include <fcntl.h>   // ADDED for FIFO open
#include <errno.h>   // ADDED for error handling (EAGAIN, EWOULDBLOCK)
#include <stdio.h>   // ADDED for fprintf, perror, etc.
#include <string.h>  // ADDED for memset

using namespace rgb_matrix;

// -----------------------------------------------------------------------------
// Matrix size
// -----------------------------------------------------------------------------
const int MATRIX_WIDTH       = 64;
const int MATRIX_HEIGHT      = 32;
const int SECTION_WIDTH      = 48;
const int SIDE_COLUMN_WIDTH  = 16;
const int TOP_ROW_HEIGHT     = 8;
const int MID_ROW_HEIGHT     = 8;
const int BOTTOM_ROW_HEIGHT  = 16;

// -----------------------------------------------------------------------------
// Components for area selection
// -----------------------------------------------------------------------------
enum Component {
    TOP_LINE,
    MID_LINE,
    BOTTOM_LINE,
    SIDE_TOP_SQUARE,
    SIDE_BOTTOM_SQUARE,
    NUM_COMPONENTS // 5 total
};

volatile bool interrupt_received = false;
Component currentSelection = TOP_LINE;

// FIFO path
static const char* COMMAND_FIFO_PATH = "/tmp/uart_fifo";
static const char* DATA_FIFO_PATH = "/tmp/mode_fifo";
int last_encoder_value = 0;  // We'll track the last rotary-encoder value

// GPIO pins for your IR sensors
static const int SENSOR_TOP_PIN = 22;    // Affects top square
static const int SENSOR_BOTTOM_PIN = 27; // Affects bottom square

// -----------------------------------------------------------------------------
// For random 5-mode selection
// -----------------------------------------------------------------------------
static const int MAIN_MODE_INDEX = 17;  // or whichever index "main_mode_rotary" has in the master’s mode array
static const int TOTAL_MODES = 19;      // from your master code

// We'll store which "real mode" each of the 5 areas is assigned to
static int g_assignedModes[NUM_COMPONENTS];  // e.g. {2, 10, 5, 7, 0}, etc.

static int cmd_fd = -1;

void openCommandFIFOIfNeeded() {
    if (cmd_fd == -1) {
        cmd_fd = open(COMMAND_FIFO_PATH, O_WRONLY | O_NONBLOCK);
        if (cmd_fd == -1) {
            perror("Failed to open COMMAND_FIFO_PATH for writing");
            // Possibly return or handle error
        }
    }
}


// ADDED: a function to pick 5 random distinct modes (excluding MAIN_MODE_INDEX)
static void AssignRandomModes() {
    // Build a list of possible mode indices
    std::vector<int> available;
    available.reserve(TOTAL_MODES - 1);
    for (int i = 0; i < TOTAL_MODES; i++) {
        if (i != MAIN_MODE_INDEX) { 
            available.push_back(i);
        }
    }
    // Shuffle
    std::random_shuffle(available.begin(), available.end());

    // Take the first 5 for our areas
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        g_assignedModes[i] = available[i];
    }
    fprintf(stderr, "Assigned 5 random modes: ");
    for (int i = 0; i < NUM_COMPONENTS; i++) {
        fprintf(stderr, "%d ", g_assignedModes[i]);
    }
    fprintf(stderr, "\n");
}

// -----------------------------------------------------------------------------
// Signal handler
// -----------------------------------------------------------------------------
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

// -----------------------------------------------------------------------------
// Day-of-week, moon-phase, & other existing functions
// -----------------------------------------------------------------------------
int getCurrentDayOfWeek() {
    auto now      = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* localTime = std::localtime(&now_time);
    int day = localTime->tm_wday; 
    return (day + 6) % 7;
}

int getMoonPhase() {
    std::tm new_moon = {};
    new_moon.tm_year = 100;
    new_moon.tm_mon  = 0;
    new_moon.tm_mday = 6;
    std::time_t new_moon_time = std::mktime(&new_moon);
    std::time_t now           = std::time(nullptr);

    double days_since_new_moon = std::difftime(now, new_moon_time) / (24*3600);
    double synodic_month       = 29.530588853;
    double current_phase       = std::fmod(days_since_new_moon, synodic_month);
    int phase_index            = (int)((current_phase / synodic_month) * 8) % 8;
    return phase_index;
}

static uint8_t calculateOpacity(int currentPhase, int targetPhase, int totalPhases) {
    int distance = std::abs(targetPhase - currentPhase);
    if (distance > totalPhases / 2) {
        distance = totalPhases - distance;
    }
    double factor = 1.0 - (double)distance / (totalPhases/2.0);
    factor = std::max(0.0, std::min(factor, 1.0));
    return (uint8_t)(factor * 255);
}

// -----------------------------------------------------------------------------
// Draw a rectangle outline
// -----------------------------------------------------------------------------
void DrawRectangle(Canvas* canvas, int x0, int y0, int x1, int y1, const Color &color) {
    x0 = std::max(0, std::min(canvas->width()-1, x0));
    y0 = std::max(0, std::min(canvas->height()-1, y0));
    x1 = std::max(0, std::min(canvas->width()-1, x1));
    y1 = std::max(0, std::min(canvas->height()-1, y1));

    rgb_matrix::DrawLine(canvas, x0, y0, x1, y0, color);
    rgb_matrix::DrawLine(canvas, x1, y0, x1, y1, color);
    rgb_matrix::DrawLine(canvas, x1, y1, x0, y1, color);
    rgb_matrix::DrawLine(canvas, x0, y1, x0, y0, color);
}

void drawHighlightOverlay(Canvas* canvas, int x, int y, int width, int height) {
    Color highlight(255,255,255);
    DrawRectangle(canvas, x, y, x+width-1, y+height-1, highlight);
}

// -----------------------------------------------------------------------------
// Top row: day-of-week progress bar
// -----------------------------------------------------------------------------
void renderProgressBar(Canvas* canvas, Font &font) {
    // 1) Determine which day of the week is "current"
    int currentDay = getCurrentDayOfWeek();  
    // (e.g., Monday=0, Tuesday=1, Wednesday=2, Thursday=3, Friday=4, Saturday=5, Sunday=6, 
    //  but you re-map with (day + 6) % 7 in your getCurrentDayOfWeek())

    // 2) Define the colors for past, current, future days
    Color pastColor(153, 0, 153);
    Color currentColor(204, 0, 102);
    Color futureColor(255, 165, 0);

    // 3) The total width of the “day bar” is 48 (64 total minus 16 for side column).
    //    We'll split it into 7 segments, where 5 segments are 6 px, and 2 are 9 px.
    const int daysInWeek = 7;
    int totalBarWidth = (MATRIX_WIDTH - SIDE_COLUMN_WIDTH);

    // Exactly 7 segments: M=6, T=6, W=6, TH=9, F=6, S=6, SU=9 => 6+6+6+9+6+6+9 = 48
    static const int segmentWidths[daysInWeek] = {6, 6, 6, 9, 6, 6, 9};

    // 4) Fill each day block in order
    int currentX = 0; // left boundary for each day
    for (int i = 0; i < daysInWeek; i++) {
        // Decide the color based on whether it's in the past, current, or future
        Color color = (i < currentDay)
            ? pastColor
            : ((i == currentDay) ? currentColor : futureColor);

        // Segment width for day i
        int dayWidth = segmentWidths[i];
        int endX = currentX + dayWidth;  // right boundary (exclusive)

        // Fill that rectangular area for day i
        for (int x = currentX; x < endX; x++) {
            for (int y = 0; y < TOP_ROW_HEIGHT; y++) {
                canvas->SetPixel(x, y, color.r, color.g, color.b);
            }
        }

        // Move to the next day's starting x
        currentX = endX;
    }

    // 5) Days-of-week map: index -> label
    static const std::map<int, std::string> daysOfWeek = {
        {0, "M"},  {1, "T"},  {2, "W"}, 
        {3, "TH"}, {4, "F"},  {5, "S"}, 
        {6, "SU"}
    };

    // Retrieve the current day’s label
    std::string dayAbbrev = daysOfWeek.at(currentDay);

    // 6) Draw the text label for the current day
    Color textColor(0, 255, 0);
    rgb_matrix::DrawText(canvas, font,
                         1, TOP_ROW_HEIGHT - 1,  // Position near top-left
                         textColor, nullptr,
                         dayAbbrev.c_str(), 0);
}



// -----------------------------------------------------------------------------
// Middle row: moon phase bar with red pixel + opacity effect
// -----------------------------------------------------------------------------
void renderMoonPhase(Canvas* canvas) {
    int moonPhase = getMoonPhase();

    Color baseActive(255,255,0);
    Color baseInactive(76,0,153);

    const int phases = 8;
    int segmentWidth = SECTION_WIDTH / phases;
    int yStart = TOP_ROW_HEIGHT;

    for (int i = 0; i < phases; i++) {
        // Opacity for brightness
        uint8_t brightness = calculateOpacity(moonPhase, i, phases);

        // Scale color by brightness
        auto scaleColor = [&](const Color &c) {
            return Color(
                (uint8_t)((c.r * brightness)/255),
                (uint8_t)((c.g * brightness)/255),
                (uint8_t)((c.b * brightness)/255)
            );
        };
        // Active vs. inactive base color
        Color base = (i == moonPhase) ? baseActive : baseInactive;
        Color fillColor = scaleColor(base);

        // Draw the bar
        for (int x = i * segmentWidth; x < (i+1)*segmentWidth; x++) {
            for (int y = yStart; y < yStart + MID_ROW_HEIGHT; y++) {
                canvas->SetPixel(x, y, fillColor.r, fillColor.g, fillColor.b);
            }
        }

        // Draw a single red pixel in the center with the same brightness
        uint8_t redVal = brightness; 
        if (redVal > 0) {
            int px = i * segmentWidth + (segmentWidth / 10);
            int py = yStart + (MID_ROW_HEIGHT / 20);
            canvas->SetPixel(px, py, redVal, 0, 0);
        }
    }
}

// -----------------------------------------------------------------------------
// Bottom row: time/date
// -----------------------------------------------------------------------------
void renderBottomRow(Canvas* canvas, Font &smallFont) {
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    char timeStr[9];       // "HH:MM:SS"
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localTime);

    // Modify date format to show only the last two digits of the year: "MM.DD.YY"
    char dateStr[9];       
    std::strftime(dateStr, sizeof(dateStr), "%m.%d.%y", localTime);

    Color textColor(255,255,0);
    int rowY = TOP_ROW_HEIGHT + MID_ROW_HEIGHT;

    // Render time
    rgb_matrix::DrawText(canvas, smallFont,
                         1, rowY + (BOTTOM_ROW_HEIGHT/2),
                         textColor, nullptr, timeStr, 0);
    // Render date
    rgb_matrix::DrawText(canvas, smallFont,
                         1, rowY + (BOTTOM_ROW_HEIGHT/2) + 7,
                         textColor, nullptr, dateStr, 0);
}

// -----------------------------------------------------------------------------
// ANIMATIONS for side squares
// -----------------------------------------------------------------------------
enum AnimationType {
    ANIM_FALLING_SAND = 0,
    ANIM_FLOATING_PARTICLE,
    ANIM_RANDOM_FADING_PIXELS,
    ANIM_PULSATING_CIRCLE,
    ANIM_COUNT
};

struct SquareAnimation {
    AnimationType type;
    int frameCount;
    static const int MAX_W = SIDE_COLUMN_WIDTH;
    static const int MAX_H = MATRIX_HEIGHT/2; 

    uint8_t pixels[MAX_W][MAX_H];

    int   circleRadius;
    bool  growing;
    int   partX, partY;  // for floating particle
    float influenceFactor;
};

SquareAnimation topSquareAnim;
SquareAnimation bottomSquareAnim;

void pickRandomAnimation(SquareAnimation &anim) {
    anim.type = (AnimationType)(rand() % ANIM_COUNT);
    anim.frameCount = 0;
    for (int x = 0; x < SquareAnimation::MAX_W; x++) {
        for (int y = 0; y < SquareAnimation::MAX_H; y++) {
            anim.pixels[x][y] = 0;
        }
    }
    anim.circleRadius    = 1;
    anim.growing         = true;
    anim.partX           = SquareAnimation::MAX_W / 2;
    anim.partY           = SquareAnimation::MAX_H / 2;
    anim.influenceFactor = 1.0f;
}

void initSquareAnimation(SquareAnimation &anim) {
    pickRandomAnimation(anim);
}

// -----------------------------------------------------------------------------
// Animation update functions
// -----------------------------------------------------------------------------
static void updateFallingSand(SquareAnimation &anim) {
    for (int y = SquareAnimation::MAX_H - 2; y >= 0; y--) {
        for (int x = 0; x < SquareAnimation::MAX_W; x++) {
            if (anim.pixels[x][y] > 0) {
                if (anim.pixels[x][y+1] == 0) {
                    anim.pixels[x][y+1] = anim.pixels[x][y];
                    anim.pixels[x][y]   = 0;
                }
            }
        }
    }
    int spawnThreshold = (int)(10 / std::max(1.0f, anim.influenceFactor));
    if (spawnThreshold < 1) spawnThreshold = 1; 

    if (rand() % spawnThreshold == 0) {
        int rx = rand() % SquareAnimation::MAX_W;
        anim.pixels[rx][0] = 128; 
    }
}

static void updateFloatingParticle(SquareAnimation &anim) {
    int steps = (int)std::ceil(anim.influenceFactor);

    for (int s = 0; s < steps; s++) {
        int dir = rand() % 4;
        int nx = anim.partX;
        int ny = anim.partY;
        if (dir == 0) nx--;
        if (dir == 1) nx++;
        if (dir == 2) ny--;
        if (dir == 3) ny++;

        if (nx >= 0 && nx < SquareAnimation::MAX_W) anim.partX = nx;
        if (ny >= 0 && ny < SquareAnimation::MAX_H) anim.partY = ny;
    }
}

static void updateRandomFadingPixels(SquareAnimation &anim) {
    int fadeInBoost = (int)(30 * anim.influenceFactor);

    for (int i = 0; i < 5; i++) {
        int rx = rand() % SquareAnimation::MAX_W;
        int ry = rand() % SquareAnimation::MAX_H;
        if (anim.pixels[rx][ry] < 255) {
            anim.pixels[rx][ry] += fadeInBoost;
            if (anim.pixels[rx][ry] > 255) anim.pixels[rx][ry] = 255;
        }
    }
    int fadeOut = 5;
    for (int x = 0; x < SquareAnimation::MAX_W; x++) {
        for (int y = 0; y < SquareAnimation::MAX_H; y++) {
            if (anim.pixels[x][y] > fadeOut) {
                anim.pixels[x][y] -= fadeOut;
            } else {
                anim.pixels[x][y] = 0;
            }
        }
    }
}

static void updatePulsatingCircle(SquareAnimation &anim) {
    int speedUp = (int)std::ceil(anim.influenceFactor);
    while (speedUp-- > 0) {
        if (anim.growing) {
            anim.circleRadius++;
            if (anim.circleRadius > 10) anim.growing = false;
        } else {
            anim.circleRadius--;
            if (anim.circleRadius < 1) anim.growing = true;
        }
    }
}

void updateSquareAnimation(SquareAnimation &anim) {
    anim.frameCount++;
    switch (anim.type) {
    case ANIM_FALLING_SAND:         updateFallingSand(anim);        break;
    case ANIM_FLOATING_PARTICLE:    updateFloatingParticle(anim);   break;
    case ANIM_RANDOM_FADING_PIXELS: updateRandomFadingPixels(anim); break;
    case ANIM_PULSATING_CIRCLE:     updatePulsatingCircle(anim);    break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// Rendering each square
// -----------------------------------------------------------------------------
void renderSquareAnimation(Canvas* canvas, const SquareAnimation &anim, int originX, int originY) {
    switch (anim.type) {
    case ANIM_FALLING_SAND: {
        for (int x = 0; x < SquareAnimation::MAX_W; x++) {
            for (int y = 0; y < SquareAnimation::MAX_H; y++) {
                uint8_t val = anim.pixels[x][y];
                if (val > 0) {
                    int scaled = (int)(val * anim.influenceFactor);
                    if (scaled > 255) scaled = 255;
                    canvas->SetPixel(originX + x, originY + y, scaled, scaled, 0);
                }
            }
        }
    }   break;

    case ANIM_FLOATING_PARTICLE: {
        int r = (int)(255 * anim.influenceFactor);
        if (r > 255) r = 255;
        canvas->SetPixel(originX + anim.partX, originY + anim.partY, r, 0, 0);
    }   break;

    case ANIM_RANDOM_FADING_PIXELS: {
        for (int x = 0; x < SquareAnimation::MAX_W; x++) {
            for (int y = 0; y < SquareAnimation::MAX_H; y++) {
                uint8_t val = anim.pixels[x][y];
                if (val > 0) {
                    int scaled = (int)(val * anim.influenceFactor);
                    if (scaled > 255) scaled = 255;
                    canvas->SetPixel(originX + x, originY + y, scaled, 0, scaled);
                }
            }
        }
    }   break;

    case ANIM_PULSATING_CIRCLE: {
        int g = (int)(255 * anim.influenceFactor);
        if (g > 255) g = 255;

        int cx = originX + SIDE_COLUMN_WIDTH/2;
        int cy = originY + (MATRIX_HEIGHT/4); 
        int r  = anim.circleRadius;

        for (int deg = 0; deg < 360; deg++) {
            double rad = deg * M_PI/180.0;
            int px = cx + (int)(r*cos(rad));
            int py = cy + (int)(r*sin(rad));
            if (px >= originX && px < originX + SIDE_COLUMN_WIDTH &&
                py >= originY && py < originY + (MATRIX_HEIGHT/2)) {
                canvas->SetPixel(px, py, 0, g, 0);
            }
        }
    }   break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// The main layout function
// -----------------------------------------------------------------------------
void renderLayout(Canvas* canvas, Font &font, Font &smallFont) {
    // 1) Top row: progress bar
    renderProgressBar(canvas, font);

    // 2) Middle row: moon phase
    renderMoonPhase(canvas);

    // 3) Bottom row background + time/date
    Color bottomColor(0,0,153);
    for (int x = 0; x < SECTION_WIDTH; x++) {
        for (int y = TOP_ROW_HEIGHT + MID_ROW_HEIGHT; y < MATRIX_HEIGHT; y++) {
            canvas->SetPixel(x, y, bottomColor.r, bottomColor.g, bottomColor.b);
        }
    }
    renderBottomRow(canvas, smallFont);

    // 4) Side column
    Color sideColor(0,153,153);
    for (int x = SECTION_WIDTH; x < MATRIX_WIDTH; x++) {
        for (int y = 0; y < MATRIX_HEIGHT; y++) {
            canvas->SetPixel(x, y, sideColor.r, sideColor.g, sideColor.b);
        }
    }
    int halfH = MATRIX_HEIGHT / 2;

    // 5) Update & render top square
    updateSquareAnimation(topSquareAnim);
    renderSquareAnimation(canvas, topSquareAnim, SECTION_WIDTH, 0);

    // 6) Update & render bottom square
    updateSquareAnimation(bottomSquareAnim);
    renderSquareAnimation(canvas, bottomSquareAnim, SECTION_WIDTH, halfH);

    // 7) Draw highlight overlays for whichever area is selected
    if (currentSelection == TOP_LINE) {
        drawHighlightOverlay(canvas, 0, 0, SECTION_WIDTH, TOP_ROW_HEIGHT);
    }
    else if (currentSelection == MID_LINE) {
        drawHighlightOverlay(canvas, 0, TOP_ROW_HEIGHT, SECTION_WIDTH, MID_ROW_HEIGHT);
    }
    else if (currentSelection == BOTTOM_LINE) {
        drawHighlightOverlay(canvas, 0, TOP_ROW_HEIGHT + MID_ROW_HEIGHT, SECTION_WIDTH, BOTTOM_ROW_HEIGHT);
    }
    else if (currentSelection == SIDE_TOP_SQUARE) {
        drawHighlightOverlay(canvas, SECTION_WIDTH, 0, SIDE_COLUMN_WIDTH, halfH);
    }
    else if (currentSelection == SIDE_BOTTOM_SQUARE) {
        drawHighlightOverlay(canvas, SECTION_WIDTH, halfH, SIDE_COLUMN_WIDTH, halfH);
    }
}

// -----------------------------------------------------------------------------
// Read rotary encoder position from FIFO & update selection
// -----------------------------------------------------------------------------
void readFromFIFO(int fifo_fd) {
    char buffer[256];
    int bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';

        // 1) Check for "Rotary Encoder Position: XXX"
        int encoder_value;
        if (sscanf(buffer, "Rotary Encoder Position: %d", &encoder_value) == 1) {
            int diff = encoder_value - last_encoder_value;
            last_encoder_value = encoder_value;

            // Move selection up or down depending on diff
            if (diff != 0) {
                while (diff > 0) {
                    currentSelection = (Component)((int)currentSelection + 1);
                    if (currentSelection >= NUM_COMPONENTS) {
                        currentSelection = (Component)(currentSelection - NUM_COMPONENTS);
                    }
                    diff--;
                }
                while (diff < 0) {
                    currentSelection = (Component)((int)currentSelection - 1);
                    if (currentSelection < 0) {
                        currentSelection = (Component)(currentSelection + NUM_COMPONENTS);
                    }
                    diff++;
                }
            }
        }
        // 2) Check for "Button Pressed" (the new requirement)
        else if (strstr(buffer, "Button is pressed") != NULL) {
            // The user pressed the button while in the main mode.
            // => We want to kill main mode and run whichever mode is assigned to currentSelection
            int areaIndex = (int)currentSelection;  // 0..4
            int chosenModeIndex = g_assignedModes[areaIndex];

            fprintf(stderr, "[MAIN_MODE] User selected area %d => assigned to real mode index %d.\n",
                    areaIndex, chosenModeIndex);

            // We’ll exit with code "100 + chosenModeIndex". The master sees that in waitpid().
            exit(100 + chosenModeIndex);
        }
        else {
            fprintf(stderr, "Received unexpected data: %s\n", buffer);
        }
    } 
    else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("Error reading from FIFO");
    }
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // 1) Setup signals
    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    // 2) Setup random seed
    srand(time(nullptr));

    // 3) Setup wiringPi for GPIO
    wiringPiSetupGpio();
    pinMode(SENSOR_TOP_PIN, INPUT);
    pinMode(SENSOR_BOTTOM_PIN, INPUT);

    int oldTopSensor = digitalRead(SENSOR_TOP_PIN);
    int oldBottomSensor = digitalRead(SENSOR_BOTTOM_PIN);

    // 4) Create matrix
    RGBMatrix::Options options;
    options.hardware_mapping = "adafruit-hat";
    options.rows = 32;
    options.cols = 64;
    options.chain_length = 1;
    options.brightness = 50;
    options.disable_hardware_pulsing = 1;
    options.panel_type = "FM6127";

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = 4;

    RGBMatrix* matrix = CreateMatrixFromOptions(options, runtime);
    if (!matrix) {
        fprintf(stderr, "Could not create matrix.\n");
        return 1;
    }

    // 5) Load fonts
    Font font;
    if (!font.LoadFont("../fonts/5x8.bdf")) {
        fprintf(stderr, "Failed to load font.\n");
        return 1;
    }
    Font smallFont;
    if (!smallFont.LoadFont("../fonts/5x8.bdf")) {
        fprintf(stderr, "Failed to load small font.\n");
        return 1;
    }

    // 6) Initialize side-square animations
    initSquareAnimation(topSquareAnim);
    initSquareAnimation(bottomSquareAnim);

    // ADDED: Assign 5 random modes whenever main_mode_rotary starts up
    AssignRandomModes();

    // 7) Create an offscreen "frame canvas"
    FrameCanvas *offscreen = matrix->CreateFrameCanvas();

    // 8) Open the FIFO (non-blocking)
    int fifo_fd;
    int attempts = 10;
    while ((fifo_fd = open(DATA_FIFO_PATH, O_RDONLY | O_NONBLOCK)) == -1 && attempts > 0) {
        perror("Failed to open FIFO for reading");
        sleep(1);
        attempts--;
    }
    if (fifo_fd == -1) {
        fprintf(stderr, "Could not open FIFO after multiple attempts. Exiting.\n");
        delete matrix;
        return 1;
    }

    // 9) Main loop
    while (!interrupt_received) {
        // -- Read new sensor states
        int newTopSensor    = digitalRead(SENSOR_TOP_PIN);
        int newBottomSensor = digitalRead(SENSOR_BOTTOM_PIN);

        // If sensor goes HIGH => set influenceFactor to 2; else 1
        if (newTopSensor != oldTopSensor) {
            if (newTopSensor == 1) topSquareAnim.influenceFactor = 2.0f;
            else                   topSquareAnim.influenceFactor = 1.0f;
            oldTopSensor = newTopSensor;
        }

        if (newBottomSensor != oldBottomSensor) {
            if (newBottomSensor == 1) bottomSquareAnim.influenceFactor = 2.0f;
            else                      bottomSquareAnim.influenceFactor = 1.0f;
            oldBottomSensor = newBottomSensor;
        }

        // -- Read from FIFO for rotary + button updates
        readFromFIFO(fifo_fd);

        // -- Clear offscreen
        offscreen->Clear();

        // -- Render layout (progress bars, squares, etc.)
        renderLayout(offscreen, font, smallFont);

        // -- Swap buffers
        offscreen = matrix->SwapOnVSync(offscreen);

        // -- Slight delay
        usleep(100 * 1000); // 100 ms
    }

    // Cleanup
    close(fifo_fd);
    delete matrix;
    return 0;
}
