#pragma once
#include <cstdint>

// ==========================================
// WASM MACROS
// ==========================================
#define WASM_IMPORT(mod, name) __attribute__((import_module(mod), import_name(name)))
#define WASM_EXPORT(name) __attribute__((export_name(name)))

// ==========================================
// CONSOLE SPECS
// ==========================================
constexpr int WIDTH = 480;
constexpr int HEIGHT = 270;
constexpr int SAMPLE_RATE = 44100;
constexpr int AUDIO_FRAMES_PER_TICK = 735; 

// ==========================================
// BUTTON MASKS (SNES-style layout)
// ==========================================
constexpr uint32_t BTN_UP     = 1;      // UP ARROW
constexpr uint32_t BTN_DOWN   = 2;      // DOWN ARROW
constexpr uint32_t BTN_LEFT   = 4;      // LEFT ARROW
constexpr uint32_t BTN_RIGHT  = 8;      // RIGHT ARROW
constexpr uint32_t BTN_A      = 16;     // X (Physical bottom row)
constexpr uint32_t BTN_B      = 32;     // Z (Physical bottom row)
constexpr uint32_t BTN_X      = 64;     // S (Physical middle row)
constexpr uint32_t BTN_Y      = 128;    // A (Physical middle row)
constexpr uint32_t BTN_START  = 256;    // RETURN
constexpr uint32_t BTN_SELECT = 512;    // RIGHT SHIFT
constexpr uint32_t BTN_L      = 1024;   // Q (Physical top row)
constexpr uint32_t BTN_R      = 2048;   // W (Physical top row)

// ==========================================
// SYSTEM BUFFERS
// ==========================================
inline uint32_t framebuffer[WIDTH * HEIGHT];
inline int16_t audio_buffer[AUDIO_FRAMES_PER_TICK * 2];

// ==========================================
// HOST API IMPORTS
// ==========================================
extern "C" {
    WASM_IMPORT("env", "get_btn_pressed")
    uint32_t get_btn_pressed();

    WASM_IMPORT("env", "get_btn_just_pressed")
    uint32_t get_btn_just_pressed();

    WASM_IMPORT("env", "get_btn_just_released")
    uint32_t get_btn_just_released();
}

// ==========================================
// CARTRIDGE EXPORTS (Engine Boilerplate)
// ==========================================
extern "C" {
    WASM_EXPORT("get_framebuffer_ptr")
    uint32_t* get_framebuffer_ptr() { return framebuffer; }

    WASM_EXPORT("get_audio_ptr")
    int16_t* get_audio_ptr() { return audio_buffer; }

    // Forward declarations - The game developer MUST implement these!
    void init();
    void update();
    void draw();
}