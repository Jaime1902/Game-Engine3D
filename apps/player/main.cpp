#include "ge3d/Scene.hpp"
#include <iostream>

int main() {
    const auto scene = ge3d::makeStarterScene();
    std::cout << "Game-Engine3D player bootstrap. Entities: " << scene.entities().size() << '\n';
}
