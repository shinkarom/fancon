#pragma once
#include <cstdint>

#define WASM_IMPORT(mod, name) __attribute__((import_module(mod), import_name(name)))
#define WASM_EXPORT(name) __attribute__((export_name(name)))

// ==========================================
// CONSOLE SPECS (1080p High-Def Ready)
// ==========================================
constexpr int MAX_RES_W = 1920;
constexpr int MAX_RES_H = 1080;
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
constexpr uint32_t BTN_SELECT = 512;
constexpr uint32_t BTN_L      = 1024;
constexpr uint32_t BTN_R      = 2048;

// ==========================================
// SYSTEM BUFFERS
// ==========================================
// Sized for 1080p (1920 * 1080 = 2,073,600 uint32s = ~8.3 MB)
inline uint32_t framebuffer[MAX_RES_W * MAX_RES_H];
inline int16_t audio_buffer[AUDIO_FRAMES_PER_TICK * 2];

extern "C" {
    // Input API
    WASM_IMPORT("env", "get_btn_pressed")       uint32_t get_btn_pressed();
    WASM_IMPORT("env", "get_btn_just_pressed")  uint32_t get_btn_just_pressed();
    WASM_IMPORT("env", "get_btn_just_released") uint32_t get_btn_just_released();

    // Hardware API
    WASM_IMPORT("env", "set_screen_res")        void set_screen_res(int width, int height);

    // Exports
    WASM_EXPORT("get_framebuffer_ptr") inline uint32_t* get_framebuffer_ptr() { return framebuffer; }
    WASM_EXPORT("get_audio_ptr")       inline int16_t*  get_audio_ptr() { return audio_buffer; }

    void init();
    void update();
    void draw();
}