#include "ge3d/Primitives.hpp"
#include <stdexcept>

namespace ge3d {

Mesh makePrimitive(Primitive primitive) {
    constexpr float a = 0.5f;
    switch (primitive) {
    case Primitive::box:
        return {
            {{-a,-a,-a}, {a,-a,-a}, {a,-a,a}, {-a,-a,a},
             {-a,a,-a}, {a,a,-a}, {a,a,a}, {-a,a,a}},
            {{{0,1,2},0}, {{0,2,3},0}, {{4,7,6},1}, {{4,6,5},1},
             {{0,4,5},2}, {{0,5,1},2}, {{3,2,6},3}, {{3,6,7},3},
             {{0,3,7},4}, {{0,7,4},4}, {{1,5,6},5}, {{1,6,2},5}}
        };
    case Primitive::ramp:
        return {
            {{-a,-a,-a}, {a,-a,-a}, {a,-a,a}, {-a,-a,a}, {-a,a,-a}, {a,a,-a}},
            {{{0,1,2},0}, {{0,2,3},0}, {{4,3,2},1}, {{4,2,5},1},
             {{0,4,5},2}, {{0,5,1},2}, {{0,3,4},3}, {{1,5,2},4}}
        };
    }
    throw std::invalid_argument("Unknown primitive");
}

std::uint32_t faceCount(Primitive primitive) noexcept {
    return primitive == Primitive::box ? 6 : 5;
}

const char* faceName(Primitive primitive, std::uint32_t face) noexcept {
    static constexpr const char* box[] = {"Abajo", "Arriba", "Frente", "Fondo", "Izquierda", "Derecha"};
    static constexpr const char* ramp[] = {"Abajo", "Pendiente", "Pared", "Lado izquierdo", "Lado derecho"};
    if (face >= faceCount(primitive)) return "?";
    return primitive == Primitive::box ? box[face] : ramp[face];
}

}
