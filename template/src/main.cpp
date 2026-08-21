#include <cstdint>
#include <cstdio>
#include <cmath>

#define WIDTH 480
#define HEIGHT 270

// --- VIDEO ---
uint8_t framebuffer[WIDTH * HEIGHT * 4];

// --- AUDIO ---
const int SAMPLE_RATE = 44100;
const int AUDIO_FRAMES_PER_TICK = 735; // 44100 hz / 60 fps = 735 frames
int16_t audio_buffer[AUDIO_FRAMES_PER_TICK * 2]; // * 2 for Stereo (Left/Right)
float audio_phase = 0.0f;

// --- GAME STATE ---
float box_x = 100.0f;
float box_y = 100.0f;
float vel_x = 4.0f;
float vel_y = 3.0f;
const int BOX_SIZE = 40;

extern "C" {

    // --- VIDEO EXPORTS ---
    __attribute__((export_name("get_framebuffer_ptr")))
    uint8_t* get_framebuffer_ptr() { return framebuffer; }

    // --- AUDIO EXPORTS ---
    __attribute__((export_name("get_audio_ptr")))
    int16_t* get_audio_ptr() { return audio_buffer; }

    __attribute__((export_name("get_audio_size")))
    int get_audio_size() { return AUDIO_FRAMES_PER_TICK * 2 * sizeof(int16_t); } // Total bytes

    // --- LIFECYCLE ---
    __attribute__((export_name("init")))
    void init() {
        printf("Hello from WASM C++! Audio and Video initialized.\n");
    }

    __attribute__((export_name("update")))
    void update() {
        // 1. Move the box
        box_x += vel_x;
        box_y += vel_y;

        if (box_x <= 0 || box_x + BOX_SIZE >= WIDTH) vel_x = -vel_x;
        if (box_y <= 0 || box_y + BOX_SIZE >= HEIGHT) vel_y = -vel_y;

        // 2. Generate Audio (Sine Wave)
        // We link the frequency of the wave to the X position of the box!
        float base_freq = 200.0f; 
        float freq = base_freq + box_x; 
        
        // How much to advance the wave per sample
        float phase_inc = (2.0f * 3.14159265f * freq) / SAMPLE_RATE;

        for (int i = 0; i < AUDIO_FRAMES_PER_TICK; ++i) {
            // Amplitude is 4000 (out of 32767 max) so it's not deafening loud
            int16_t sample = (int16_t)(sin(audio_phase) * 4000.0f);
            
            audio_buffer[i * 2 + 0] = sample; // Left Channel
            audio_buffer[i * 2 + 1] = sample; // Right Channel
            
            audio_phase += phase_inc;
            
            // Keep phase within 0 to 2*Pi to prevent floating point inaccuracy
            if (audio_phase > 2.0f * 3.14159265f) {
                audio_phase -= 2.0f * 3.14159265f;
            }
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

        // Draw the bouncing box (Orange)
        int bx = (int)box_x;
        int by = (int)box_y;

        for (int y = 0; y < BOX_SIZE; ++y) {
            for (int x = 0; x < BOX_SIZE; ++x) {
                int px = bx + x;
                int py = by + y;
                
                if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT) {
                    int index = (py * WIDTH + px) * 4;
                    framebuffer[index + 0] = 255; // R
                    framebuffer[index + 1] = 128; // G
                    framebuffer[index + 2] = 0;   // B
                    framebuffer[index + 3] = 255; // A
                }
            }
        }
    }
}