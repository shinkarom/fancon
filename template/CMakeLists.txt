cmake_minimum_required(VERSION 3.15)

# 1. Tell CMake we are cross-compiling
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# 2. Hardcode the WASI compilers (Requires wasi-sdk/bin to be in your PATH)
set(CMAKE_C_COMPILER wasm32-wasi-clang)
set(CMAKE_CXX_COMPILER wasm32-wasi-clang++)

# 3. NOW declare the project (CMake will use the compilers set above)
project(FantasyCartridge CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. WASI Reactor configuration (allows host to call init/update/draw)
add_link_options(-mexec-model=reactor)

# 5. Build the cartridge
add_executable(cart src/main.cpp)

# Force the output file extension to be .wasm
set_target_properties(cart PROPERTIES SUFFIX ".wasm")

# Optimize for speed and size
target_compile_options(cart PRIVATE -O3 -flto)
target_link_options(cart PRIVATE -O3 -flto)