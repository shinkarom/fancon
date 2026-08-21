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
# AUDIO SUBSYSTEM
# ==========================================
class AudioRingBuffer:
    """A true streaming buffer to handle arbitrary chunk sizes safely."""
    def __init__(self, max_bytes=44100 * 4): # Max 1 second of audio buffered (44100 samples * 4 bytes)
        self.lock = threading.Lock()
        self.buffer = bytearray()
        self.max_bytes = max_bytes

    def write(self, pcm_bytes):
        with self.lock:
            # Prevent infinite memory growth if audio thread stalls
            if len(self.buffer) < self.max_bytes:
                self.buffer.extend(pcm_bytes)

    def read_generator(self):
        """Infinite generator consumed by the miniaudio background thread."""
        
        # 1. This first yield pauses the generator so it is no longer "just-started".
        # When miniaudio calls .send(framecount) for the first time, it resumes here.
        frames_requested = yield b"" 
        
        while True:
            # 1 frame = 2 channels * 2 bytes (16-bit audio) = 4 bytes
            bytes_needed = frames_requested * 4 
            
            with self.lock:
                if len(self.buffer) >= bytes_needed:
                    # We have enough audio! Slice it off the front.
                    chunk = bytes(self.buffer[:bytes_needed])
                    del self.buffer[:bytes_needed]
                else:
                    # Buffer underrun: drain what we have, pad the rest with silence
                    chunk = bytes(self.buffer) + b'\x00' * (bytes_needed - len(self.buffer))
                    self.buffer.clear()
            
            # 2. Yield the audio to miniaudio, and receive the next frame request
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
    
    # Configure WASI to allow printing to host console and reading the cart directory
    wasi_config = wasmtime.WasiConfig()
    wasi_config.inherit_stdout()
    wasi_config.inherit_stderr() 
    
    try:        
        # Map the folder containing the .wasm file to the root '/' in the guest
        wasi_config.preopen_dir(cart_dir, "/", wasmtime.DirPerms.READ_ONLY, wasmtime.FilePerms.READ_ONLY)
    except wasmtime.WasmtimeError as e:
        print(f"Failed to map directory {cart_dir} to WASI root: {e}")
        sys.exit(1)

    store = wasmtime.Store(engine)
    store.set_wasi(wasi_config)
    
    try:
        module = wasmtime.Module.from_file(engine, cart_path)
        instance = linker.instantiate(store, module)
        exports = instance.exports(store)
        
        # Required WASM Exports
        wasm_memory = exports["memory"]
        wasm_update = exports["update"]
        wasm_draw = exports["draw"]
        get_framebuffer_ptr = exports["get_framebuffer_ptr"]
        
        # Standard WASI Reactor initialization (if exported by compiler)
        if "_initialize" in exports:
            exports["_initialize"](store)
            
        # Custom user initialization
        if "init" in exports:
            exports["init"](store)
            
    except Exception as e:
        print(f"Failed to load or instantiate '{cart_path}':\n{e}")
        print("\nEnsure your WASM file exports:")
        print("  - memory")
        print("  - update()")
        print("  - draw()")
        print("  - get_framebuffer_ptr() -> returns a pointer")
        sys.exit(1)

    # 3. INITIALIZE AUDIO
    ring_buffer = AudioRingBuffer()
    audio_device = miniaudio.PlaybackDevice(
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=CHANNELS,
        sample_rate=SAMPLE_RATE
    )
    
    # Create the generator instance
    audio_gen = ring_buffer.read_generator()
    # Prime the generator strictly (Advances it to the first yield)
    audio_gen.send(None) 
    # Start the device with the primed generator
    audio_device.start(audio_gen)

    # 4. INITIALIZE PYGAME (HOST DISPLAY)
    pygame.init()
    pygame.font.init() 
    sys_font = pygame.font.SysFont(None, 36) 
    
    # Get the user's native monitor resolution
    info = pygame.display.Info()
    monitor_w = info.current_w
    monitor_h = info.current_h

    # Force Fullscreen, Double Buffering, and VSync
    flags = pygame.FULLSCREEN | pygame.DOUBLEBUF
    # Passing (0, 0) tells Pygame to take over the whole monitor
    screen = pygame.display.set_mode((0, 0), flags, vsync=1)
    
    clock = pygame.time.Clock()
    
    # Calculate the perfect letterbox for this specific monitor
    scaled_size, offset = calculate_letterbox(monitor_w, monitor_h)

    show_fps = True

    # 5. MAIN EXECUTION LOOP
    running = True
    while running:
        # --- EVENT HANDLING ---
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                
            elif event.type == pygame.VIDEORESIZE:
                window_size = (event.w, event.h)
                scaled_size, offset = calculate_letterbox(window_size[0], window_size[1])
                
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:      
                    running = False
                elif event.key == pygame.K_F11:       
                    # Toggle the FPS overlay instead of fullscreen
                    show_fps = not show_fps

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
        fps = clock.get_fps()
        pygame.display.set_caption(f"Arcade Core - {os.path.basename(cart_path)}")
        
        if show_fps:
            # Draw yellow text with black background block for high contrast
            fps_text = sys_font.render(f" FPS: {fps:.1f} ", True, (255, 255, 0), (0, 0, 0))
            
            # Position it safely inside the game screen (Top-Right corner)
            # offset[0] is the left black bar, scaled_size[0] is the game width
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