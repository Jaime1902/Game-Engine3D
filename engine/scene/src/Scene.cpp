#include "ge3d/Scene.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ge3d {

MaterialId defaultMaterial(Primitive primitive) {
    return primitive == Primitive::box ? "piedra" : "adoquin";
}

ShapeComponent makeShape(Primitive primitive) {
    return makeShape(primitive, defaultMaterial(primitive));
}

ShapeComponent makeShape(Primitive primitive, const MaterialId& material) {
    ShapeComponent shape;
    shape.primitive = primitive;
    shape.materials.fill(material);
    return shape;
}

TerrainComponent makeTerrain(const TerrainSettings& settings) {
    return {settings, generateTerrain(settings)};
}

bool isValidScriptPath(const std::string& path) {
    const auto dot = path.rfind('.');
    return !path.empty() && path.size() < 260 && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\' && path.find(':') == std::string::npos && dot != std::string::npos && path.substr(dot) == ".luau";
}

bool isValidSoundPath(const std::string& path) {
    const auto dot = path.rfind('.');
    return !path.empty() && path.size() < 260 && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\' && path.find(':') == std::string::npos && dot != std::string::npos && path.substr(dot) == ".wav";
}

HeightmapComponent makeHeightmap(const HeightmapSettings& settings) {
    return {settings, generateHeightmap(settings)};
}

Entity Scene::makeEntity(std::string name, Transform transform) {
    Entity entity;
    entity.id = nextId_++;
    entity.uuid = Uuid::generate();
    entity.name = std::move(name);
    entity.transform = transform;
    return entity;
}

Entity Scene::makeEntity(std::string name, Primitive primitive, Transform transform) {
    auto entity = makeEntity(std::move(name), transform);
    entity.shape = makeShape(primitive);
    return entity;
}

EntityId Scene::create(std::string name, Primitive primitive, Transform transform) {
    auto entity = makeEntity(std::move(name), primitive, transform);
    const EntityId id = entity.id;
    entities_.push_back(std::move(entity));
    return id;
}

EntityId Scene::add(Entity entity) {
    entity.id = nextId_++;
    if (entity.uuid.isNil()) entity.uuid = Uuid::generate();
    const EntityId id = entity.id;
    entities_.push_back(std::move(entity));
    return id;
}

void Scene::insert(Entity entity, std::size_t index) {
    if (entity.id == 0 || find(entity.id)) {
        throw std::invalid_argument("Scene::insert: id de entidad invalido o repetido");
    }
    nextId_ = std::max(nextId_, entity.id + 1);
    index = std::min(index, entities_.size());
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(index), std::move(entity));
}

bool Scene::remove(EntityId id) {
    const auto it = std::find_if(entities_.begin(), entities_.end(),
                                 [id](const Entity& entity) { return entity.id == id; });
    if (it == entities_.end()) return false;
    entities_.erase(it);
    return true;
}

void Scene::clear() noexcept {
    entities_.clear();
}

Scene Scene::deepCopy() const {
    Scene copy = *this;
    for (auto& entity : copy.entities_) {
        if (entity.terrain && entity.terrain->voxels) {
            entity.terrain->voxels = std::make_shared<VoxelGrid>(*entity.terrain->voxels);
        }
        if (entity.heightmap && entity.heightmap->data) {
            entity.heightmap->data = std::make_shared<Heightmap>(*entity.heightmap->data);
        }
    }
    return copy;
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

const Entity* Scene::find(EntityId id) const noexcept {
    return const_cast<Scene*>(this)->find(id);
}

std::optional<std::size_t> Scene::indexOf(EntityId id) const noexcept {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (entities_[i].id == id) return i;
    }
    return std::nullopt;
}

Scene makeStarterScene() {
    Scene scene;
    const auto floor = scene.create("Floor", Primitive::box, {{0, -0.25f, 0}, {}, {8, 0.5f, 8}});
    scene.find(floor)->shape->materials.fill("cesped");
    scene.create("Crate", Primitive::box, {{0, 0.5f, 0}, {}, {1, 1, 1}});
    scene.create("Ramp", Primitive::ramp, {{2, 0.5f, 1}, {}, {2, 1, 2}});
    return scene;
}

}
