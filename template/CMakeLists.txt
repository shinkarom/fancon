cmake_minimum_required(VERSION 3.15)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# 1. FIX THE WARNING: Use the new wasip1 compiler names for WASI SDK 33+
set(CMAKE_C_COMPILER wasm32-wasip1-clang)
set(CMAKE_CXX_COMPILER wasm32-wasip1-clang++)

project(FantasyCartridge CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_link_options(-mexec-model=reactor)

add_executable(cart src/main.cpp)

set_target_properties(cart PROPERTIES SUFFIX ".wasm")

# 2. FIX THE ERROR: Add -fno-exceptions so std::vector doesn't try to use cxa_throw
target_compile_options(cart PRIVATE -O3 -flto -fno-exceptions)
target_link_options(cart PRIVATE -O3 -flto -fno-exceptions)