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

add_library(ge3d_runtime STATIC
    scene/src/Scene.cpp
    scene/src/Uuid.cpp
    geometry/src/Primitives.cpp
    terrain/src/Voxel.cpp
    editing/src/History.cpp
    serialization/src/SceneIO.cpp
    assets/src/Materials.cpp
    assets/src/Image.cpp
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
target_include_directories(ge3d_runtime SYSTEM PRIVATE "${stb_SOURCE_DIR}")
# Generación y mallado de vóxeles son bucles muy calientes: con GCC/Clang se optimizan también en
# Debug para que el editor de desarrollo no sea 6 veces más lento al cargar o esculpir terrenos.
set_source_files_properties(terrain/src/Voxel.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2>")
# Image.cpp solo contiene la implementación de stb (código externo): sin avisos.
set_source_files_properties(assets/src/Image.cpp PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>")
