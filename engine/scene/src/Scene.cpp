#include "ge3d/Scene.hpp"
#include <utility>

namespace ge3d {

EntityId Scene::create(std::string name, Primitive primitive, Transform transform) {
    const EntityId id = nextId_;
    entities_.push_back({id, std::move(name), primitive, transform});
    ++nextId_;
    return id;
}

const std::vector<Entity>& Scene::entities() const noexcept {
    return entities_;
}

Entity* Scene::find(EntityId id) noexcept {
    for (auto& entity : entities_) {
        if (entity.id == id) return &entity;
    }
    return nullptr;
}

Scene makeStarterScene() {
    Scene scene;
    scene.create("Floor", Primitive::box, {{0, -0.25f, 0}, {}, {8, 0.5f, 8}});
    scene.create("Crate", Primitive::box, {{0, 0.5f, 0}, {}, {1, 1, 1}});
    scene.create("Ramp", Primitive::ramp, {{2, 0.5f, 1}, {}, {2, 1, 2}});
    return scene;
}

}
