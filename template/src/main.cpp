#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#define WIDTH 480
#define HEIGHT 270

// ==========================================
// AUDIO & VIDEO BUFFERS
// ==========================================
uint8_t framebuffer[WIDTH * HEIGHT * 4];

const int SAMPLE_RATE = 44100;
const int AUDIO_FRAMES_PER_TICK = 735; 
int16_t audio_buffer[AUDIO_FRAMES_PER_TICK * 2];
float audio_phase = 0.0f;

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

// BUTTON MASKS (Must match Python Host)
const uint32_t BTN_UP    = 1;
const uint32_t BTN_DOWN  = 2;
const uint32_t BTN_LEFT  = 4;
const uint32_t BTN_RIGHT = 8;
const uint32_t BTN_A     = 16;
const uint32_t BTN_B     = 32;

// ==========================================
// HOST API IMPORTS (The "Pull" Model)
// ==========================================
extern "C" {
    // These tell the compiler: Python will provide these functions at runtime!
    __attribute__((import_module("env"), import_name("get_btn_pressed")))
    uint32_t get_btn_pressed();

    __attribute__((import_module("env"), import_name("get_btn_just_pressed")))
    uint32_t get_btn_just_pressed();

    __attribute__((import_module("env"), import_name("get_btn_just_released")))
    uint32_t get_btn_just_released();
}

// ==========================================
// CARTRIDGE EXPORTS (Called by Host)
// ==========================================
extern "C" {

    __attribute__((export_name("get_framebuffer_ptr")))
    uint8_t* get_framebuffer_ptr() { return framebuffer; }

    __attribute__((export_name("get_audio_ptr")))
    int16_t* get_audio_ptr() { return audio_buffer; }

    __attribute__((export_name("get_audio_size")))
    int get_audio_size() { return AUDIO_FRAMES_PER_TICK * 2 * sizeof(int16_t); }

    __attribute__((export_name("init")))
    void init() {
        printf("\n--- WASM C++ INITIALIZATION ---\n");

        // 1. PROVE CONTAINERS WORK
        trail.reserve(50);
        printf("[OK] std::vector heap allocation successful.\n");

        // 2. PROVE FILESYSTEM WORKS (Read-Only)
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
        printf("Controls: Arrow Keys to move, 'X' key (A Button) to change color.\n\n");
    }

    __attribute__((export_name("update")))
    void update() {
        // 1. PULL INPUTS FROM THE HOST
        uint32_t pressed = get_btn_pressed();
        uint32_t just_pressed = get_btn_just_pressed();
        uint32_t just_released = get_btn_just_released();

        // 2. RECORD TRAIL
        trail.push_back({(int)box_x, (int)box_y});
        if (trail.size() > 40) trail.erase(trail.begin()); 

        // 3. CONTINUOUS MOVEMENT
        if (pressed & BTN_LEFT)  box_x -= speed;
        if (pressed & BTN_RIGHT) box_x += speed;
        if (pressed & BTN_UP)    box_y -= speed;
        if (pressed & BTN_DOWN)  box_y += speed;

        // Keep box clamped to the screen
        if (box_x < 0) box_x = 0;
        if (box_x > WIDTH - BOX_SIZE) box_x = WIDTH - BOX_SIZE;
        if (box_y < 0) box_y = 0;
        if (box_y > HEIGHT - BOX_SIZE) box_y = HEIGHT - BOX_SIZE;

        // 4. SINGLE ACTIONS (Triggered once per tap)
        if (just_pressed & BTN_A) {
            box_r = (box_r == 255) ? 0 : 255; // Toggle red channel
            printf("A Button Just Pressed!\n");
        }

        if (just_released & BTN_B) {
            printf("B Button Just Released!\n");
        }

        // 5. GENERATE AUDIO (Pitch linked to X position)
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

    __attribute__((export_name("draw")))
    void draw() {
        // Clear screen to dark purple
        for (int i = 0; i < WIDTH * HEIGHT * 4; i += 4) {
            framebuffer[i + 0] = 30;  // R
            framebuffer[i + 1] = 10;  // G
            framebuffer[i + 2] = 40;  // B
            framebuffer[i + 3] = 255; // A
        }

        // Draw the fading trail
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

        // Draw the main interactive box
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