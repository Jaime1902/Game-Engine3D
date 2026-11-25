add_executable(ge3d_vulkan_editor main.cpp)
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG c788c52156f3ef7bc7ab769cb03c110a53ac8fcb
)
FetchContent_MakeAvailable(vma)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG 52fe0a05a7b1aa180a202bb24f0f2a049a9c1b7d SOURCE_SUBDIR unused)
FetchContent_Declare(imguizmo GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git GIT_TAG 5b473bc30438dc128520b90b688745bca85c4a91 SOURCE_SUBDIR unused)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863 SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(imgui imguizmo glm)
add_library(ge3d_editor_widgets STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp"
)
target_include_directories(ge3d_editor_widgets SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(ge3d_editor_widgets PRIVATE Vulkan::Vulkan glfw)
target_link_libraries(ge3d_vulkan_editor PRIVATE ge3d::runtime ge3d::scripting ge3d::physics ge3d::audio ge3d_editor_widgets Vulkan::Vulkan glfw GPUOpen::VulkanMemoryAllocator)
target_include_directories(ge3d_vulkan_editor SYSTEM PRIVATE "${glm_SOURCE_DIR}")
target_sources(ge3d_vulkan_editor PRIVATE vma.cpp EditorView.cpp EditorLog.cpp MaterialTextures.cpp TerrainRenderer.cpp EditorScripts.cpp EditorPlay.cpp EditorSettings.cpp EditorSystem.cpp EditorPrefabs.cpp)
# Preferencias del editor en JSON; la papelera y el Explorador usan shell32 en Windows.
target_link_libraries(ge3d_vulkan_editor PRIVATE nlohmann_json::nlohmann_json $<$<PLATFORM_ID:Windows>:shell32>)
# vma.cpp solo compila la implementación de VulkanMemoryAllocator (código externo).
if(NOT MSVC)
    set_source_files_properties(vma.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

find_program(GE3D_SLANGC NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(GE3D_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${GE3D_SHADER_OUTPUT_DIR}")
# Puntos de entrada de los shaders: archivo .slang, nombre de salida, función y etapa.
# Cada uno se compila a <archivo>.<nombre>.spv.
set(GE3D_SHADER_ENTRIES
    "scene|vertex|vertexMain|vertex"
    "scene|voxel|voxelVertexMain|vertex"
    "scene|height|heightVertexMain|vertex"
    "scene|water|waterVertexMain|vertex"
    "scene|fragment|fragmentMain|fragment"
    "post|vertex|postVertexMain|vertex"
    "post|fragment|postFragmentMain|fragment"
)
foreach(entry IN LISTS GE3D_SHADER_ENTRIES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 file)
    list(GET parts 1 name)
    list(GET parts 2 function)
    list(GET parts 3 stage)
    add_custom_command(
        OUTPUT "${GE3D_SHADER_OUTPUT_DIR}/${file}.${name}.spv"
        COMMAND "${GE3D_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${file}.slang"
            -entry ${function} -stage ${stage} -target spirv -matrix-layout-column-major
            -o "${GE3D_SHADER_OUTPUT_DIR}/${file}.${name}.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${file}.slang"
        VERBATIM
    )
    list(APPEND GE3D_SHADER_BINARIES "${GE3D_SHADER_OUTPUT_DIR}/${file}.${name}.spv")
endforeach()
add_custom_target(ge3d_vulkan_shaders DEPENDS ${GE3D_SHADER_BINARIES})
add_dependencies(ge3d_vulkan_editor ge3d_vulkan_shaders)
target_compile_definitions(ge3d_vulkan_editor PRIVATE
    GE3D_SHADER_DIR="${GE3D_SHADER_OUTPUT_DIR}"
    GE3D_DEFAULT_SCENE="${PROJECT_SOURCE_DIR}/projects/pueblo/Content/scenes/main.scene.json"
    GE3D_CONTENT_DIR="${PROJECT_SOURCE_DIR}/projects/pueblo/Content"
)
