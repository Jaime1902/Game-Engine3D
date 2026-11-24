#include "ge3d/Primitives.hpp"

int main() {
    const auto scene = ge3d::makeStarterScene();
    if (scene.entities().size() != 3 || scene.entities()[0].id == scene.entities()[1].id) {
        return 1;
    }

    const auto box = ge3d::makePrimitive(ge3d::Primitive::box);
    const auto ramp = ge3d::makePrimitive(ge3d::Primitive::ramp);
    if (box.triangles.size() != 12 || ramp.triangles.size() != 8) {
        return 1;
    }
    for (const auto* mesh : {&box, &ramp}) {
        for (const auto& triangle : mesh->triangles) {
            for (const auto index : triangle.indices) {
                if (index >= mesh->positions.size()) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
