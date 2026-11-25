#include "ge3d/Primitives.hpp"
#include <iostream>

int main() {
    auto scene = ge3d::makeStarterScene();
    std::cout << "Game-Engine3D editor bootstrap\n";
    for (const auto& entity : scene.entities()) {
        std::cout << entity.id << " " << entity.name;
        if (entity.shape) std::cout << " " << ge3d::makePrimitive(entity.shape->primitive).triangles.size() << " triangles";
        std::cout << '\n';
    }
}
