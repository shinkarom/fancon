#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#define WIDTH 480
#define HEIGHT 270

// --- VIDEO & AUDIO ---
uint8_t framebuffer[WIDTH * HEIGHT * 4];
const int SAMPLE_RATE = 44100;
const int AUDIO_FRAMES_PER_TICK = 735; 
int16_t audio_buffer[AUDIO_FRAMES_PER_TICK * 2];
float audio_phase = 0.0f;

// --- GAME STATE ---
float box_x = 100.0f;
float box_y = 100.0f;
float vel_x = 4.0f; 
float vel_y = 3.0f; 
const int BOX_SIZE = 40;

// --- CONTAINERS (Proves dynamic memory/heap works) ---
struct Point {
    int x, y;
};
std::vector<Point> trail;
std::string file_message = "No message loaded.";

extern "C" {

    __attribute__((export_name("get_framebuffer_ptr")))
    uint8_t* get_framebuffer_ptr() { return framebuffer; }

    __attribute__((export_name("get_audio_ptr")))
    int16_t* get_audio_ptr() { return audio_buffer; }

    __attribute__((export_name("get_audio_size")))
    int get_audio_size() { return AUDIO_FRAMES_PER_TICK * 2 * sizeof(int16_t); }

    // --- LIFECYCLE ---
    __attribute__((export_name("init")))
    void init() {
        printf("\n--- WASM C++ INITIALIZATION ---\n");

        // 1. PROVE CONTAINERS WORK
        trail.reserve(50); // Pre-allocate heap memory
        printf("[OK] std::vector heap allocation successful.\n");

        // 2. PROVE FILESYSTEM WORKS
        // Because Python mapped the cart's folder to "/", we look for "/hello.txt"
        std::ifstream file("/hello.txt");
        
        if (file.is_open()) {
            // Read the first line of the file into our std::string container
            std::getline(file, file_message);
            printf("[OK] File loaded successfully!\n");
            printf("     Contents: \"%s\"\n", file_message.c_str());
            file.close();
        } else {
            printf("[FAIL] Could not open /hello.txt\n");
            printf("       Make sure hello.txt is in the same folder as cart.wasm!\n");
        }
        
        printf("-------------------------------\n\n");
    }

    __attribute__((export_name("update")))
    void update() {
        // 1. Record current position into the vector container
        trail.push_back({(int)box_x, (int)box_y});
        
        // Keep only the last 40 positions in the vector
        if (trail.size() > 40) {
            trail.erase(trail.begin()); 
        }

        // 2. Move the box
        box_x += vel_x;
        box_y += vel_y;

        if (box_x <= 0 || box_x + BOX_SIZE >= WIDTH) vel_x = -vel_x;
        if (box_y <= 0 || box_y + BOX_SIZE >= HEIGHT) vel_y = -vel_y;

        // 3. Audio (unchanged)
        float base_freq = 200.0f; 
        float freq = base_freq + box_x; 
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
            framebuffer[i + 0] = 30; 
            framebuffer[i + 1] = 10; 
            framebuffer[i + 2] = 40; 
            framebuffer[i + 3] = 255;
        }

        // --- DRAW THE C++ CONTAINER TRAIL ---
        // Iterate through our std::vector and draw a fading trail
        for (size_t i = 0; i < trail.size(); ++i) {
            int tx = trail[i].x;
            int ty = trail[i].y;
            
            // Calculate a fading color based on position in the vector
            uint8_t color_intensity = (uint8_t)((float)i / trail.size() * 255.0f);

            // Draw a smaller box for the trail
            for (int y = 10; y < BOX_SIZE - 10; ++y) {
                for (int x = 10; x < BOX_SIZE - 10; ++x) {
                    int px = tx + x;
                    int py = ty + y;
                    
                    if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT) {
                        int index = (py * WIDTH + px) * 4;
                        framebuffer[index + 0] = color_intensity; // R
                        framebuffer[index + 1] = 0;               // G
                        framebuffer[index + 2] = 100;             // B
                        framebuffer[index + 3] = 255;             // A
                    }
                }
            }
        }

        // --- DRAW THE MAIN BOX ---
        int bx = (int)box_x;
        int by = (int)box_y;

        for (int y = 0; y < BOX_SIZE; ++y) {
            for (int x = 0; x < BOX_SIZE; ++x) {
                int px = bx + x;
                int py = by + y;
                
                if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT) {
                    int index = (py * WIDTH + px) * 4;
                    framebuffer[index + 0] = 255; 
                    framebuffer[index + 1] = 128; 
                    framebuffer[index + 2] = 0;   
                    framebuffer[index + 3] = 255; 
                }
            }
        }
    }
}