add_library(ge3d_runtime STATIC
    scene/src/Scene.cpp
    geometry/src/Primitives.cpp
)
add_library(ge3d::runtime ALIAS ge3d_runtime)
target_link_libraries(ge3d_runtime PUBLIC ge3d_options)
target_include_directories(ge3d_runtime PUBLIC scene/include geometry/include)
