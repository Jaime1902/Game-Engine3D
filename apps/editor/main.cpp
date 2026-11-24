#include "ge3d/Primitives.hpp"
#include <iostream>

int main() {
    auto scene = ge3d::makeStarterScene();
    std::cout << "Game-Engine3D editor bootstrap\n";
    for (const auto& entity : scene.entities()) {
        const auto mesh = ge3d::makePrimitive(entity.primitive);
        std::cout << entity.id << " " << entity.name << " " << mesh.triangles.size()
                  << " triangles\n";
    }
}
