cmake_minimum_required(VERSION 3.22)
project(HandPosMod)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(HandPosMod SHARED src/main.cpp)

target_include_directories(HandPosMod PRIVATE include/)

target_link_libraries(HandPosMod android log EGL GLESv3)

target_compile_options(HandPosMod PRIVATE
    -O2 -fvisibility=hidden -ffunction-sections -fdata-sections)

target_link_options(HandPosMod PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,--allow-shlib-undefined
    -Wl,--unresolved-symbols=ignore-all)
