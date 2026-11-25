add_executable(ge3d_texture_gen texture_gen.cpp)
target_link_libraries(ge3d_texture_gen PRIVATE ge3d::runtime)

if(GE3D_ENABLE_VULKAN)
    find_package(Vulkan 1.3 REQUIRED)
    add_executable(ge3d_vulkan_probe vulkan_probe.cpp)
    target_link_libraries(ge3d_vulkan_probe PRIVATE ge3d_options Vulkan::Vulkan)
endif()
