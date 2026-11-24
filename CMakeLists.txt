cmake_minimum_required(VERSION 3.24)
project(GameEngine3D VERSION 0.1.0 LANGUAGES CXX)

option(GE3D_BUILD_TESTS "Build baseline tests" ON)
option(GE3D_ENABLE_VULKAN "Build Vulkan loader diagnostics" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
add_library(ge3d_options INTERFACE)
target_compile_features(ge3d_options INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(ge3d_options INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(ge3d_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(engine)
add_subdirectory(apps/editor)
add_subdirectory(apps/player)
add_subdirectory(tools)

if(GE3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
