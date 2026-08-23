import argparse
import sys
import threading
import os
import time
import wasmtime
import miniaudio
import glfw
import skia

# ==========================================
# CONSOLE HARDWARE SPECIFICATIONS
# ==========================================
MAX_RES = 640
SAMPLE_RATE = 44100
CHANNELS = 2
FPS = 60

# 44100 / 60 = 735 samples * 2 channels * 2 bytes = 2940 bytes per tick
AUDIO_BYTES_PER_TICK = int((SAMPLE_RATE / FPS) * CHANNELS * 2)

# ==========================================
# INPUT SUBSYSTEM
# ==========================================
KEY_MAP = {
    glfw.KEY_UP: 1, glfw.KEY_DOWN: 2, glfw.KEY_LEFT: 4, glfw.KEY_RIGHT: 8,
    glfw.KEY_X: 16, glfw.KEY_Z: 32, glfw.KEY_S: 64, glfw.KEY_A: 128,
    glfw.KEY_ENTER: 256, glfw.KEY_RIGHT_SHIFT: 512, glfw.KEY_Q: 1024, glfw.KEY_W: 2048
}

GAMEPAD_MAP = {
    glfw.GAMEPAD_BUTTON_DPAD_UP: 1, glfw.GAMEPAD_BUTTON_DPAD_DOWN: 2, 
    glfw.GAMEPAD_BUTTON_DPAD_LEFT: 4, glfw.GAMEPAD_BUTTON_DPAD_RIGHT: 8,
    glfw.GAMEPAD_BUTTON_A: 16, glfw.GAMEPAD_BUTTON_B: 32, 
    glfw.GAMEPAD_BUTTON_X: 64, glfw.GAMEPAD_BUTTON_Y: 128,
    glfw.GAMEPAD_BUTTON_START: 256, glfw.GAMEPAD_BUTTON_BACK: 512, 
    glfw.GAMEPAD_BUTTON_LEFT_BUMPER: 1024, glfw.GAMEPAD_BUTTON_RIGHT_BUMPER: 2048
}

class AudioRingBuffer:
    def __init__(self, max_bytes=44100 * 4): 
        self.lock = threading.Lock()
        self.buffer = bytearray()
        self.max_bytes = max_bytes

    def write(self, pcm_bytes):
        with self.lock:
            if len(self.buffer) < self.max_bytes:
                self.buffer.extend(pcm_bytes)

    def read_generator(self):
        frames_requested = yield b"" 
        while True:
            bytes_needed = frames_requested * 4 
            with self.lock:
                if len(self.buffer) >= bytes_needed:
                    chunk = bytes(self.buffer[:bytes_needed])
                    del self.buffer[:bytes_needed]
                else:
                    chunk = bytes(self.buffer) + b'\x00' * (bytes_needed - len(self.buffer))
                    self.buffer.clear()
            frames_requested = yield chunk

def calculate_letterbox(win_w, win_h, res_w, res_h):
    scale = min(win_w / res_w, win_h / res_h)
    new_w = res_w * scale
    new_h = res_h * scale
    offset_x = (win_w - new_w) / 2
    offset_y = (win_h - new_h) / 2
    return skia.Rect.MakeXYWH(offset_x, offset_y, new_w, new_h)

def poll_gamepad():
    """Polls GLFW Joystick 1 and maps it to our bitmask."""
    gamepad_mask = 0
    if glfw.joystick_present(glfw.JOYSTICK_1) and glfw.joystick_is_gamepad(glfw.JOYSTICK_1):
        state = glfw.get_gamepad_state(glfw.JOYSTICK_1)
        if state:
            # Map Buttons
            for btn, bitmask in GAMEPAD_MAP.items():
                if state.buttons[btn] == glfw.PRESS:
                    gamepad_mask |= bitmask
            
            # Map Left Analog Stick to D-Pad (with 0.5 deadzone)
            deadzone = 0.5
            if state.axes[glfw.GAMEPAD_AXIS_LEFT_Y] < -deadzone: gamepad_mask |= 1 # UP
            if state.axes[glfw.GAMEPAD_AXIS_LEFT_Y] > deadzone: gamepad_mask |= 2  # DOWN
            if state.axes[glfw.GAMEPAD_AXIS_LEFT_X] < -deadzone: gamepad_mask |= 4 # LEFT
            if state.axes[glfw.GAMEPAD_AXIS_LEFT_X] > deadzone: gamepad_mask |= 8  # RIGHT

    return gamepad_mask

def main():
    parser = argparse.ArgumentParser(description="Pure Skia Fantasy Console")
    parser.add_argument("cart", help="Path to the .wasm cartridge to run")
    args = parser.parse_args()

    cart_path = os.path.abspath(args.cart)
    cart_dir = os.path.dirname(cart_path)

    # ==========================================
    # WASI & WASMTIME SETUP
    # ==========================================
    engine = wasmtime.Engine()
    linker = wasmtime.Linker(engine)
    linker.define_wasi()
    
    wasi_config = wasmtime.WasiConfig()
    wasi_config.inherit_stdout()
    wasi_config.inherit_stderr() 
    wasi_config.preopen_dir(cart_dir, "/", wasmtime.DirPerms.READ_ONLY, wasmtime.FilePerms.READ_ONLY)

    store = wasmtime.Store(engine)
    store.set_wasi(wasi_config)
    
    # ------------------------------------------
    # HOST API FUNCTIONS
    # ------------------------------------------
    input_state = {"pressed": 0, "just_pressed": 0, "just_released": 0}
    console_state = {"res_w": 320, "res_h": 180}

    def host_get_btn_pressed(): return input_state["pressed"]
    def host_get_btn_just_pressed(): return input_state["just_pressed"]
    def host_get_btn_just_released(): return input_state["just_released"]

    def host_set_screen_res(w, h):
        console_state["res_w"] = max(1, min(MAX_RES, w))
        console_state["res_h"] = max(1, min(MAX_RES, h))

    # Bind Inputs
    sig_i32 = wasmtime.FuncType([], [wasmtime.ValType.i32()])
    linker.define_func("env", "get_btn_pressed", sig_i32, host_get_btn_pressed)
    linker.define_func("env", "get_btn_just_pressed", sig_i32, host_get_btn_just_pressed)
    linker.define_func("env", "get_btn_just_released", sig_i32, host_get_btn_just_released)

    # Bind Resolution Controller
    sig_set_res = wasmtime.FuncType([wasmtime.ValType.i32(), wasmtime.ValType.i32()], [])
    linker.define_func("env", "set_screen_res", sig_set_res, host_set_screen_res)

    module = wasmtime.Module.from_file(engine, cart_path)
    instance = linker.instantiate(store, module)
    exports = instance.exports(store)
    
    wasm_memory = exports["memory"]
    wasm_update = exports["update"]
    wasm_draw = exports["draw"]
    get_framebuffer_ptr = exports["get_framebuffer_ptr"]
    
    if "_initialize" in exports: exports["_initialize"](store)
    if "init" in exports: exports["init"](store)

    # ==========================================
    # AUDIO SETUP
    # ==========================================
    ring_buffer = AudioRingBuffer()
    audio_device = miniaudio.PlaybackDevice(
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=CHANNELS,
        sample_rate=SAMPLE_RATE
    )
    audio_gen = ring_buffer.read_generator()
    audio_gen.send(None) 
    audio_device.start(audio_gen)

    # ==========================================
    # GLFW & SKIA SETUP
    # ==========================================
    if not glfw.init():
        sys.exit(1)

    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    
    # Start fullscreen borderless
    monitor = glfw.get_primary_monitor()
    mode = glfw.get_video_mode(monitor)
    window = glfw.create_window(mode.size.width, mode.size.height, "Skia Fantasy Console", monitor, None)
    glfw.make_context_current(window)
    glfw.swap_interval(1) # VSYNC ON

    context = skia.GrDirectContext.MakeGL()

    font = skia.Font(skia.Typeface.MakeFromName("Consolas", skia.FontStyle.Bold()), 24)
    paint_fps = skia.Paint(Color=skia.ColorYELLOW, AntiAlias=True)

    show_fps = True
    keyboard_btn_mask = 0
    prev_btn_mask = 0

    def key_callback(win, key, scancode, action, mods):
        nonlocal keyboard_btn_mask, show_fps
        if key == glfw.KEY_ESCAPE and action == glfw.PRESS:
            glfw.set_window_should_close(win, True)
        elif key == glfw.KEY_F11 and action == glfw.PRESS:
            show_fps = not show_fps
            
        if key in KEY_MAP:
            if action == glfw.PRESS: keyboard_btn_mask |= KEY_MAP[key]
            elif action == glfw.RELEASE: keyboard_btn_mask &= ~KEY_MAP[key]

    glfw.set_key_callback(window, key_callback)

    last_time = time.time()
    frames = 0
    fps_display = "0.0"

    # ==========================================
    # MAIN HARDWARE LOOP
    # ==========================================
    while not glfw.window_should_close(window):
        glfw.poll_events()

        # Combine Keyboard and Gamepad states!
        gamepad_btn_mask = poll_gamepad()
        current_btn_mask = keyboard_btn_mask | gamepad_btn_mask

        input_state["just_pressed"] = current_btn_mask & ~prev_btn_mask
        input_state["just_released"] = ~current_btn_mask & prev_btn_mask
        input_state["pressed"] = current_btn_mask
        prev_btn_mask = current_btn_mask

        # Tick Cartridge
        wasm_update(store)
        wasm_draw(store)

        # Audio Extract
        if "get_audio_ptr" in exports:
            audio_ptr = exports["get_audio_ptr"](store)
            raw_audio = wasm_memory.read(store, audio_ptr, audio_ptr + AUDIO_BYTES_PER_TICK)
            ring_buffer.write(raw_audio)

        # Video Extract
        res_w = console_state["res_w"]
        res_h = console_state["res_h"]
        fb_ptr = get_framebuffer_ptr(store)
        
        # Pull only the exact amount of VRAM needed for the active resolution
        active_vram_bytes = res_w * res_h * 4
        raw_vram = wasm_memory.read(store, fb_ptr, fb_ptr + active_vram_bytes)
        
        vram_info = skia.ImageInfo.Make(res_w, res_h, skia.ColorType.kRGBA_8888_ColorType, skia.AlphaType.kUnpremul_AlphaType)
        skia_image = skia.Image.MakeRasterData(vram_info, skia.Data.MakeWithoutCopy(raw_vram), res_w * 4)

        # Skia GPU Drawing
        win_w, win_h = glfw.get_framebuffer_size(window)
        backend_render_target = skia.GrBackendRenderTarget(
            win_w, win_h, 0, 0, skia.GrGLFramebufferInfo(0, 0x8058)
        )
        surface = skia.Surface.MakeFromBackendRenderTarget(
            context, backend_render_target, skia.kBottomLeft_GrSurfaceOrigin, skia.kRGBA_8888_ColorType, None
        )
        
        canvas = surface.getCanvas()
        canvas.clear(skia.ColorBLACK)
        
        dest_rect = calculate_letterbox(win_w, win_h, res_w, res_h)
        source_rect = skia.Rect.MakeWH(res_w, res_h)
        
        canvas.drawImageRect(skia_image, source_rect, dest_rect, skia.SamplingOptions(skia.FilterMode.kNearest))

        # FPS calculation
        frames += 1
        current_time = time.time()
        if current_time - last_time >= 1.0:
            fps_display = f"FPS: {frames}"
            frames = 0
            last_time = current_time

        if show_fps:
            canvas.drawString(fps_display, dest_rect.right() - 100, dest_rect.top() + 30, font, paint_fps)

        context.flush()
        glfw.swap_buffers(window)

    print("Shutting down Console...")
    try:
        audio_device.stop()
        audio_device.close()
    except: pass

    glfw.terminate()
    os._exit(0)

if __name__ == "__main__":
    main()