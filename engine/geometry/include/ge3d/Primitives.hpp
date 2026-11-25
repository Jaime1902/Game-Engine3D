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

// Caras pintables de cada primitiva. Caja: abajo, arriba, frente (-Z), fondo (+Z), izquierda (-X),
// derecha (+X). Rampa: abajo, pendiente, frente (-Z, la pared alta), lado izquierdo, lado derecho.
[[nodiscard]] std::uint32_t faceCount(Primitive primitive) noexcept;
[[nodiscard]] const char* faceName(Primitive primitive, std::uint32_t face) noexcept;

}
