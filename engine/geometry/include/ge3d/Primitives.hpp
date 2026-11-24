#pragma once

#include "ge3d/Scene.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace ge3d {

struct Triangle {
    std::array<std::uint32_t, 3> indices;
    std::uint32_t face;
};

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Triangle> triangles;
};

[[nodiscard]] Mesh makePrimitive(Primitive primitive);

}
