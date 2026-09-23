#include "console.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ==========================================
// PALETTE & COLOR UTILS (0xAABBGGRR)
// ==========================================
constexpr uint32_t C_BLACK   = 0xFF000000;
constexpr uint32_t C_WHITE   = 0xFFFFFFFF;
constexpr uint32_t C_RED     = 0xFF3333FF;
constexpr uint32_t C_GREEN   = 0xFF33FF33;
constexpr uint32_t C_BLUE    = 0xFFFF5533;
constexpr uint32_t C_CYAN    = 0xFFFFFF33;
constexpr uint32_t C_YELLOW  = 0xFF33FFFF;
constexpr uint32_t C_MAGENTA = 0xFFFF33FF;
constexpr uint32_t C_DARK    = 0xFF181010;

// Additive Blend: dst + src (clamped to 255 per channel)
inline uint32_t blend_add(uint32_t dst, uint32_t src) {
    uint32_t r = (dst & 0xFF) + (src & 0xFF);
    uint32_t g = ((dst >> 8) & 0xFF) + ((src >> 8) & 0xFF);
    uint32_t b = ((dst >> 16) & 0xFF) + ((src >> 16) & 0xFF);
    return 0xFF000000 | 
           (std::min(b, 255u) << 16) | 
           (std::min(g, 255u) << 8) | 
           std::min(r, 255u);
}

// ==========================================
// RESOLUTION PRESETS
// ==========================================
struct ResPreset {
    int w, h;
};

const ResPreset RESOLUTIONS[] = {
    {160, 90},    // 0: Ultra Low
    {320, 180},   // 1: Retro 16:9
    {480, 270},   // 2: Portable
    {640, 360},   // 3: SD Widescreen
    {640, 480},   // 4: Classic VGA 4:3
    {960, 540},   // 5: qHD
    {1280, 720},  // 6: 720p HD
    {1920, 1080}  // 7: 1080p Full HD
};
constexpr int NUM_RESOLUTIONS = sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]);
int current_res_idx = 1; // Default: 320x180
int game_w = 320;
int game_h = 180;

// ==========================================
// GAME STATE & STRESS BENCHMARK
// ==========================================
enum GameState { ATTRACT_MODE, PLAYING };
GameState state = ATTRACT_MODE;

int frame_counter = 0;
float px = 160.0f, py = 150.0f;
bool plasma_enabled = false;

// 3D Starfield
constexpr int MAX_STARS = 1000;
struct Star3D { float x, y, z; };
Star3D stars[MAX_STARS];

// Additive Particle Stress System
constexpr int MAX_PARTICLES = 2500;
struct Particle {
    float x, y, vx, vy;
    uint32_t color;
    int life, max_life;
    bool active;
};
Particle particles[MAX_PARTICLES];
int active_particle_target = 300; // Scalable stress level

// Laser Bullets
struct Bullet { float x, y; bool active; };
Bullet bullets[16];

// Audio State
float sfx_pitch = 0.0f;
float sfx_noise = 0.0f;
float player_pan = 0.5f; // 0.0 (left) to 1.0 (right)

// ==========================================
// 3D WIREFRAME CUBE DATA
// ==========================================
struct Vec3 { float x, y, z; };
const Vec3 CUBE_VERTS[8] = {
    {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
};
const int CUBE_EDGES[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};
float cube_rot_x = 0.0f;
float cube_rot_y = 0.0f;

// ==========================================
// GRAPHICS PRIMITIVES
// ==========================================
inline void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < game_w && y >= 0 && y < game_h) {
        framebuffer[y * game_w + x] = color;
    }
}

inline void put_pixel_add(int x, int y, uint32_t color) {
    if (x >= 0 && x < game_w && y >= 0 && y < game_h) {
        int idx = y * game_w + x;
        framebuffer[idx] = blend_add(framebuffer[idx], color);
    }
}

void draw_rect(int start_x, int start_y, int w, int h, uint32_t color) {
    int x1 = std::max(0, start_x);
    int y1 = std::max(0, start_y);
    int x2 = std::min(game_w, start_x + w);
    int y2 = std::min(game_h, start_y + h);

    for (int y = y1; y < y2; ++y) {
        int row = y * game_w;
        for (int x = x1; x < x2; ++x) {
            framebuffer[row + x] = color;
        }
    }
}

void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void spawn_explosion(float x, float y, int count, uint32_t color) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; ++i) {
        if (!particles[i].active) {
            particles[i].active = true;
            particles[i].x = x;
            particles[i].y = y;
            float angle = (float)(rand() % 628) / 100.0f;
            float speed = ((rand() % 100) / 25.0f) + 1.0f;
            particles[i].vx = std::cos(angle) * speed;
            particles[i].vy = std::sin(angle) * speed;
            particles[i].life = (rand() % 40) + 20;
            particles[i].max_life = particles[i].life;
            particles[i].color = color;
            spawned++;
        }
    }
}

// ==========================================
// LIFECYCLE
// ==========================================
extern "C" {
    WASM_EXPORT("init")
    void init() {
        set_screen_res(game_w, game_h);

        // Init 3D Stars
        for (int i = 0; i < MAX_STARS; ++i) {
            stars[i].x = (float)((rand() % 2000) - 1000);
            stars[i].y = (float)((rand() % 2000) - 1000);
            stars[i].z = (float)((rand() % 1000) + 1);
        }

        // Init Particles
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            particles[i].active = false;
        }

        // Init Bullets
        for (int i = 0; i < 16; ++i) {
            bullets[i].active = false;
        }
    }

    WASM_EXPORT("update")
    void update() {
        frame_counter++;
        uint32_t pressed = get_btn_pressed();
        uint32_t just_pressed = get_btn_just_pressed();

        // 1. Resolution Switching
        if (just_pressed & BTN_X) {
            current_res_idx = (current_res_idx + 1) % NUM_RESOLUTIONS;
            game_w = RESOLUTIONS[current_res_idx].w;
            game_h = RESOLUTIONS[current_res_idx].h;
            set_screen_res(game_w, game_h);
        }
        if (just_pressed & BTN_Y) {
            current_res_idx = (current_res_idx - 1 + NUM_RESOLUTIONS) % NUM_RESOLUTIONS;
            game_w = RESOLUTIONS[current_res_idx].w;
            game_h = RESOLUTIONS[current_res_idx].h;
            set_screen_res(game_w, game_h);
        }

        // 2. Stress Load Adjustment
        if (just_pressed & BTN_R) { // Increase Load
            active_particle_target = std::min(active_particle_target + 250, MAX_PARTICLES);
        }
        if (just_pressed & BTN_L) { // Decrease Load
            active_particle_target = std::max(active_particle_target - 250, 50);
        }
        if (just_pressed & BTN_B) { // Toggle Heavy Plasma
            plasma_enabled = !plasma_enabled;
        }

        // 3. State & Game Logic
        if (state == ATTRACT_MODE) {
            if (just_pressed & BTN_START) {
                state = PLAYING;
                px = game_w * 0.5f;
                py = game_h * 0.85f;
                sfx_noise = 1.0f; // Roar sound on start
            }
        } 
        else if (state == PLAYING) {
            float speed = (game_w / 320.0f) * 3.5f; // Scale speed with resolution
            if (pressed & BTN_LEFT)  px -= speed;
            if (pressed & BTN_RIGHT) px += speed;
            if (pressed & BTN_UP)    py -= speed;
            if (pressed & BTN_DOWN)  py += speed;

            px = std::max(20.0f, std::min((float)game_w - 20.0f, px));
            py = std::max(20.0f, std::min((float)game_h - 20.0f, py));
            player_pan = px / (float)game_w;

            // Shoot
            if (just_pressed & BTN_A) {
                for (int i = 0; i < 16; ++i) {
                    if (!bullets[i].active) {
                        bullets[i].active = true;
                        bullets[i].x = px;
                        bullets[i].y = py - 12;
                        sfx_pitch = 900.0f;
                        spawn_explosion(px, py, 120, C_CYAN); // Additive particle burst
                        break;
                    }
                }
            }
        }

        // Update Bullets
        for (int i = 0; i < 16; ++i) {
            if (bullets[i].active) {
                bullets[i].y -= (game_h / 180.0f) * 7.0f;
                if (bullets[i].y < 0) bullets[i].active = false;
            }
        }

        // 4. Update 3D Starfield
        for (int i = 0; i < MAX_STARS; ++i) {
            stars[i].z -= 12.0f;
            if (stars[i].z <= 1.0f) {
                stars[i].z = 1000.0f;
                stars[i].x = (float)((rand() % 2000) - 1000);
                stars[i].y = (float)((rand() % 2000) - 1000);
            }
        }

        // 5. Update Ambient Stress Particles
        int active_count = 0;
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (particles[i].active) {
                particles[i].x += particles[i].vx;
                particles[i].y += particles[i].vy;
                particles[i].life--;
                if (particles[i].life <= 0) particles[i].active = false;
                else active_count++;
            }
        }
        // Continuous particle emitter to keep stress high
        if (active_count < active_particle_target) {
            spawn_explosion((float)(rand() % game_w), (float)(rand() % (game_h / 2)), 
                            std::min(25, active_particle_target - active_count), 
                            (rand() % 2 == 0) ? C_MAGENTA : C_YELLOW);
        }

        // 6. Update 3D Cube Rotation
        cube_rot_x += 0.02f;
        cube_rot_y += 0.035f;

        // 7. Stereo Synthesizer Engine
        static float phase = 0.0f;
        for (int i = 0; i < AUDIO_FRAMES_PER_TICK; ++i) {
            int16_t mono = 0;
            // Background Engine Hum
            float hum = (state == PLAYING) ? 55.0f : 45.0f;
            mono += (int16_t)(std::sin(phase * hum) * 1200.0f);

            // Laser Sine Sweep
            if (sfx_pitch > 50.0f) {
                mono += (int16_t)(std::sin(phase * sfx_pitch) * 3500.0f);
                sfx_pitch -= 0.8f;
            }

            // White Noise Burst
            if (sfx_noise > 0.01f) {
                mono += (int16_t)(((rand() % 2000) - 1000) * sfx_noise * 3.0f);
                sfx_noise *= 0.999f;
            }

            // Pan audio between left and right ears
            audio_buffer[i * 2 + 0] = (int16_t)(mono * (1.0f - player_pan)); // Left
            audio_buffer[i * 2 + 1] = (int16_t)(mono * player_pan);          // Right

            phase += (2.0f * 3.14159f) / SAMPLE_RATE;
            if (phase > 100.0f) phase -= 100.0f;
        }
    }

    WASM_EXPORT("draw")
    void draw() {
        // ==========================================
        // 1. BACKGROUND (FAST CLEAR OR HEAVY PLASMA)
        // ==========================================
        if (plasma_enabled) {
            // Per-pixel trig plasma stress test across current resolution
            float t = frame_counter * 0.05f;
            for (int y = 0; y < game_h; ++y) {
                int row = y * game_w;
                float fy = (float)y * 0.03f;
                for (int x = 0; x < game_w; ++x) {
                    float fx = (float)x * 0.03f;
                    float v = std::sin(fx + t) + std::sin(fy + t) + std::sin((fx + fy) + t);
                    uint8_t c = (uint8_t)((v + 3.0f) * 28.0f);
                    framebuffer[row + x] = 0xFF000000 | (c << 16) | (c / 2 << 8) | (c / 4);
                }
            }
        } else {
            // Raw Fast Screen Clear
            std::fill_n(framebuffer, game_w * game_h, C_BLACK);
        }

        // ==========================================
        // 2. 3D PERSPECTIVE STARFIELD
        // ==========================================
        float cx = game_w * 0.5f;
        float cy = game_h * 0.5f;
        float fov = game_w * 0.6f;

        for (int i = 0; i < MAX_STARS; ++i) {
            float inv_z = 1.0f / stars[i].z;
            int sx = (int)(cx + stars[i].x * inv_z * fov);
            int sy = (int)(cy + stars[i].y * inv_z * fov);

            if (sx >= 0 && sx < game_w && sy >= 0 && sy < game_h) {
                uint32_t col = (stars[i].z < 400.0f) ? C_WHITE : 
                              ((stars[i].z < 700.0f) ? C_CYAN : 0xFF555555);
                put_pixel(sx, sy, col);
            }
        }

        // ==========================================
        // 3. 3D ROTATING WIREFRAME CUBE
        // ==========================================
        Vec3 proj_verts[8];
        float cos_x = std::cos(cube_rot_x), sin_x = std::sin(cube_rot_x);
        float cos_y = std::cos(cube_rot_y), sin_y = std::sin(cube_rot_y);
        float cube_scale = game_h * 0.15f;

        for (int i = 0; i < 8; ++i) {
            // Y rotation
            float x1 = CUBE_VERTS[i].x * cos_y + CUBE_VERTS[i].z * sin_y;
            float z1 = -CUBE_VERTS[i].x * sin_y + CUBE_VERTS[i].z * cos_y;
            // X rotation
            float y2 = CUBE_VERTS[i].y * cos_x - z1 * sin_x;
            float z2 = CUBE_VERTS[i].y * sin_x + z1 * cos_x + 3.5f;

            // Perspective Projection
            proj_verts[i].x = cx + (x1 / z2) * cube_scale * 3.0f;
            proj_verts[i].y = cy + (y2 / z2) * cube_scale * 3.0f;
        }

        for (int i = 0; i < 12; ++i) {
            const auto& p1 = proj_verts[CUBE_EDGES[i][0]];
            const auto& p2 = proj_verts[CUBE_EDGES[i][1]];
            draw_line((int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y, C_BLUE);
        }

        // ==========================================
        // 4. ADDITIVE GLOW PARTICLES (STRESS TEST)
        // ==========================================
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (particles[i].active) {
                int px_pos = (int)particles[i].x;
                int py_pos = (int)particles[i].y;
                // Additive 3x3 glowing core
                put_pixel_add(px_pos, py_pos, particles[i].color);
                put_pixel_add(px_pos - 1, py_pos, 0xFF442222);
                put_pixel_add(px_pos + 1, py_pos, 0xFF442222);
                put_pixel_add(px_pos, py_pos - 1, 0xFF442222);
                put_pixel_add(px_pos, py_pos + 1, 0xFF442222);
            }
        }

        // ==========================================
        // 5. BULLETS & PLAYER SHIP
        // ==========================================
        for (int i = 0; i < 16; ++i) {
            if (bullets[i].active) {
                draw_rect((int)bullets[i].x - 1, (int)bullets[i].y, 3, 10, C_YELLOW);
            }
        }

        if (state == PLAYING) {
            int scale = std::max(1, game_w / 320);
            draw_rect((int)px - (6 * scale), (int)py + (4 * scale), 12 * scale, 4 * scale, C_BLUE);
            draw_rect((int)px - (2 * scale), (int)py - (4 * scale), 4 * scale, 12 * scale, C_GREEN);
            draw_rect((int)px - (1 * scale), (int)py - (6 * scale), 2 * scale, 2 * scale, C_RED);
        } else {
            // Blinking Title Indicator
            if ((frame_counter / 30) % 2 == 0) {
                int banner_w = game_w * 0.4f;
                int banner_h = 24;
                draw_rect(cx - (banner_w / 2), cy + (game_h * 0.25f), banner_w, banner_h, C_RED);
            }
        }

        // ==========================================
        // 6. ON-SCREEN HUD (RESOLUTION & LOAD MONITOR)
        // ==========================================
        // Lower stress-indicator bar
        int bar_width = (int)(((float)active_particle_target / MAX_PARTICLES) * (game_w - 20));
        draw_rect(10, game_h - 6, bar_width, 3, C_GREEN);
    }
}