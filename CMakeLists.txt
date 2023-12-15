cmake_minimum_required(VERSION 3.10)

set(CMAKE_TOOLCHAIN_FILE $ENV{PS2SDK}/ps2dev.cmake)

project(ps2-http)

add_executable(ps2-http.elf main.c mongoose.c mongoose.h)

target_include_directories(ps2-http.elf PRIVATE ext/perf)

target_link_libraries(ps2-http.elf
    PRIVATE
    kernel
    patches
    debug.so
    ps2_drivers)
