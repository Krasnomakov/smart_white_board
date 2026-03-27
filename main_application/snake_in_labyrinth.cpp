#include "led-matrix-c.h"
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
#include <stdint.h> // For uint32_t, uint8_t

// Forward declarations
static void outputMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas);

// ============================================================================
// Updated Code for Infinite Maze:
// - Snake moves through infinite maze sections.
// - Red dots appear in each maze section.
// - Red dots move within the maze section but never leave the screen.
// - Initially start with 1 dot in a section.
//   When snake eats all dots in that section, next time a new unvisited section
//   is encountered, it spawns one more dot than last time, up to 10.
//   After reaching 10 and consumed, cycle back to 1.
// - Each maze section maintains its own dots. If snake returns to a previously
//   visited section, the remaining dots (if any) are still there.
//   If the section was cleared, it's cleared until re-spawn logic triggers on a new section.
// ============================================================================

// Constants
const int MATRIX_WIDTH = 64;
const int MATRIX_HEIGHT = 32;
const int CELLS_W = MATRIX_WIDTH / 2;  // 32
const int CELLS_H = MATRIX_HEIGHT / 2; // 16

// Structure for RGB color
struct RGB {
    unsigned char r, g, b;
};

RGB wall_color = {255, 255, 255}; 
RGB path_color = {0, 0, 0};       
RGB player_color = {255, 0, 0};   // Red for dots
RGB head_color = {0, 255, 0};     // Green for snake head

struct Cell {
    bool visited = false;
    bool walls[4] = {true, true, true, true};
    bool traversal_visited = false; 
};

enum Direction { TOP, RIGHT, BOTTOM, LEFT };
using MazeSection = std::vector<std::vector<Cell>>;

std::map<std::pair<int, int>, MazeSection> maze_sections; 

// Dot structure
struct Dot {
    int x, y; // cell coords in the maze section
};

// For each maze section, store its dots
std::map<std::pair<int,int>, std::vector<Dot>> section_dots;

// Snake structures
struct SnakePlayer {
    int x, y; 
};

struct Segment {
    long gx, gy;
};

// Globals
std::mt19937 rng;
RGB led_matrix[MATRIX_WIDTH][MATRIX_HEIGHT];

// Snake global position
long snake_global_x = CELLS_W/2; 
long snake_global_y = CELLS_H/2;
int snake_maze_x = 0;
int snake_maze_y = 0;
SnakePlayer snake_player;
std::vector<Segment> snake_segments;
std::map<std::pair<long,long>, int> snake_visited_count;

// Superpower
bool superpower_active = false;
uint32_t superpower_start = 0; 
static uint32_t fake_millis = 0; 
static uint32_t getFakeMillis() {
    return fake_millis;
}

// Dot spawn cycle
int dots_to_spawn = 1; 
// When a section is first visited and has no dots yet, we spawn dots_to_spawn dots.
// When all dots in that section are consumed by the snake, next new section that requires spawn increments dots_to_spawn by 1 until it hits 10, then resets to 1.

// Utility
static int divFloor(long a, long b) {
    if (b == 0) return 0; 
    int d = (int)(a / b);
    if ((a < 0) != (b < 0) && (a % b != 0)) {
        d -= 1;
    }
    return d;
}

bool isInBounds(int x, int y) {
    return x >= 0 && x < CELLS_W && y >= 0 && y < CELLS_H;
}

Direction opposite(Direction dir) {
    switch (dir) {
        case TOP: return BOTTOM;
        case RIGHT: return LEFT;
        case BOTTOM: return TOP;
        case LEFT: return RIGHT;
    }
    return TOP; 
}

void generateMazeSection(MazeSection& maze, std::mt19937& rng) {
    std::function<void(int, int)> dfs = [&](int x, int y) {
        maze[x][y].visited = true;
        std::vector<Direction> directions = {TOP, RIGHT, BOTTOM, LEFT};
        std::shuffle(directions.begin(), directions.end(), rng);

        for (auto dir : directions) {
            int nx = x, ny = y;
            switch (dir) {
                case TOP:    ny = y - 1; break;
                case RIGHT:  nx = x + 1; break;
                case BOTTOM: ny = y + 1; break;
                case LEFT:   nx = x - 1; break;
            }

            if (isInBounds(nx, ny) && !maze[nx][ny].visited) {
                maze[x][y].walls[dir] = false;
                maze[nx][ny].walls[opposite(dir)] = false;
                dfs(nx, ny);
            }
        }
    };

    for (auto& row : maze) {
        for (auto& cell : row) {
            cell.visited = false;
            std::fill(std::begin(cell.walls), std::end(cell.walls), true);
        }
    }

    dfs(0, 0);
}

MazeSection& getMazeSection(int x, int y, std::mt19937& rng) {
    auto key = std::make_pair(x, y);
    if (maze_sections.find(key) == maze_sections.end()) {
        maze_sections[key] = MazeSection(CELLS_W, std::vector<Cell>(CELLS_H));
        generateMazeSection(maze_sections[key], rng);
    }
    return maze_sections[key];
}

static RGB interpolateColor(float fraction) {
    struct ColorStop {
        float pos;
        RGB color;
    };
    ColorStop stops[] = {
        {0.0f,  {0,   0,   255}},
        {0.25f, {0,   255, 255}},
        {0.5f,  {0,   255,   0}},
        {0.75f, {255, 255,   0}},
        {1.0f,  {255,   0,   0}}
    };

    ColorStop c1 = stops[0], c2 = stops[0];
    for (int i = 1; i < 5; i++) {
        if (fraction <= stops[i].pos) {
            c2 = stops[i];
            break;
        }
        c1 = stops[i];
    }

    float range = c2.pos - c1.pos;
    float t = (fraction - c1.pos) / ((range > 0) ? range : 1);

    uint8_t rr = (uint8_t)(c1.color.r + (c2.color.r - c1.color.r)*t);
    uint8_t gg = (uint8_t)(c1.color.g + (c2.color.g - c1.color.g)*t);
    uint8_t bb = (uint8_t)(c1.color.b + (c2.color.b - c1.color.b)*t);
    RGB result = {rr, gg, bb};
    return result;
}

RGB getColorForVisits(int count) {
    int max_visits = 10;
    if (count > max_visits) count = max_visits;
    float fraction = (float)(count-1) / (float)(max_visits-1);
    return interpolateColor(fraction);
}

void drawParticlesAt(int px, int py) {
    for (int i=0; i<5; i++) {
        int ox = px + (rand()%5 - 2);
        int oy = py + (rand()%5 - 2);
        if (ox >=0 && ox < MATRIX_WIDTH && oy >=0 && oy < MATRIX_HEIGHT) {
            RGB color = { (unsigned char)(rand()%256), (unsigned char)(rand()%256), (unsigned char)(rand()%256) };
            led_matrix[ox][oy] = color;
        }
    }
}

void maybeActivateSuperpower() {
    if (superpower_active) {
        uint32_t now = getFakeMillis();
        if (now - superpower_start > 15000) {
            superpower_active = false;
        }
        return;
    }

    if (rand() % 1000 == 0) {
        superpower_active = true;
        superpower_start = getFakeMillis();
    }
}

// Snake portals
bool canExitSection(Direction dir, int cell_x, int cell_y) {
    int portal_width = CELLS_W/4;   
    int portal_start_x = (CELLS_W - portal_width)/2; 
    int portal_end_x = portal_start_x + portal_width - 1; 

    int portal_height = CELLS_H/4; 
    int portal_start_y = (CELLS_H - portal_height)/2; 
    int portal_end_y = portal_start_y + portal_height - 1; 

    switch(dir) {
        case TOP:
            if (cell_y == 0 && cell_x >= portal_start_x && cell_x <= portal_end_x) return true;
            break;
        case BOTTOM:
            if (cell_y == CELLS_H-1 && cell_x >= portal_start_x && cell_x <= portal_end_x) return true;
            break;
        case LEFT:
            if (cell_x == 0 && cell_y >= portal_start_y && cell_y <= portal_end_y) return true;
            break;
        case RIGHT:
            if (cell_x == CELLS_W-1 && cell_y >= portal_start_y && cell_y <= portal_end_y) return true;
            break;
    }
    return false;
}

void moveSnake(std::mt19937& rng) {
    snake_maze_x = divFloor(snake_global_x, CELLS_W);
    snake_maze_y = divFloor(snake_global_y, CELLS_H);
    snake_player.x = (int)(snake_global_x - snake_maze_x*(CELLS_W));
    snake_player.y = (int)(snake_global_y - snake_maze_y*(CELLS_H));

    MazeSection& current_maze = getMazeSection(snake_maze_x, snake_maze_y, rng);

    int dx = 0, dy = 0;

    if (superpower_active) {
        int d = rand() % 4;
        switch(d) {
            case 0: dy = -1; break;
            case 1: dx = 1; break;
            case 2: dy = 1; break;
            case 3: dx = -1; break;
        }
    } else {
        std::vector<Direction> possible_dirs;
        for (auto dir : {TOP, RIGHT, BOTTOM, LEFT}) {
            int nx = snake_player.x;
            int ny = snake_player.y;
            switch(dir) {
                case TOP: ny = snake_player.y - 1; break;
                case RIGHT: nx = snake_player.x + 1; break;
                case BOTTOM: ny = snake_player.y + 1; break;
                case LEFT: nx = snake_player.x - 1; break;
            }

            bool inside = (ny >=0 && ny < CELLS_H && nx >=0 && nx < CELLS_W);

            if (!inside) {
                if (canExitSection(dir, snake_player.x, snake_player.y)) {
                    possible_dirs.push_back(dir);
                }
            } else {
                if (!current_maze[snake_player.x][snake_player.y].walls[dir]) {
                    possible_dirs.push_back(dir);
                }
            }
        }

        if (possible_dirs.empty()) {
            return;
        }

        int best_visits = INT_MAX;
        Direction chosen_dir = possible_dirs[0];

        for (auto dir : possible_dirs) {
            int ddx=0, ddy=0;
            switch(dir) {
                case TOP: ddy=-1; break;
                case RIGHT: ddx=1; break;
                case BOTTOM: ddy=1; break;
                case LEFT: ddx=-1; break;
            }

            long new_gx = snake_global_x + ddx;
            long new_gy = snake_global_y + ddy;

            int visits = 0;
            auto it = snake_visited_count.find({new_gx, new_gy});
            if (it != snake_visited_count.end()) visits = it->second;

            if (visits < best_visits) {
                best_visits = visits;
                chosen_dir = dir;
            }
        }

        switch(chosen_dir) {
            case TOP: dy=-1; break;
            case RIGHT: dx=1; break;
            case BOTTOM: dy=1; break;
            case LEFT: dx=-1; break;
        }
    }

    snake_global_x += dx;
    snake_global_y += dy;

    snake_segments.push_back({snake_global_x, snake_global_y});
    snake_visited_count[{snake_global_x, snake_global_y}]++;
}

// Move dots of the current section
void moveDots(MazeSection &maze, std::vector<Dot> &dots) {
    for (auto &dot : dots) {
        std::vector<Direction> possible_dirs;
        for (auto dir : {TOP, RIGHT, BOTTOM, LEFT}) {
            int nx = dot.x;
            int ny = dot.y;
            switch(dir) {
                case TOP: ny = dot.y - 1; break;
                case RIGHT: nx = dot.x + 1; break;
                case BOTTOM: ny = dot.y + 1; break;
                case LEFT: nx = dot.x - 1; break;
            }

            if (!isInBounds(nx, ny)) continue;
            if (!maze[dot.x][dot.y].walls[dir]) {
                possible_dirs.push_back(dir);
            }
        }

        if (!possible_dirs.empty()) {
            std::uniform_int_distribution<int> dist(0, (int)possible_dirs.size()-1);
            Direction chosen = possible_dirs[dist(rng)];
            switch(chosen) {
                case TOP: dot.y -= 1; break;
                case RIGHT: dot.x += 1; break;
                case BOTTOM: dot.y += 1; break;
                case LEFT: dot.x -= 1; break;
            }
        }
    }
}

// Check collisions in current section
void checkCollisions(std::vector<Dot> &dots) {
    int hx = snake_player.x;
    int hy = snake_player.y;

    for (size_t i=0; i<dots.size(); i++) {
        if (dots[i].x == hx && dots[i].y == hy) {
            // Dot consumed
            dots.erase(dots.begin()+i);
            break;
        }
    }
}

// If section is empty of dots on first visit, spawn new
void ensureDotsInSection(int mx, int my) {
    auto key = std::make_pair(mx,my);
    if (section_dots.find(key) == section_dots.end()) {
        // first time visiting this section
        // spawn dots_to_spawn dots at random
        std::vector<Dot> new_dots;
        MazeSection &maze = getMazeSection(mx,my,rng);
        for (int i=0; i<dots_to_spawn; i++) {
            int rx = rand()%CELLS_W;
            int ry = rand()%CELLS_H;
            new_dots.push_back({rx, ry});
        }
        section_dots[key] = new_dots;
    }
}

// If all dots consumed, adjust dots_to_spawn
void handleDotConsumption(int mx, int my) {
    auto &dots = section_dots[{mx,my}];
    if (dots.empty()) {
        if (dots_to_spawn < 10) dots_to_spawn++;
        else dots_to_spawn = 1;
    }
}

void renderMaze(MazeSection& maze, const std::vector<Dot> &dots) {
    for (int x = 0; x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < MATRIX_HEIGHT; ++y) {
            led_matrix[x][y] = wall_color;
        }
    }

    for (int x = 0; x < CELLS_W; ++x) {
        for (int y = 0; y < CELLS_H; ++y) {
            int pixel_x = x * 2 + 1;
            int pixel_y = y * 2 + 1;

            led_matrix[pixel_x][pixel_y] = path_color;

            if (!maze[x][y].walls[TOP] && pixel_y - 1 >= 0) {
                led_matrix[pixel_x][pixel_y - 1] = path_color;
            }
            if (!maze[x][y].walls[RIGHT] && pixel_x + 1 < MATRIX_WIDTH) {
                led_matrix[pixel_x + 1][pixel_y] = path_color;
            }
            if (!maze[x][y].walls[BOTTOM] && pixel_y + 1 < MATRIX_HEIGHT) {
                led_matrix[pixel_x][pixel_y + 1] = path_color;
            }
            if (!maze[x][y].walls[LEFT] && pixel_x - 1 >= 0) {
                led_matrix[pixel_x - 1][pixel_y] = path_color;
            }
        }
    }

    // Draw dots
    for (auto &dot : dots) {
        int px = dot.x * 2 + 1;
        int py = dot.y * 2 + 1;
        led_matrix[px][py] = player_color;
    }
}

void drawSnake() {
    if (snake_segments.empty()) return;

    snake_maze_x = divFloor(snake_global_x, CELLS_W);
    snake_maze_y = divFloor(snake_global_y, CELLS_H);
    snake_player.x = (int)(snake_global_x - snake_maze_x*(CELLS_W));
    snake_player.y = (int)(snake_global_y - snake_maze_y*(CELLS_H));

    for (size_t i = 0; i < snake_segments.size(); i++) {
        long sgx = snake_segments[i].gx;
        long sgy = snake_segments[i].gy;

        long section_x = divFloor(sgx, CELLS_W);
        long section_y = divFloor(sgy, CELLS_H);

        if (section_x == snake_maze_x && section_y == snake_maze_y) {
            int local_x = (int)(sgx - section_x*(CELLS_W));
            int local_y = (int)(sgy - section_y*(CELLS_H));

            int px = local_x * 2 + 1;
            int py = local_y * 2 + 1;

            int count = snake_visited_count[{sgx, sgy}];
            RGB color = (i == snake_segments.size()-1) ? head_color : getColorForVisits(count);

            led_matrix[px][py] = color;

            if (i > 0) {
                long pgx = snake_segments[i-1].gx;
                long pgy = snake_segments[i-1].gy;
                long psection_x = divFloor(pgx, CELLS_W);
                long psection_y = divFloor(pgy, CELLS_H);

                if (psection_x == snake_maze_x && psection_y == snake_maze_y) {
                    int plocal_x = (int)(pgx - psection_x*(CELLS_W));
                    int plocal_y = (int)(pgy - psection_y*(CELLS_H));
                    int ppx = plocal_x * 2 + 1;
                    int ppy = plocal_y * 2 + 1;

                    int corridor_x = ppx;
                    int corridor_y = ppy;
                    if (px > ppx) corridor_x = ppx + 1;
                    else if (px < ppx) corridor_x = ppx - 1;
                    else if (py > ppy) corridor_y = ppy + 1;
                    else if (py < ppy) corridor_y = ppy - 1;

                    led_matrix[corridor_x][corridor_y] = color;

                    if (superpower_active && i == snake_segments.size()-1) {
                        drawParticlesAt(px, py);
                        drawParticlesAt(corridor_x, corridor_y);
                    }
                } else if (superpower_active && i == snake_segments.size()-1) {
                    drawParticlesAt(px, py);
                }
            } else if (superpower_active && i == snake_segments.size()-1) {
                drawParticlesAt(px, py);
            }
        }
    }
}

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
    interrupt_received = true;
}

static void outputMatrix(struct RGBLedMatrix* matrix, struct LedCanvas* offscreen_canvas) {
    int width, height;
    led_canvas_get_size(offscreen_canvas, &width, &height);

    for (int x = 0; x < width && x < MATRIX_WIDTH; ++x) {
        for (int y = 0; y < height && y < MATRIX_HEIGHT; ++y) {
            RGB color = led_matrix[x][y];
            led_canvas_set_pixel(offscreen_canvas, x, MATRIX_HEIGHT - 1 - y, color.r, color.g, color.b);
        }
    }

    offscreen_canvas = led_matrix_swap_on_vsync(matrix, offscreen_canvas);
}

int main(int argc, char* argv[]) {
    struct RGBLedMatrixOptions options;
    struct RGBLedRuntimeOptions rt_options;
    struct RGBLedMatrix* matrix;
    struct LedCanvas* offscreen_canvas;

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    memset(&options, 0, sizeof(options));
    memset(&rt_options, 0, sizeof(rt_options));
    options.hardware_mapping = "adafruit-hat";
    options.rows = 32;
    options.cols = 64;
    options.chain_length = 1;
    options.parallel = 1;
    options.show_refresh_rate = false;
    options.brightness = 50;
    options.disable_hardware_pulsing = 1;
    options.panel_type = "FM6127"; 
    rt_options.gpio_slowdown = 4;

    matrix = led_matrix_create_from_options_and_rt_options(&options, &rt_options);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create matrix\n");
        return 1;
    }

    offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

    std::random_device rd;
    rng = std::mt19937(rd());

    snake_segments.push_back({snake_global_x, snake_global_y});
    snake_visited_count[{snake_global_x, snake_global_y}] = 1;

    while (!interrupt_received) {
        fake_millis += 100; 

        maybeActivateSuperpower();
        moveSnake(rng);

        // Current maze section for snake
        MazeSection& current_maze = getMazeSection(snake_maze_x, snake_maze_y, rng);

        // Ensure dots in current section
        ensureDotsInSection(snake_maze_x, snake_maze_y);
        auto &current_dots = section_dots[{snake_maze_x, snake_maze_y}];

        moveDots(current_maze, current_dots);
        checkCollisions(current_dots);

        // If all dots consumed in this section, handle dot count cycle
        if (current_dots.empty()) {
            handleDotConsumption(snake_maze_x, snake_maze_y);
        }

        renderMaze(current_maze, current_dots);
        drawSnake();
        outputMatrix(matrix, offscreen_canvas);

        usleep(100000); // Delay
    }

    led_matrix_delete(matrix);

    return 0;
}
