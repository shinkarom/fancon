#include "console.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

// ==========================================
// GAME STATE
// ==========================================
float box_x = 220.0f; // Start roughly in the center
float box_y = 115.0f;
float speed = 4.0f; 
const int BOX_SIZE = 40;
uint8_t box_r = 255; // Red color channel (toggled by 'A' button)

struct Point { int x, y; };
std::vector<Point> trail;

float audio_phase = 0.0f;

// ==========================================
// GAME LIFECYCLE EXPORTS
// ==========================================
extern "C" {

    WASM_EXPORT("init")
    void init() {
        printf("\n--- WASM C++ INITIALIZATION ---\n");

        // 1. PROVE CONTAINERS WORK
        trail.reserve(50);
        printf("[OK] std::vector heap allocation successful.\n");

        // 2. PROVE FILESYSTEM WORKS (Read-Only via WASI)
        std::ifstream file("/hello.txt");
        std::string file_message = "";
        
        if (file.is_open()) {
            std::getline(file, file_message);
            printf("[OK] File loaded successfully!\n");
            printf("     Contents: \"%s\"\n", file_message.c_str());
            file.close();
        } else {
            printf("[FAIL] Could not open /hello.txt\n");
        }
        
        printf("-------------------------------\n");
        printf("Controls:\n");
        printf("  Arrow Keys : Move\n");
        printf("  Q (L Bumper): Hold to Sprint\n");
        printf("  X (A Button): Change Color\n");
        printf("  RETURN (Start): Pause Menu Test\n\n");
    }

    WASM_EXPORT("update")
    void update() {
        // 1. PULL INPUTS FROM THE HOST
        uint32_t pressed = get_btn_pressed();
        uint32_t just_pressed = get_btn_just_pressed();
        uint32_t just_released = get_btn_just_released();

        // 2. RECORD TRAIL
        trail.push_back({(int)box_x, (int)box_y});
        if (trail.size() > 40) trail.erase(trail.begin()); 

        // 3. SPRINT MECHANIC (Test L Bumper)
        if (pressed & BTN_L) {
            speed = 8.0f; // Hold Q to sprint
        } else {
            speed = 4.0f;
        }

        // 4. CONTINUOUS MOVEMENT
        if (pressed & BTN_LEFT)  box_x -= speed;
        if (pressed & BTN_RIGHT) box_x += speed;
        if (pressed & BTN_UP)    box_y -= speed;
        if (pressed & BTN_DOWN)  box_y += speed;

        // Keep box clamped to the screen boundaries
        if (box_x < 0) box_x = 0;
        if (box_x > WIDTH - BOX_SIZE) box_x = WIDTH - BOX_SIZE;
        if (box_y < 0) box_y = 0;
        if (box_y > HEIGHT - BOX_SIZE) box_y = HEIGHT - BOX_SIZE;

        // 5. SINGLE ACTIONS (Triggered once per tap)
        if (just_pressed & BTN_A) {
            box_r = (box_r == 255) ? 0 : 255; // Toggle red channel
            printf("A Button (X Key) Just Pressed!\n");
        }

        if (just_released & BTN_B) {
            printf("B Button (Z Key) Just Released!\n");
        }

        if (just_pressed & BTN_START) {
            printf("START Button (RETURN Key) Pressed! (Imagine a pause menu here)\n");
        }

        // 6. GENERATE AUDIO (Pitch linked to X position)
        float freq = 200.0f + box_x; 
        float phase_inc = (2.0f * 3.14159265f * freq) / SAMPLE_RATE;

        for (int i = 0; i < AUDIO_FRAMES_PER_TICK; ++i) {
            int16_t sample = (int16_t)(sin(audio_phase) * 4000.0f);
            audio_buffer[i * 2 + 0] = sample; 
            audio_buffer[i * 2 + 1] = sample; 
            audio_phase += phase_inc;
            if (audio_phase > 2.0f * 3.14159265f) audio_phase -= 2.0f * 3.14159265f;
        }
    }

    WASM_EXPORT("draw")
    void draw() {
        // 1. Clear screen to dark purple
        for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
            framebuffer[i + 0] = 30;  // R
            framebuffer[i + 1] = 10;  // G
            framebuffer[i + 2] = 40;  // B
            framebuffer[i + 3] = 255; // A
        }

        // 2. Draw the fading trail
        for (size_t i = 0; i < trail.size(); ++i) {
            int tx = trail[i].x;
            int ty = trail[i].y;
            uint8_t alpha = (uint8_t)((float)i / trail.size() * 255.0f);

            for (int y = 10; y < BOX_SIZE - 10; ++y) {
                for (int x = 10; x < BOX_SIZE - 10; ++x) {
                    int index = ((ty + y) * WIDTH + (tx + x)) * 4;
                    framebuffer[index + 0] = box_r; 
                    framebuffer[index + 1] = 0;               
                    framebuffer[index + 2] = 100;             
                    framebuffer[index + 3] = alpha;           
                }
            }
        }

        // 3. Draw the main interactive box
        int bx = (int)box_x;
        int by = (int)box_y;

        for (int y = 0; y < BOX_SIZE; ++y) {
            for (int x = 0; x < BOX_SIZE; ++x) {
                int index = ((by + y) * WIDTH + (bx + x)) * 4;
                framebuffer[index + 0] = box_r; 
                framebuffer[index + 1] = 128; 
                framebuffer[index + 2] = 0;   
                framebuffer[index + 3] = 255; 
            }
        }
    }
}