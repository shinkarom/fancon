cmake_minimum_required(VERSION 3.15)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# 1. FIX THE WARNING: Use the new wasip1 compiler names for WASI SDK 33+
set(CMAKE_C_COMPILER wasm32-wasip1-clang)
set(CMAKE_CXX_COMPILER wasm32-wasip1-clang++)

project(FantasyCartridge CXX C)

# Set a default build type to "Debug" if none is specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_link_options(-mexec-model=reactor)

add_executable(cart src/main.cpp)
set_target_properties(cart PROPERTIES SUFFIX ".wasm")

# ==========================================
# COMPILER / LINKER FLAGS
# ==========================================

# 1. Base flags for ALL builds (Required for WASM)
target_compile_options(cart PRIVATE -fno-exceptions)
target_link_options(cart PRIVATE -fno-exceptions)

# 2. Debug Build: Fast compile times, includes debugging symbols (-g)
# Note: Using -O1 instead of -O0 so the game still runs at a playable framerate.
target_compile_options(cart PRIVATE $<$<CONFIG:Debug>:-O1 -g>)

# 3. Release Build: Slow compile times, maximum optimization
target_compile_options(cart PRIVATE $<$<CONFIG:Release>:-O3 -flto>)
target_link_options(cart PRIVATE $<$<CONFIG:Release>:-O3 -flto>)