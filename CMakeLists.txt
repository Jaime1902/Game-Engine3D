cmake_minimum_required(VERSION 3.24)
project(GameEngine3D VERSION 0.1.0 LANGUAGES CXX)

option(GE3D_BUILD_TESTS "Build baseline tests" ON)
option(GE3D_ENABLE_VULKAN "Build Vulkan loader diagnostics" OFF)
option(GE3D_BUILD_VULKAN_EDITOR "Build the GLFW and Vulkan editor window" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)
add_library(ge3d_options INTERFACE)
target_compile_features(ge3d_options INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(ge3d_options INTERFACE /W4 /permissive- /utf-8 /EHsc)
else()
    # -Wno-missing-field-initializers: el patrón de Vulkan `VkFoo info{VK_STRUCTURE_TYPE_FOO};`
    # deja el resto de campos en cero a propósito; sin esto genera cientos de avisos.
    target_compile_options(ge3d_options INTERFACE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
if(MINGW)
    # Enlaza libstdc++/libgcc dentro del .exe: evita cargar por error la DLL de otro MinGW del PATH
    # (p. ej. el de Git), que provoca el error 0xc0000139 al arrancar.
    target_link_options(ge3d_options INTERFACE -static)
endif()

add_subdirectory(engine)
add_subdirectory(apps/editor)
add_subdirectory(apps/player)
add_subdirectory(tools)

if(GE3D_BUILD_VULKAN_EDITOR)
    if(NOT GE3D_ENABLE_VULKAN)
        message(FATAL_ERROR "GE3D_BUILD_VULKAN_EDITOR requires GE3D_ENABLE_VULKAN=ON")
    endif()
    find_package(Vulkan 1.3 REQUIRED)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
    add_subdirectory(apps/vulkan-editor)
endif()

if(GE3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
