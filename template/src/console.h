#pragma once
#include <cstdint>

#define WASM_IMPORT(mod, name) __attribute__((import_module(mod), import_name(name)))
#define WASM_EXPORT(name) __attribute__((export_name(name)))

// ==========================================
// CONSOLE SPECS
// ==========================================
constexpr int MAX_RES = 640;
constexpr int SAMPLE_RATE = 44100;
constexpr int AUDIO_FRAMES_PER_TICK = 735; 

// ==========================================
// BUTTON MASKS
// ==========================================
constexpr uint32_t BTN_UP     = 1;
constexpr uint32_t BTN_DOWN   = 2;
constexpr uint32_t BTN_LEFT   = 4;
constexpr uint32_t BTN_RIGHT  = 8;
constexpr uint32_t BTN_A      = 16;
constexpr uint32_t BTN_B      = 32;
constexpr uint32_t BTN_X      = 64;
constexpr uint32_t BTN_Y      = 128;
constexpr uint32_t BTN_START  = 256;

// ==========================================
// SYSTEM BUFFERS
// ==========================================
inline uint32_t framebuffer[MAX_RES * MAX_RES];
inline int16_t audio_buffer[AUDIO_FRAMES_PER_TICK * 2];

extern "C" {
    // Input API
    WASM_IMPORT("env", "get_btn_pressed")       uint32_t get_btn_pressed();
    WASM_IMPORT("env", "get_btn_just_pressed")  uint32_t get_btn_just_pressed();
    WASM_IMPORT("env", "get_btn_just_released") uint32_t get_btn_just_released();

    // Hardware API
    WASM_IMPORT("env", "set_screen_res")        void set_screen_res(int width, int height);

    // Exports
    WASM_EXPORT("get_framebuffer_ptr") uint32_t* get_framebuffer_ptr() { return framebuffer; }
    WASM_EXPORT("get_audio_ptr")       int16_t*  get_audio_ptr() { return audio_buffer; }

    void init();
    void update();
    void draw();
}