include(FetchContent)
# nlohmann/json 3.12.0 (MIT) para leer y escribir escenas. Versión fijada por el tag de la release.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# stb_image / stb_image_write (dominio público o MIT) para leer y escribir PNG. Fijado por commit.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(stb)

# cgltf v1.14 (MIT) para importar modelos glTF/GLB. Un solo archivo; se compila en Model.cpp.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(cgltf)

add_library(ge3d_runtime STATIC
    scene/src/Scene.cpp
    scene/src/Uuid.cpp
    geometry/src/Primitives.cpp
    terrain/src/Voxel.cpp
    terrain/src/Heightmap.cpp
    editing/src/History.cpp
    editing/src/Prefab.cpp
    serialization/src/SceneIO.cpp
    assets/src/Materials.cpp
    assets/src/Image.cpp
    assets/src/Model.cpp
)
add_library(ge3d::runtime ALIAS ge3d_runtime)
target_link_libraries(ge3d_runtime
    PUBLIC ge3d_options
    PRIVATE nlohmann_json::nlohmann_json
)
target_include_directories(ge3d_runtime PUBLIC
    scene/include
    geometry/include
    terrain/include
    editing/include
    serialization/include
    assets/include
)
target_include_directories(ge3d_runtime SYSTEM PRIVATE "${stb_SOURCE_DIR}" "${cgltf_SOURCE_DIR}")
# Generación y mallado de vóxeles son bucles muy calientes: con GCC/Clang se optimizan también en
# Debug para que el editor de desarrollo no sea 6 veces más lento al cargar o esculpir terrenos.
set_source_files_properties(terrain/src/Voxel.cpp terrain/src/Heightmap.cpp assets/src/Model.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2>")
# Image.cpp solo contiene la implementación de stb (código externo): sin avisos.
set_source_files_properties(assets/src/Image.cpp PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")

# --- Scripts: Luau 0.739 (MIT), fijado por tag. Solo la VM y el compilador; sin CLI ni pruebas. ---
set(LUAU_BUILD_CLI OFF CACHE INTERNAL "")
set(LUAU_BUILD_TESTS OFF CACHE INTERNAL "")
set(LUAU_BUILD_WEB OFF CACHE INTERNAL "")
FetchContent_Declare(luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG 0.739
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(luau)
# Código externo: con MinGW, Luau avisa de un atributo de visibilidad que no existe en Windows.
# Las demás bibliotecas de Luau (análisis de tipos, CodeGen, CLI...) no se compilan salvo que algo las use.
foreach(luau_target IN ITEMS Luau.VM Luau.Compiler Luau.Ast Luau.Common Luau.Bytecode Luau.Analysis Luau.CodeGen
                             Luau.Config Luau.Require Luau.RequireNavigator Luau.CLI.lib Luau.EqSat Luau.Inliner)
    if(TARGET ${luau_target})
        get_target_property(luau_type ${luau_target} TYPE)
        if(NOT luau_type STREQUAL "INTERFACE_LIBRARY")
            set_target_properties(${luau_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
            if(NOT MSVC)
                target_compile_options(${luau_target} PRIVATE -w)
            endif()
        endif()
    endif()
endforeach()

# Biblioteca de scripts aparte: Luau no aparece en los headers de escena ni en el resto del runtime.
add_library(ge3d_scripting STATIC scripting/src/ScriptHost.cpp)
add_library(ge3d::scripting ALIAS ge3d_scripting)
target_include_directories(ge3d_scripting PUBLIC scripting/include)
target_link_libraries(ge3d_scripting PUBLIC ge3d::runtime PRIVATE Luau.VM Luau.Compiler ge3d_options)

# --- Física: Jolt Physics v5.3.0 (MIT), fijada por tag. Solo la biblioteca. ---
set(TARGET_UNIT_TESTS OFF CACHE INTERNAL "")
set(TARGET_HELLO_WORLD OFF CACHE INTERNAL "")
set(TARGET_PERFORMANCE_TEST OFF CACHE INTERNAL "")
set(TARGET_SAMPLES OFF CACHE INTERNAL "")
set(TARGET_VIEWER OFF CACHE INTERNAL "")
set(ENABLE_ALL_WARNINGS OFF CACHE INTERNAL "")
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE INTERNAL "")
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE INTERNAL "")
set(CPP_EXCEPTIONS_ENABLED ON CACHE INTERNAL "")
set(CPP_RTTI_ENABLED ON CACHE INTERNAL "")
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE INTERNAL "")
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE INTERNAL "")
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE INTERNAL "")
FetchContent_Declare(jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.3.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(jolt)
if(NOT MSVC)
    # Código externo sin avisos; optimizado también en Debug para que la simulación no sea lenta.
    target_compile_options(Jolt PRIVATE -w $<$<CONFIG:Debug>:-O2>)
endif()

add_library(ge3d_physics STATIC physics/src/Physics.cpp)
add_library(ge3d::physics ALIAS ge3d_physics)
target_include_directories(ge3d_physics PUBLIC physics/include)
target_link_libraries(ge3d_physics PUBLIC ge3d::runtime PRIVATE Jolt ge3d_options)

# --- Audio: miniaudio 0.11.23 (dominio público o MIT-0), fijado por tag. Un solo archivo. ---
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.23
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(miniaudio)
add_library(ge3d_audio STATIC audio/src/Audio.cpp audio/src/miniaudio.cpp)
add_library(ge3d::audio ALIAS ge3d_audio)
target_include_directories(ge3d_audio PUBLIC audio/include)
target_include_directories(ge3d_audio SYSTEM PRIVATE "${miniaudio_SOURCE_DIR}")
target_link_libraries(ge3d_audio PUBLIC ge3d::runtime PRIVATE ge3d_options)
set_source_files_properties(audio/src/miniaudio.cpp PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w;$<$<CONFIG:Debug>:-O2>>")
