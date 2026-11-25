#include "ge3d/Scene.hpp"

#include <algorithm>
#include <cmath>
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

// --- Rotaciones y jerarquía -------------------------------------------------------------------------

namespace {

struct Quat {
    float x, y, z, w;
};

constexpr float degrees = 3.14159265358979f / 180.0f;

Quat axisAngle(float ax, float ay, float az, float angleDegrees) {
    const float half = angleDegrees * degrees * 0.5f;
    const float s = std::sin(half);
    return {ax * s, ay * s, az * s, std::cos(half)};
}

Quat multiply(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat conjugate(const Quat& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

// Convención del editor (ImGuizmo): primero X, después Y, después Z.
Quat fromEuler(Vec3 r) {
    return multiply(axisAngle(0, 0, 1, r.z), multiply(axisAngle(0, 1, 0, r.y), axisAngle(1, 0, 0, r.x)));
}

Vec3 toEuler(const Quat& q) {
    const float r21 = 2 * (q.y * q.z + q.w * q.x);
    const float r22 = 1 - 2 * (q.x * q.x + q.y * q.y);
    const float r20 = 2 * (q.x * q.z - q.w * q.y);
    const float r10 = 2 * (q.x * q.y + q.w * q.z);
    const float r00 = 1 - 2 * (q.y * q.y + q.z * q.z);
    return {std::atan2(r21, r22) / degrees, std::atan2(-r20, std::sqrt(r21 * r21 + r22 * r22)) / degrees, std::atan2(r10, r00) / degrees};
}

Vec3 rotate(const Quat& q, Vec3 v) {
    // v + 2w (q × v) + 2 q × (q × v)
    const Vec3 u{q.x, q.y, q.z};
    const auto cross = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    const Vec3 t = cross(u, v);
    const Vec3 t2{t.x * 2, t.y * 2, t.z * 2};
    const Vec3 c = cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}

float safeDivide(float a, float b) {
    return std::abs(b) < 1e-6f ? a : a / b;
}

}

Vec3 composeRotations(Vec3 a, Vec3 b) {
    return toEuler(multiply(fromEuler(a), fromEuler(b)));
}

Transform worldTransform(const Scene& scene, const Entity& entity) {
    Transform world = entity.transform;
    const Entity* parent = entity.parent ? scene.find(entity.parent) : nullptr;
    // La profundidad está acotada para no colgarse si alguien construye un ciclo a mano.
    for (int depth = 0; parent && depth < 64; ++depth) {
        const auto& p = parent->transform;
        const Quat q = fromEuler(p.rotationDegrees);
        const Vec3 scaled{world.position.x * p.scale.x, world.position.y * p.scale.y, world.position.z * p.scale.z};
        const Vec3 moved = rotate(q, scaled);
        world.position = {p.position.x + moved.x, p.position.y + moved.y, p.position.z + moved.z};
        world.rotationDegrees = toEuler(multiply(q, fromEuler(world.rotationDegrees)));
        world.scale = {world.scale.x * p.scale.x, world.scale.y * p.scale.y, world.scale.z * p.scale.z};
        parent = parent->parent ? scene.find(parent->parent) : nullptr;
    }
    return world;
}

Transform localTransform(const Scene& scene, EntityId parentId, const Transform& world) {
    const Entity* parent = parentId ? scene.find(parentId) : nullptr;
    if (!parent) return world;
    const Transform p = worldTransform(scene, *parent);
    const Quat inverse = conjugate(fromEuler(p.rotationDegrees));
    const Vec3 offset = rotate(inverse, {world.position.x - p.position.x, world.position.y - p.position.y, world.position.z - p.position.z});
    Transform local;
    local.position = {safeDivide(offset.x, p.scale.x), safeDivide(offset.y, p.scale.y), safeDivide(offset.z, p.scale.z)};
    local.rotationDegrees = toEuler(multiply(inverse, fromEuler(world.rotationDegrees)));
    local.scale = {safeDivide(world.scale.x, p.scale.x), safeDivide(world.scale.y, p.scale.y), safeDivide(world.scale.z, p.scale.z)};
    return local;
}

bool activeInHierarchy(const Scene& scene, const Entity& entity) {
    const Entity* current = &entity;
    for (int depth = 0; current && depth < 64; ++depth) {
        if (!current->active) return false;
        current = current->parent ? scene.find(current->parent) : nullptr;
    }
    return true;
}

std::vector<EntityId> childrenOf(const Scene& scene, EntityId parent) {
    std::vector<EntityId> children;
    for (const auto& entity : scene.entities()) {
        if (entity.parent == parent && entity.id != parent) children.push_back(entity.id);
    }
    return children;
}

std::vector<EntityId> descendantsOf(const Scene& scene, EntityId root) {
    std::vector<EntityId> result;
    std::vector<EntityId> pending{root};
    while (!pending.empty() && result.size() < scene.entities().size()) {
        const auto current = pending.front();
        pending.erase(pending.begin());
        for (const auto child : childrenOf(scene, current)) {
            if (child == root || std::find(result.begin(), result.end(), child) != result.end()) continue;
            result.push_back(child);
            pending.push_back(child);
        }
    }
    return result;
}

bool isAncestor(const Scene& scene, EntityId ancestor, EntityId entity) {
    const Entity* current = scene.find(entity);
    for (int depth = 0; current && current->parent && depth < 64; ++depth) {
        if (current->parent == ancestor) return true;
        current = scene.find(current->parent);
    }
    return false;
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
