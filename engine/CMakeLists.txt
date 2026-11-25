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

add_library(ge3d_runtime STATIC
    scene/src/Scene.cpp
    scene/src/Uuid.cpp
    geometry/src/Primitives.cpp
    editing/src/History.cpp
    serialization/src/SceneIO.cpp
)
add_library(ge3d::runtime ALIAS ge3d_runtime)
target_link_libraries(ge3d_runtime
    PUBLIC ge3d_options
    PRIVATE nlohmann_json::nlohmann_json
)
target_include_directories(ge3d_runtime PUBLIC
    scene/include
    geometry/include
    editing/include
    serialization/include
)
