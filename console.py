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
RES_W = 480
RES_H = 270
VRAM_SIZE = RES_W * RES_H * 4  # 4 bytes per pixel (RGBA)

SAMPLE_RATE = 44100
CHANNELS = 2

# ==========================================
# INPUT SUBSYSTEM (GLFW Key Mappings)
# ==========================================
KEY_MAP = {
    glfw.KEY_UP: 1,
    glfw.KEY_DOWN: 2,
    glfw.KEY_LEFT: 4,
    glfw.KEY_RIGHT: 8,
    glfw.KEY_X: 16,            # A button
    glfw.KEY_Z: 32,            # B button
    glfw.KEY_S: 64,            # X button
    glfw.KEY_A: 128,           # Y button
    glfw.KEY_ENTER: 256,       # Start
    glfw.KEY_RIGHT_SHIFT: 512, # Select
    glfw.KEY_Q: 1024,          # L Bumper
    glfw.KEY_W: 2048           # R Bumper
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

def calculate_letterbox(win_w, win_h):
    scale = min(win_w / RES_W, win_h / RES_H)
    new_w = RES_W * scale
    new_h = RES_H * scale
    offset_x = (win_w - new_w) / 2
    offset_y = (win_h - new_h) / 2
    return skia.Rect.MakeXYWH(offset_x, offset_y, new_w, new_h)

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
    
    input_state = {"pressed": 0, "just_pressed": 0, "just_released": 0}

    def host_get_btn_pressed(): return input_state["pressed"]
    def host_get_btn_just_pressed(): return input_state["just_pressed"]
    def host_get_btn_just_released(): return input_state["just_released"]

    sig_i32 = wasmtime.FuncType([], [wasmtime.ValType.i32()])
    linker.define_func("env", "get_btn_pressed", sig_i32, host_get_btn_pressed)
    linker.define_func("env", "get_btn_just_pressed", sig_i32, host_get_btn_just_pressed)
    linker.define_func("env", "get_btn_just_released", sig_i32, host_get_btn_just_released)

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
    # GLFW & SKIA (GPU BACKEND) SETUP
    # ==========================================
    if not glfw.init():
        sys.exit(1)

    # Request an OpenGL Core profile window
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    
    # Start fullscreen borderless
    monitor = glfw.get_primary_monitor()
    mode = glfw.get_video_mode(monitor)
    window = glfw.create_window(mode.size.width, mode.size.height, "Skia Fantasy Console", monitor, None)
    
    glfw.make_context_current(window)
    glfw.swap_interval(1) # VSYNC ON (Locks to 60 FPS automatically)

    # Bind Skia directly to the OpenGL context
    context = skia.GrDirectContext.MakeGL()

    # Pre-allocate Skia info for the 480x270 WASM buffer
    vram_info = skia.ImageInfo.Make(
        RES_W, RES_H, 
        skia.ColorType.kRGBA_8888_ColorType, 
        skia.AlphaType.kUnpremul_AlphaType
    )
    
    vram_rect = skia.Rect.MakeWH(RES_W, RES_H)

    # FPS Font (Skia native font rendering!)
    font = skia.Font(skia.Typeface.MakeFromName("Consolas", skia.FontStyle.Bold()), 24)
    paint_fps = skia.Paint(Color=skia.ColorYELLOW, AntiAlias=True)

    show_fps = True
    current_btn_mask = 0
    prev_btn_mask = 0

    # GLFW Key Callback
    def key_callback(win, key, scancode, action, mods):
        nonlocal current_btn_mask, show_fps
        if key == glfw.KEY_ESCAPE and action == glfw.PRESS:
            glfw.set_window_should_close(win, True)
        elif key == glfw.KEY_F11 and action == glfw.PRESS:
            show_fps = not show_fps
            
        if key in KEY_MAP:
            if action == glfw.PRESS:
                current_btn_mask |= KEY_MAP[key]
            elif action == glfw.RELEASE:
                current_btn_mask &= ~KEY_MAP[key]

    glfw.set_key_callback(window, key_callback)

    last_time = time.time()
    frames = 0
    fps_display = "0.0"

    # ==========================================
    # MAIN HARDWARE LOOP
    # ==========================================
    while not glfw.window_should_close(window):
        glfw.poll_events()

        # Input tracking
        input_state["just_pressed"] = current_btn_mask & ~prev_btn_mask
        input_state["just_released"] = ~current_btn_mask & prev_btn_mask
        input_state["pressed"] = current_btn_mask
        prev_btn_mask = current_btn_mask

        # Tick Cartridge
        wasm_update(store)
        wasm_draw(store)

        # Audio Extract
        if "get_audio_ptr" in exports and "get_audio_size" in exports:
            audio_ptr = exports["get_audio_ptr"](store)
            audio_size = exports["get_audio_size"](store)
            raw_audio = wasm_memory.read(store, audio_ptr, audio_ptr + audio_size)
            ring_buffer.write(raw_audio)

        # Video Extract
        fb_ptr = get_framebuffer_ptr(store)
        raw_vram = wasm_memory.read(store, fb_ptr, fb_ptr + VRAM_SIZE)
        
        # 1. Map WASM memory directly to Skia (Zero-copy GPU upload)
        skia_image = skia.Image.MakeRasterData(vram_info, skia.Data.MakeWithoutCopy(raw_vram), RES_W * 4)

        # 2. Get the current window size and create a GPU Render Target
        win_w, win_h = glfw.get_framebuffer_size(window)
        backend_render_target = skia.GrBackendRenderTarget(
            win_w, win_h, 0, 0, skia.GrGLFramebufferInfo(0, 0x8058) # 0x8058 = GL_RGBA8
        )
        surface = skia.Surface.MakeFromBackendRenderTarget(
            context, backend_render_target, skia.kBottomLeft_GrSurfaceOrigin,
            skia.kRGBA_8888_ColorType, None
        )
        canvas = surface.getCanvas()

        # 3. GPU Rendering
        canvas.clear(skia.ColorBLACK)
        
        # Draw the WASM buffer scaled to letterbox, using Pixel-Art Nearest Neighbor filtering!
        dest_rect = calculate_letterbox(win_w, win_h)
        canvas.drawImageRect(
            skia_image, vram_rect, dest_rect, 
            skia.SamplingOptions(skia.FilterMode.kNearest)
        )

        # 4. FPS Calculation
        frames += 1
        current_time = time.time()
        if current_time - last_time >= 1.0:
            fps_display = f"FPS: {frames}"
            frames = 0
            last_time = current_time

        if show_fps:
            canvas.drawString(fps_display, dest_rect.right() - 100, dest_rect.top() + 30, font, paint_fps)

        # 5. Flush Skia commands to the GPU and swap the screen buffer
        context.flush()
        glfw.swap_buffers(window)

    # ==========================================
    # SHUTDOWN & CLEANUP
    # ==========================================
    print("Shutting down Console...")
    
    # 1. Stop the audio hardware thread first
    try:
        audio_device.stop()
        audio_device.close()
    except:
        pass

    # 2. Destroy the window context
    glfw.terminate()
    
    # 3. Force the OS to instantly kill the Python process and any stray background threads
    os._exit(0)

if __name__ == "__main__":
    main()