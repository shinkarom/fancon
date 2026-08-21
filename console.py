import argparse
import sys
import threading
import os
import pygame
import wasmtime
import miniaudio
import numpy as np

# ==========================================
# CONSOLE HARDWARE SPECIFICATIONS
# ==========================================
RES_W = 480
RES_H = 270
VRAM_SIZE = RES_W * RES_H * 4  # 4 bytes per pixel (32-bit RGBA)

SAMPLE_RATE = 44100
CHANNELS = 2

# ==========================================
# INPUT SUBSYSTEM (HARDWARE SCANCODES)
# ==========================================
# These integers map to the physical QWERTY key locations across all OSes and layouts.
SCANCODE_MAP = {
    82: 1,      # UP ARROW
    81: 2,      # DOWN ARROW
    80: 4,      # LEFT ARROW
    79: 8,      # RIGHT ARROW
    27: 16,     # X (A button - Physical bottom row)
    29: 32,     # Z (B button - Physical bottom row)
    22: 64,     # S (X button - Physical middle row)
    4:  128,    # A (Y button - Physical middle row)
    40: 256,    # RETURN (Start)
    229: 512,   # RIGHT SHIFT (Select)
    20: 1024,   # Q (L Bumper - Physical top row)
    26: 2048    # W (R Bumper - Physical top row)
}

# ==========================================
# AUDIO SUBSYSTEM
# ==========================================
class AudioRingBuffer:
    """A true streaming buffer to handle arbitrary chunk sizes safely."""
    def __init__(self, max_bytes=44100 * 4): 
        self.lock = threading.Lock()
        self.buffer = bytearray()
        self.max_bytes = max_bytes

    def write(self, pcm_bytes):
        with self.lock:
            if len(self.buffer) < self.max_bytes:
                self.buffer.extend(pcm_bytes)

    def read_generator(self):
        """Infinite generator consumed by the miniaudio background thread."""
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

# ==========================================
# VIDEO SUBSYSTEM
# ==========================================
def calculate_letterbox(win_w, win_h):
    """Calculates scaling and integer offsets to maintain a perfect 16:9 aspect ratio."""
    scale = min(win_w / RES_W, win_h / RES_H)
    new_w = int(RES_W * scale)
    new_h = int(RES_H * scale)
    offset_x = (win_w - new_w) // 2
    offset_y = (win_h - new_h) // 2
    return (new_w, new_h), (offset_x, offset_y)


# ==========================================
# MAIN CORE LOOP
# ==========================================
def main():
    # 1. PARSE ARGUMENTS & RESOLVE PATHS
    parser = argparse.ArgumentParser(description="Advanced Arcade Fantasy Console")
    parser.add_argument("cart", help="Path to the .wasm cartridge to run")
    args = parser.parse_args()

    cart_path = os.path.abspath(args.cart)
    cart_dir = os.path.dirname(cart_path)

    if not os.path.exists(cart_path):
        print(f"Error: Cartridge not found at {cart_path}")
        sys.exit(1)

    # 2. INITIALIZE WASMTIME & WASI (GUEST)
    engine = wasmtime.Engine()
    linker = wasmtime.Linker(engine)
    linker.define_wasi()
    
    wasi_config = wasmtime.WasiConfig()
    wasi_config.inherit_stdout()
    wasi_config.inherit_stderr() 
    
    try:
        # Securely lock the guest to Read-Only access for the cartridge directory
        wasi_config.preopen_dir(
            cart_dir, 
            "/", 
            wasmtime.DirPerms.READ_ONLY, 
            wasmtime.FilePerms.READ_ONLY
        )
    except wasmtime.WasmtimeError as e:
        print(f"Failed to map directory {cart_dir} to WASI root: {e}")
        sys.exit(1)

    store = wasmtime.Store(engine)
    store.set_wasi(wasi_config)
    
    # ----------------------------------------------------
    # HOST API: Inject custom Python functions into WASM
    # ----------------------------------------------------
    input_state = {"pressed": 0, "just_pressed": 0, "just_released": 0}

    def host_get_btn_pressed():
        return input_state["pressed"]

    def host_get_btn_just_pressed():
        return input_state["just_pressed"]

    def host_get_btn_just_released():
        return input_state["just_released"]

    sig_return_i32 = wasmtime.FuncType([], [wasmtime.ValType.i32()])

    linker.define_func("env", "get_btn_pressed", sig_return_i32, host_get_btn_pressed)
    linker.define_func("env", "get_btn_just_pressed", sig_return_i32, host_get_btn_just_pressed)
    linker.define_func("env", "get_btn_just_released", sig_return_i32, host_get_btn_just_released)
    # ----------------------------------------------------

    try:
        module = wasmtime.Module.from_file(engine, cart_path)
        instance = linker.instantiate(store, module)
        exports = instance.exports(store)
        
        wasm_memory = exports["memory"]
        wasm_update = exports["update"]
        wasm_draw = exports["draw"]
        get_framebuffer_ptr = exports["get_framebuffer_ptr"]
        
        if "_initialize" in exports:
            exports["_initialize"](store)
            
        if "init" in exports:
            exports["init"](store)
            
    except Exception as e:
        print(f"Failed to load or instantiate '{cart_path}':\n{e}")
        sys.exit(1)

    # 3. INITIALIZE AUDIO
    ring_buffer = AudioRingBuffer()
    audio_device = miniaudio.PlaybackDevice(
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=CHANNELS,
        sample_rate=SAMPLE_RATE
    )
    
    audio_gen = ring_buffer.read_generator()
    audio_gen.send(None) 
    audio_device.start(audio_gen)

    # 4. INITIALIZE PYGAME (HOST DISPLAY)
    pygame.init()
    pygame.font.init() 
    sys_font = pygame.font.SysFont(None, 36) 
    
    info = pygame.display.Info()
    monitor_w = info.current_w
    monitor_h = info.current_h

    # Force Fullscreen, Double Buffering, and VSync
    flags = pygame.FULLSCREEN | pygame.DOUBLEBUF
    screen = pygame.display.set_mode((0, 0), flags, vsync=1)
    
    clock = pygame.time.Clock()
    scaled_size, offset = calculate_letterbox(monitor_w, monitor_h)

    show_fps = True
    prev_btn_mask = 0

    # 5. MAIN EXECUTION LOOP
    running = True
    current_btn_mask = 0
    prev_btn_mask = 0

    while running:
        # --- EVENT HANDLING ---
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                
            elif event.type == pygame.KEYDOWN:
                # 1. Handle System Keys (We use event.key here because Escape/F11 are universal)
                if event.key == pygame.K_ESCAPE:      
                    running = False 
                elif event.key == pygame.K_F11:       
                    show_fps = not show_fps
                
                # 2. Handle Game Input (Using hardware scancodes)
                if event.scancode in SCANCODE_MAP:
                    current_btn_mask |= SCANCODE_MAP[event.scancode]
                    
            elif event.type == pygame.KEYUP:
                # 3. Handle Game Input Release
                if event.scancode in SCANCODE_MAP:
                    current_btn_mask &= ~SCANCODE_MAP[event.scancode]

        # --- CALCULATE INPUT STATES ---
        input_state["just_pressed"] = current_btn_mask & ~prev_btn_mask
        input_state["just_released"] = ~current_btn_mask & prev_btn_mask
        input_state["pressed"] = current_btn_mask
        
        prev_btn_mask = current_btn_mask

        # --- UPDATE & DRAW GUEST ---
        wasm_update(store)
        wasm_draw(store)

        # --- FETCH AUDIO ---
        if "get_audio_ptr" in exports and "get_audio_size" in exports:
            audio_ptr = exports["get_audio_ptr"](store)
            audio_size = exports["get_audio_size"](store)
            raw_audio = wasm_memory.read(store, audio_ptr, audio_ptr + audio_size)
            ring_buffer.write(raw_audio)

        # --- RENDER FRAMEBUFFER ---
        fb_ptr = get_framebuffer_ptr(store)
        raw_vram = wasm_memory.read(store, fb_ptr, fb_ptr + VRAM_SIZE)
        
        pixels = np.frombuffer(raw_vram, dtype=np.uint8).reshape((RES_H, RES_W, 4))
        pixels = np.transpose(pixels, (1, 0, 2))
        fb_surface = pygame.surfarray.make_surface(pixels[:, :, :3])
        
        # --- LETTERBOX SCALING ---
        screen.fill((0, 0, 0)) 
        scaled_fb = pygame.transform.scale(fb_surface, scaled_size) 
        screen.blit(scaled_fb, offset)
        
        # --- RENDER FPS OVERLAY ---
        if show_fps:
            fps = clock.get_fps()
            fps_text = sys_font.render(f" FPS: {fps:.1f} ", True, (255, 255, 0), (0, 0, 0))
            
            safe_x = offset[0] + scaled_size[0] - fps_text.get_width() - 20
            safe_y = offset[1] + 20
            screen.blit(fps_text, (safe_x, safe_y))
        
        pygame.display.flip()
        clock.tick(60) 

    # 6. SHUTDOWN
    audio_device.close()
    pygame.quit()

if __name__ == "__main__":
    main()