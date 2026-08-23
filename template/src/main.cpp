#include "console.h"
#include <cmath>
#include <cstdlib>

// ==========================================
// COLOR PALETTE (0xAABBGGRR)
// ==========================================
constexpr uint32_t C_BLACK  = 0xFF000000;
constexpr uint32_t C_WHITE  = 0xFFFFFFFF;
constexpr uint32_t C_RED    = 0xFF0000FF;
constexpr uint32_t C_GREEN  = 0xFF00FF00;
constexpr uint32_t C_YELLOW = 0xFF00FFFF;
constexpr uint32_t C_GRAY   = 0xFF444444;

// ==========================================
// GAME STATE
// ==========================================
int game_w = 320;
int game_h = 180;

enum GameState { ATTRACT_MODE, PLAYING };
GameState state = ATTRACT_MODE;

int frame_counter = 0;
float px = 160.0f;
float py = 150.0f;
constexpr float SPEED = 3.0f;

bool b_active = false;
float bx = 0, by = 0;
float b_audio_pitch = 0;

struct Star { float x, y, speed; };
Star stars[50];

// ==========================================
// RENDER HELPERS
// ==========================================
void draw_rect(float fx, float fy, int w, int h, uint32_t color) {
    int start_x = (int)fx;
    int start_y = (int)fy;
    
    for (int y = 0; y < h; ++y) {
        int py = start_y + y;
        if (py < 0 || py >= game_h) continue; 

        for (int x = 0; x < w; ++x) {
            int px = start_x + x;
            if (px < 0 || px >= game_w) continue; 
            
            // Tightly pack the pixels based on current active width
            framebuffer[py * game_w + px] = color;
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

        for (int i = 0; i < 50; ++i) {
            stars[i].x = (float)(rand() % game_w);
            stars[i].y = (float)(rand() % game_h);
            stars[i].speed = ((rand() % 10) / 10.0f) + 0.2f;
        }
    }

    WASM_EXPORT("update")
    void update() {
        frame_counter++;
        uint32_t pressed = get_btn_pressed();
        uint32_t just_pressed = get_btn_just_pressed();

        // --- Resolution Testing ---
        if (just_pressed & BTN_X) {
            game_w = 640; game_h = 480; // High-Res
            set_screen_res(game_w, game_h);
        }
        if (just_pressed & BTN_Y) {
            game_w = 160; game_h = 90;  // Zoomed-in Chunky
            set_screen_res(game_w, game_h);
        }

        // --- Arcade Logic ---
        if (state == ATTRACT_MODE) {
            if (just_pressed & BTN_START) {
                state = PLAYING;
                px = game_w / 2.0f;
                py = game_h - 30.0f;
                b_active = false;
            }
        } 
        else if (state == PLAYING) {
            if (pressed & BTN_LEFT)  px -= SPEED;
            if (pressed & BTN_RIGHT) px += SPEED;
            if (pressed & BTN_UP)    py -= SPEED;
            if (pressed & BTN_DOWN)  py += SPEED;
            
            // Keep on screen
            if (px < 10) px = 10;
            if (px > game_w - 10) px = game_w - 10;
            if (py < 10) py = 10;
            if (py > game_h - 10) py = game_h - 10;

            if ((just_pressed & BTN_A) && !b_active) {
                b_active = true;
                bx = px;
                by = py - 10;
                b_audio_pitch = 800.0f;
            }

            if (b_active) {
                by -= 6.0f;
                b_audio_pitch -= 40.0f;
                if (by < 0) b_active = false;
            }
        }

        for (int i = 0; i < 50; ++i) {
            stars[i].y += stars[i].speed;
            if (stars[i].y > game_h) {
                stars[i].y = 0;
                stars[i].x = (float)(rand() % game_w);
            }
        }

        // --- Audio Synthesizer ---
        static float phase = 0.0f;
        for (int i = 0; i < AUDIO_FRAMES_PER_TICK; ++i) {
            int16_t sample = 0;
            float hum_freq = (state == PLAYING) ? 60.0f : 40.0f;
            sample += (int16_t)(sin(phase * hum_freq) * 1000.0f);

            if (b_active && b_audio_pitch > 0) {
                sample += (sin(phase * b_audio_pitch) > 0) ? 4000 : -4000; 
            }

            audio_buffer[i * 2 + 0] = sample; 
            audio_buffer[i * 2 + 1] = sample; 
            
            phase += (2.0f * 3.14159f) / SAMPLE_RATE;
            if (phase > 100.0f) phase -= 100.0f; 
        }
    }

    WASM_EXPORT("draw")
    void draw() {
        // Clear Active Screen
        for (int i = 0; i < game_w * game_h; ++i) {
            framebuffer[i] = C_BLACK;
        }

        // Draw Parallax Stars
        for (int i = 0; i < 50; ++i) {
            uint32_t star_color = (stars[i].speed > 0.8f) ? C_WHITE : C_GRAY;
            draw_rect(stars[i].x, stars[i].y, 1, 1, star_color);
        }

        // Draw Game
        if (state == PLAYING) {
            if (b_active) draw_rect(bx - 1, by, 2, 8, C_YELLOW);

            draw_rect(px - 6, py + 4, 12, 4, C_GRAY);   
            draw_rect(px - 2, py - 4, 4,  12, C_GREEN); 
            draw_rect(px - 1, py - 6, 2,  2,  C_RED);   
        } 
        else if (state == ATTRACT_MODE) {
            if ((frame_counter / 30) % 2 == 0) {
                draw_rect((game_w/2.0f) - 40, (game_h/2.0f) - 10, 80, 20, C_RED);
                draw_rect((game_w/2.0f) - 38, (game_h/2.0f) - 8,  76, 16, C_YELLOW);
                draw_rect((game_w/2.0f) - 36, (game_h/2.0f) - 6,  72, 12, C_BLACK);
            }
        }
    }
}