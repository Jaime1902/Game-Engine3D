#include "ge3d/Physics.hpp"

#include "ge3d/Primitives.hpp"
#include "ge3d/Voxel.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>
#include <unordered_map>

namespace ge3d {

namespace {

// --- Capas: lo que no se mueve no choca entre sí ----------------------------------------------------

namespace Layers {
constexpr JPH::ObjectLayer still = 0;
constexpr JPH::ObjectLayer moving = 1;
constexpr JPH::ObjectLayer count = 2;
}

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::moving || b == Layers::moving;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer == Layers::moving ? 1 : 0));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 1 ? "moving" : "still";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        return layer == Layers::moving || broad.GetValue() == 1;
    }
};

void initializeJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();   // vive hasta el final del proceso
        JPH::RegisterTypes();
    });
}

JPH::Quat toQuat(Vec3 degrees) {
    const auto q = eulerToQuaternion(degrees);
    return {q[0], q[1], q[2], q[3]};
}

Vec3 toVec(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}

JPH::Vec3 toJolt(Vec3 v) {
    return {v.x, v.y, v.z};
}

// Transformación del editor (escala, rotación, traslación) aplicada a un punto local.
Vec3 transformPoint(const Transform& t, Vec3 p) {
    const JPH::Vec3 scaled{p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z};
    return toVec(toQuat(t.rotationDegrees) * scaled + toJolt(t.position));
}

Vec3 inverseTransformPoint(const Transform& t, Vec3 p) {
    const JPH::Vec3 local = toQuat(t.rotationDegrees).Conjugated() * (toJolt(p) - toJolt(t.position));
    return {local.GetX() / t.scale.x, local.GetY() / t.scale.y, local.GetZ() / t.scale.z};
}

std::uint64_t pairKey(JPH::BodyID a, JPH::BodyID b) {
    const auto x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
    return x < y ? (static_cast<std::uint64_t>(x) << 32) | y : (static_cast<std::uint64_t>(y) << 32) | x;
}

}

// --- Rotaciones --------------------------------------------------------------------------------------

std::array<float, 4> eulerToQuaternion(Vec3 degrees) {
    constexpr float toRadians = 3.14159265358979f / 180.0f;
    const JPH::Quat q = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), degrees.z * toRadians) *
                        JPH::Quat::sRotation(JPH::Vec3::sAxisY(), degrees.y * toRadians) *
                        JPH::Quat::sRotation(JPH::Vec3::sAxisX(), degrees.x * toRadians);
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

Vec3 quaternionToEuler(const std::array<float, 4>& quaternion) {
    constexpr float toDegrees = 180.0f / 3.14159265358979f;
    const JPH::Mat44 r = JPH::Mat44::sRotation(JPH::Quat(quaternion[0], quaternion[1], quaternion[2], quaternion[3]).Normalized());
    // Igual que ImGuizmo::DecomposeMatrixToComponents.
    const float x = std::atan2(r(2, 1), r(2, 2));
    const float y = std::atan2(-r(2, 0), std::sqrt(r(2, 1) * r(2, 1) + r(2, 2) * r(2, 2)));
    const float z = std::atan2(r(1, 0), r(0, 0));
    return {x * toDegrees, y * toDegrees, z * toDegrees};
}

// --- Mundo ---------------------------------------------------------------------------------------

struct PhysicsWorld::Impl final : public JPH::ContactListener, public JPH::CharacterContactListener {
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairFilter objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobs;
    std::unique_ptr<JPH::PhysicsSystem> system;
    float accumulator{};
    double stepMs{};

    // Qué hay creado por cada entidad, para detectar cambios y retirar lo que sobra.
    enum class Kind { piece, terrain, heightmap };
    struct Tracked {
        Kind kind{};
        std::vector<JPH::BodyID> bodies;               // pieza: 1; terrenos: uno por chunk (o inválido si vacío)
        std::vector<std::uint64_t> revisions;          // terrenos: revisión de cada chunk
        const void* data{};                            // terrenos: rejilla de la que salen
        Transform transform{};
        std::optional<ShapeComponent> shape;
        std::optional<BodyComponent> body;
        float cellSize{};
        bool seen{};
    };
    std::unordered_map<EntityId, Tracked> tracked;
    struct BodyInfo {
        EntityId entity{};
        bool sensor{};
        bool dynamic{};
    };
    std::unordered_map<std::uint32_t, BodyInfo> bodies;   // BodyID -> entidad
    std::set<std::uint64_t> sensorPairs;
    std::vector<PhysicsEvent> events;
    std::mutex eventMutex;

    // Jugador
    JPH::Ref<JPH::CharacterVirtual> character;
    EntityId characterOwner{};
    PlayerComponent player{};
    std::set<EntityId> playerTriggers;
    bool inWater{};
    const Scene* scene{};

    Impl() {
        initializeJolt();
        temp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        jobs = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(65536, 0, 65536, 16384, broadPhaseLayers, objectVsBroadPhase, objectPairs);
        system->SetContactListener(this);
    }
    ~Impl() override {
        character = nullptr;
        auto& bodyInterface = system->GetBodyInterface();
        for (auto& [id, item] : tracked) destroy(item, bodyInterface);
    }

    JPH::BodyInterface& bodyInterface() { return system->GetBodyInterface(); }

    void push(const PhysicsEvent& event) {
        std::lock_guard lock(eventMutex);
        if (events.size() < 512) events.push_back(event);
    }
    const BodyInfo* info(JPH::BodyID id) const {
        const auto it = bodies.find(id.GetIndexAndSequenceNumber());
        return it == bodies.end() ? nullptr : &it->second;
    }

    // Choques y zonas (el sistema de trabajos es de un solo hilo: llega en el hilo principal).
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
        const auto* infoA = info(a.GetID());
        const auto* infoB = info(b.GetID());
        if (!infoA || !infoB) return;
        if (a.IsSensor() || b.IsSensor()) {
            const bool aSensor = a.IsSensor();
            sensorPairs.insert(pairKey(a.GetID(), b.GetID()));
            push({PhysicsEvent::Kind::triggerEnter, aSensor ? infoA->entity : infoB->entity, aSensor ? infoB->entity : infoA->entity, 0});
            return;
        }
        const float speed = (a.GetLinearVelocity() - b.GetLinearVelocity()).Length();
        if (speed > 0.3f) {
            push({PhysicsEvent::Kind::collision, infoA->entity, infoB->entity, speed});
            push({PhysicsEvent::Kind::collision, infoB->entity, infoA->entity, speed});
        }
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        const auto key = pairKey(pair.GetBody1ID(), pair.GetBody2ID());
        if (!sensorPairs.erase(key)) return;
        const auto* infoA = info(pair.GetBody1ID());
        const auto* infoB = info(pair.GetBody2ID());
        if (!infoA || !infoB) return;
        const bool aSensor = infoA->sensor;
        push({PhysicsEvent::Kind::triggerExit, aSensor ? infoA->entity : infoB->entity, aSensor ? infoB->entity : infoA->entity, 0});
    }
    // El jugador choca con algo (para onCollision de los scripts).
    void OnContactAdded(const JPH::CharacterVirtual*, const JPH::BodyID& id, const JPH::SubShapeID&, JPH::RVec3Arg, JPH::Vec3Arg,
                        JPH::CharacterContactSettings&) override {
        if (const auto* other = info(id); other && !other->sensor) {
            push({PhysicsEvent::Kind::collision, characterOwner, other->entity, 0});
            push({PhysicsEvent::Kind::collision, other->entity, characterOwner, 0});
        }
    }

    void destroy(Tracked& item, JPH::BodyInterface& bi) {
        for (auto id : item.bodies) {
            if (id.IsInvalid()) continue;
            bodies.erase(id.GetIndexAndSequenceNumber());
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
        item.bodies.clear();
    }

    JPH::BodyID addBody(JPH::BodyCreationSettings& settings, EntityId entity, bool dynamic) {
        settings.mUserData = entity;
        const auto id = bodyInterface().CreateAndAddBody(settings, dynamic || settings.mMotionType != JPH::EMotionType::Static
                                                                         ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (!id.IsInvalid()) bodies[id.GetIndexAndSequenceNumber()] = {entity, settings.mIsSensor, dynamic};
        return id;
    }

    // Pieza (o cuerpo sin forma, como una caja del tamaño de la escala).
    void createPiece(const Entity& entity, Tracked& item) {
        const auto& t = entity.transform;
        JPH::RefConst<JPH::Shape> shape;
        const auto primitive = entity.shape ? entity.shape->primitive : Primitive::box;
        if (primitive == Primitive::box) {
            const JPH::Vec3 half{std::max(0.01f, t.scale.x * 0.5f), std::max(0.01f, t.scale.y * 0.5f), std::max(0.01f, t.scale.z * 0.5f)};
            const float radius = std::min(0.05f, half.ReduceMin() * 0.5f);
            shape = JPH::BoxShapeSettings(half, radius).Create().Get();
        } else {
            JPH::Array<JPH::Vec3> points;
            for (const auto& p : makePrimitive(primitive).positions) points.push_back({p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z});
            JPH::ConvexHullShapeSettings settings(points, 0.02f);
            const auto result = settings.Create();
            if (result.HasError()) return;
            shape = result.Get();
        }
        auto motion = JPH::EMotionType::Static;
        auto layer = Layers::still;
        bool dynamic = false;
        if (entity.body) {
            dynamic = entity.body->type == BodyType::dynamic;
            motion = dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Kinematic;
            layer = Layers::moving;
        }
        JPH::BodyCreationSettings settings(shape, toJolt(t.position), toQuat(t.rotationDegrees), motion, layer);
        if (entity.body) {
            settings.mFriction = entity.body->friction;
            settings.mRestitution = entity.body->restitution;
            settings.mIsSensor = entity.body->type == BodyType::trigger;
            settings.mCollideKinematicVsNonDynamic = settings.mIsSensor;
            if (dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                // Masa automática: una caja de madera hueca (150 kg/m³), que el jugador puede empujar.
                const float volume = t.scale.x * t.scale.y * t.scale.z * (primitive == Primitive::ramp ? 0.5f : 1.0f);
                settings.mMassPropertiesOverride.mMass = entity.body->mass > 0.0f ? entity.body->mass : std::max(0.1f, volume * 150.0f);
            }
        } else {
            settings.mFriction = 0.8f;
        }
        item.bodies.push_back(addBody(settings, entity.id, dynamic));
    }

    // Malla estática de un chunk de terreno (vértices en el espacio del terreno, ya escalados).
    JPH::BodyID createMeshBody(const Entity& entity, JPH::VertexList& vertices, JPH::IndexedTriangleList& triangles) {
        if (triangles.empty()) return {};
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        const auto result = settings.Create();
        if (result.HasError()) return {};
        JPH::BodyCreationSettings body(result.Get(), toJolt(entity.transform.position), toQuat(entity.transform.rotationDegrees),
                                       JPH::EMotionType::Static, Layers::still);
        body.mFriction = 0.8f;
        return addBody(body, entity.id, false);
    }

    JPH::BodyID createHeightmapChunk(const Entity& entity, int chunk) {
        const auto& map = *entity.heightmap->data;
        const float cell = entity.heightmap->settings.cellSize;
        const auto& s = entity.transform.scale;
        const auto [cx, cz] = map.chunkCoords(chunk);
        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        constexpr int n = Heightmap::chunkVertices;
        for (int z = 0; z < n; ++z) {
            for (int x = 0; x < n; ++x) {
                const int gx = cx * Heightmap::chunkCells + x, gz = cz * Heightmap::chunkCells + z;
                vertices.push_back(JPH::Float3(static_cast<float>(gx) * cell * s.x, map.height(gx, gz) * s.y, static_cast<float>(gz) * cell * s.z));
            }
        }
        for (int z = 0; z < Heightmap::chunkCells; ++z) {
            for (int x = 0; x < Heightmap::chunkCells; ++x) {
                const auto v00 = static_cast<JPH::uint32>(z * n + x), v10 = v00 + 1, v01 = v00 + n, v11 = v00 + n + 1;
                triangles.push_back(JPH::IndexedTriangle(v00, v11, v10));
                triangles.push_back(JPH::IndexedTriangle(v00, v01, v11));
            }
        }
        return createMeshBody(entity, vertices, triangles);
    }

    JPH::BodyID createVoxelChunk(const Entity& entity, int chunk) {
        const auto& grid = *entity.terrain->voxels;
        const float voxel = entity.terrain->settings.voxelSize;
        const auto& s = entity.transform.scale;
        const auto mesh = meshChunk(grid, chunk);
        if (mesh.vertices.empty()) return {};
        const auto coords = grid.chunkCoords(chunk);
        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        for (const auto& v : mesh.vertices) {
            const float x = static_cast<float>((v.position & 63u) + static_cast<unsigned>(coords[0] * VoxelGrid::chunkSize));
            const float y = static_cast<float>(((v.position >> 6) & 63u) + static_cast<unsigned>(coords[1] * VoxelGrid::chunkSize));
            const float z = static_cast<float>(((v.position >> 12) & 63u) + static_cast<unsigned>(coords[2] * VoxelGrid::chunkSize));
            vertices.push_back(JPH::Float3(x * voxel * s.x, y * voxel * s.y, z * voxel * s.z));
        }
        for (JPH::uint32 quad = 0; quad < mesh.quadCount(); ++quad) {
            const JPH::uint32 b = quad * 4;
            triangles.push_back(JPH::IndexedTriangle(b, b + 1, b + 2));
            triangles.push_back(JPH::IndexedTriangle(b, b + 2, b + 3));
        }
        return createMeshBody(entity, vertices, triangles);
    }

    // Crea o actualiza los cuerpos según la escena (cambios de scripts, Inspector o deshacer).
    void reconcile(const Scene& current, float dt) {
        auto& bi = bodyInterface();
        for (auto& [id, item] : tracked) item.seen = false;
        for (const auto& original : current.entities()) {
            if (original.player || !activeInHierarchy(current, original)) continue;   // el jugador es el personaje, no un cuerpo
            // Los cuerpos viven en el mundo: con padres, se usa la transformación combinada.
            Entity worldCopy;
            const bool hasParent = original.parent != 0;
            if (hasParent) {
                worldCopy = original;
                worldCopy.transform = worldTransform(current, original);
            }
            const Entity& entity = hasParent ? worldCopy : original;
            const bool piece = entity.shape.has_value() || entity.body.has_value();
            const bool heightmap = entity.heightmap && entity.heightmap->data;
            const bool voxels = entity.terrain && entity.terrain->voxels;
            if (!piece && !heightmap && !voxels) continue;
            auto [it, created] = tracked.try_emplace(entity.id);
            auto& item = it->second;
            item.seen = true;
            const Kind kind = heightmap ? Kind::heightmap : voxels ? Kind::terrain : Kind::piece;
            const bool scaleChanged = !(item.transform.scale == entity.transform.scale);
            if (!created && (item.kind != kind || scaleChanged || item.shape != entity.shape || item.body != entity.body)) {
                destroy(item, bi);
                created = true;
            }
            if (kind == Kind::piece) {
                if (created) {
                    item = Tracked{kind, {}, {}, nullptr, entity.transform, entity.shape, entity.body, 0.0f, true};
                    createPiece(entity, item);
                    continue;
                }
                if (item.transform == entity.transform || item.bodies.empty() || item.bodies[0].IsInvalid()) continue;
                // Movida desde fuera de la física (script, gizmo, Inspector).
                const auto id = item.bodies[0];
                if (entity.body && entity.body->type != BodyType::dynamic) {
                    bi.MoveKinematic(id, toJolt(entity.transform.position), toQuat(entity.transform.rotationDegrees), dt);
                } else {
                    bi.SetPositionAndRotation(id, toJolt(entity.transform.position), toQuat(entity.transform.rotationDegrees),
                                              entity.body ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
                    if (entity.body) bi.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
                }
                item.transform = entity.transform;
                continue;
            }
            // Terrenos: un cuerpo por chunk, rehecho solo si cambia su revisión.
            const void* data = heightmap ? static_cast<const void*>(entity.heightmap->data.get()) : static_cast<const void*>(entity.terrain->voxels.get());
            const float cell = heightmap ? entity.heightmap->settings.cellSize : entity.terrain->settings.voxelSize;
            const int chunks = heightmap ? entity.heightmap->data->chunkCount() : entity.terrain->voxels->chunkCount();
            if (created || item.data != data || item.cellSize != cell || static_cast<int>(item.bodies.size()) != chunks) {
                destroy(item, bi);
                item = Tracked{kind, std::vector<JPH::BodyID>(static_cast<std::size_t>(chunks)),
                               std::vector<std::uint64_t>(static_cast<std::size_t>(chunks), ~0ull), data, entity.transform, {}, {}, cell, true};
            } else if (!(item.transform == entity.transform)) {
                for (auto id : item.bodies) {
                    if (!id.IsInvalid()) bi.SetPositionAndRotation(id, toJolt(entity.transform.position), toQuat(entity.transform.rotationDegrees), JPH::EActivation::DontActivate);
                }
                item.transform = entity.transform;
            }
            for (int chunk = 0; chunk < chunks; ++chunk) {
                const auto revision = heightmap ? entity.heightmap->data->chunkRevision(chunk) : entity.terrain->voxels->chunkRevision(chunk);
                auto& id = item.bodies[static_cast<std::size_t>(chunk)];
                if (item.revisions[static_cast<std::size_t>(chunk)] == revision) continue;
                if (!id.IsInvalid()) {
                    bodies.erase(id.GetIndexAndSequenceNumber());
                    bi.RemoveBody(id);
                    bi.DestroyBody(id);
                }
                id = heightmap ? createHeightmapChunk(entity, chunk) : createVoxelChunk(entity, chunk);
                item.revisions[static_cast<std::size_t>(chunk)] = revision;
            }
        }
        for (auto it = tracked.begin(); it != tracked.end();) {
            if (!it->second.seen) {
                destroy(it->second, bi);
                it = tracked.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Superficie del agua bajo un punto del mundo (terrenos de malla con agua), si la hay.
    std::optional<float> waterSurface(Vec3 point) const {
        if (!scene) return std::nullopt;
        for (const auto& entity : scene->entities()) {
            if (!entity.heightmap || !entity.heightmap->data || !activeInHierarchy(*scene, entity)) continue;
            const auto& map = *entity.heightmap->data;
            const float cell = entity.heightmap->settings.cellSize;
            const Transform world = worldTransform(*scene, entity);
            const Vec3 local = inverseTransformPoint(world, point);
            const int x = static_cast<int>(std::lround(local.x / cell)), z = static_cast<int>(std::lround(local.z / cell));
            if (!map.contains(x, z)) continue;
            const float depth = map.water(x, z);
            if (depth < 0.02f) continue;
            return transformPoint(world, {local.x, map.height(x, z) + depth, local.z}).y;
        }
        return std::nullopt;
    }

    void applyBuoyancy(float dt) {
        auto& bi = bodyInterface();
        for (const auto& [index, body] : bodies) {
            if (!body.dynamic) continue;
            const JPH::BodyID id(index);
            if (!bi.IsActive(id)) continue;
            const Vec3 position = toVec(bi.GetCenterOfMassPosition(id));
            const auto surface = waterSurface(position);
            if (!surface) continue;
            bi.ApplyBuoyancyImpulse(id, JPH::RVec3(position.x, *surface, position.z), JPH::Vec3::sAxisY(), 1.6f, 0.6f, 0.08f,
                                    JPH::Vec3::sZero(), system->GetGravity(), dt);
        }
    }

    void createCharacter(const Entity& original, const Scene& owner) {
        Entity entity = original;
        entity.transform = worldTransform(owner, original);
        player = *entity.player;
        characterOwner = entity.id;
        const float halfCylinder = std::max(0.05f, player.height * 0.5f - player.radius);
        const JPH::RefConst<JPH::Shape> shape =
            JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0, player.height * 0.5f, 0), JPH::Quat::sIdentity(),
                                                new JPH::CapsuleShape(halfCylinder, player.radius))
                .Create()
                .Get();
        JPH::CharacterVirtualSettings settings;
        settings.mShape = shape;
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        settings.mMass = 70.0f;
        settings.mMaxStrength = 2500.0f;  // fuerza para empujar cajas (N)
        settings.mCharacterPadding = 0.02f;
        settings.mPenetrationRecoverySpeed = 1.0f;
        settings.mPredictiveContactDistance = 0.1f;
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -player.radius);
        settings.mInnerBodyShape = shape;
        settings.mInnerBodyLayer = Layers::moving;
        // Solo el giro en Y: el personaje siempre está de pie.
        constexpr float toRadians = 3.14159265358979f / 180.0f;
        const JPH::Quat rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), entity.transform.rotationDegrees.y * toRadians);
        character = new JPH::CharacterVirtual(&settings, toJolt(entity.transform.position), rotation, entity.id, system.get());
        character->SetListener(this);
        bodies[character->GetInnerBodyID().GetIndexAndSequenceNumber()] = {entity.id, false, false};
    }

    // Filtro del personaje y de los rayos: ni zonas ni el propio cuerpo del jugador.
    class SolidFilter final : public JPH::BodyFilter {
    public:
        explicit SolidFilter(JPH::BodyID ignore) : ignore_(ignore) {}
        bool ShouldCollide(const JPH::BodyID& id) const override { return id != ignore_; }
        bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }

    private:
        JPH::BodyID ignore_;
    };

    void stepCharacter(const CharacterInput& input, float dt) {
        if (!character) return;
        const JPH::Vec3 gravity = system->GetGravity();
        const Vec3 feet = toVec(character->GetPosition());
        const auto surface = waterSurface({feet.x, feet.y + player.height * 0.5f, feet.z});
        inWater = surface && *surface > feet.y + player.height * 0.45f;
        const bool grounded = character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        float speed = input.run ? player.runSpeed : player.walkSpeed;
        if (inWater) speed *= 0.5f;
        JPH::Vec3 horizontal(input.move.x * speed, 0, input.move.z * speed);
        JPH::Vec3 velocity = character->GetLinearVelocity();
        float vertical = velocity.GetY();
        if (inWater) {
            // Nadar: casi sin peso, Espacio sube.
            vertical = vertical * 0.9f + gravity.GetY() * 0.1f * dt;
            if (input.jump) vertical = std::max(vertical, 2.5f);
        } else if (grounded) {
            vertical = character->GetGroundVelocity().GetY();
            if (input.jump) vertical = player.jumpSpeed;
        } else {
            vertical += gravity.GetY() * dt;
        }
        character->SetLinearVelocity(horizontal + JPH::Vec3(0, vertical, 0));
        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp = JPH::Vec3(0, 0.45f, 0);
        update.mStickToFloorStepDown = inWater ? JPH::Vec3::sZero() : JPH::Vec3(0, -0.5f, 0);
        const SolidFilter filter(character->GetInnerBodyID());
        character->ExtendedUpdate(dt, inWater ? gravity * 0.1f : gravity, update,
                                  system->GetDefaultBroadPhaseLayerFilter(Layers::moving),
                                  system->GetDefaultLayerFilter(Layers::moving), filter, {}, *temp);
    }

    // Zonas que contienen al jugador: el personaje virtual no las activa solo, se comprueba su centro.
    void checkPlayerTriggers() {
        if (!character) return;
        const Vec3 feet = toVec(character->GetPosition());
        const JPH::RVec3 center(feet.x, feet.y + player.height * 0.5f, feet.z);
        JPH::AllHitCollisionCollector<JPH::CollidePointCollector> collector;
        system->GetNarrowPhaseQuery().CollidePoint(center, collector);
        std::set<EntityId> inside;
        for (const auto& hit : collector.mHits) {
            if (const auto* body = info(hit.mBodyID); body && body->sensor) inside.insert(body->entity);
        }
        for (const auto zone : inside) {
            if (!playerTriggers.contains(zone)) push({PhysicsEvent::Kind::triggerEnter, zone, characterOwner, 0});
        }
        for (const auto zone : playerTriggers) {
            if (!inside.contains(zone)) push({PhysicsEvent::Kind::triggerExit, zone, characterOwner, 0});
        }
        playerTriggers = std::move(inside);
    }

    // Cuerpos dinámicos -> escena.
    void writeBack(Scene& current) {
        auto& bi = bodyInterface();
        for (auto& [entityId, item] : tracked) {
            if (item.kind != Kind::piece || !item.body || item.body->type != BodyType::dynamic || item.bodies.empty()) continue;
            const auto id = item.bodies[0];
            if (id.IsInvalid() || !bi.IsActive(id)) continue;
            auto* entity = current.find(entityId);
            if (!entity) continue;
            const JPH::Quat q = bi.GetRotation(id);
            // item.transform está en el mundo; la entidad guarda la transformación relativa a su padre.
            Transform world = item.transform;
            world.position = toVec(bi.GetPosition(id));
            world.rotationDegrees = quaternionToEuler({q.GetX(), q.GetY(), q.GetZ(), q.GetW()});
            entity->transform = localTransform(current, entity->parent, world);
            item.transform = world;
        }
        if (character) {
            if (auto* entity = current.find(characterOwner)) {
                Transform world = worldTransform(current, *entity);
                world.position = toVec(character->GetPosition());
                entity->transform.position = localTransform(current, entity->parent, world).position;
            }
        }
    }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::build(const Scene& scene) {
    impl_->scene = &scene;
    for (const auto& entity : scene.entities()) {
        if (entity.player && activeInHierarchy(scene, entity)) {
            impl_->createCharacter(entity, scene);
            break;
        }
    }
    impl_->reconcile(scene, fixedStep);
    impl_->system->OptimizeBroadPhase();
}

void PhysicsWorld::step(Scene& scene, float frameSeconds, const CharacterInput& input) {
    auto& impl = *impl_;
    impl.scene = &scene;
    const auto start = std::chrono::steady_clock::now();
    impl.reconcile(scene, fixedStep);
    // Acumulador con tope (guidelines 6.2): nunca más de 6 pasos en un fotograma lento.
    impl.accumulator = std::min(impl.accumulator + std::max(0.0f, frameSeconds), 0.1f);
    while (impl.accumulator >= fixedStep) {
        impl.stepCharacter(input, fixedStep);
        impl.applyBuoyancy(fixedStep);
        impl.system->Update(fixedStep, 1, impl.temp.get(), impl.jobs.get());
        impl.accumulator -= fixedStep;
    }
    impl.checkPlayerTriggers();
    impl.writeBack(scene);
    impl.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::vector<PhysicsEvent> PhysicsWorld::takeEvents() {
    std::lock_guard lock(impl_->eventMutex);
    return std::exchange(impl_->events, {});
}

bool PhysicsWorld::hasCharacter() const noexcept {
    return impl_->character != nullptr;
}

EntityId PhysicsWorld::characterEntity() const noexcept {
    return impl_->characterOwner;
}

CharacterState PhysicsWorld::character() const {
    CharacterState state;
    if (!impl_->character) return state;
    state.feet = toVec(impl_->character->GetPosition());
    state.eye = {state.feet.x, state.feet.y + impl_->player.height * 0.92f, state.feet.z};
    state.velocity = toVec(impl_->character->GetLinearVelocity());
    state.grounded = impl_->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    state.inWater = impl_->inWater;
    return state;
}

namespace {
JPH::BodyID dynamicBody(const PhysicsWorld::Impl& impl, EntityId entity) {
    const auto it = impl.tracked.find(entity);
    if (it == impl.tracked.end() || !it->second.body || it->second.body->type != BodyType::dynamic || it->second.bodies.empty()) return {};
    return it->second.bodies[0];
}
}

std::optional<Vec3> PhysicsWorld::velocity(EntityId entity) const {
    if (entity == impl_->characterOwner && impl_->character) return toVec(impl_->character->GetLinearVelocity());
    const auto id = dynamicBody(*impl_, entity);
    if (id.IsInvalid()) return std::nullopt;
    return toVec(impl_->system->GetBodyInterface().GetLinearVelocity(id));
}

void PhysicsWorld::setVelocity(EntityId entity, Vec3 velocity) {
    const auto id = dynamicBody(*impl_, entity);
    if (!id.IsInvalid()) impl_->system->GetBodyInterface().SetLinearVelocity(id, toJolt(velocity));
}

void PhysicsWorld::applyImpulse(EntityId entity, Vec3 impulse) {
    const auto id = dynamicBody(*impl_, entity);
    if (!id.IsInvalid()) impl_->system->GetBodyInterface().AddImpulse(id, toJolt(impulse));
}

std::optional<std::pair<EntityId, float>> PhysicsWorld::raycast(Vec3 from, Vec3 direction, float maxDistance) const {
    const JPH::Vec3 dir = toJolt(direction).NormalizedOr(JPH::Vec3::sAxisZ()) * maxDistance;
    const JPH::RRayCast ray(toJolt(from), dir);
    JPH::RayCastResult hit;
    const Impl::SolidFilter filter(impl_->character ? impl_->character->GetInnerBodyID() : JPH::BodyID());
    if (!impl_->system->GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return std::nullopt;
    const auto* body = impl_->info(hit.mBodyID);
    if (!body) return std::nullopt;
    return std::pair{body->entity, hit.mFraction * maxDistance};
}

PhysicsWorld::Stats PhysicsWorld::stats() const noexcept {
    Stats stats;
    stats.bodies = static_cast<int>(impl_->bodies.size());
    for (const auto& [index, body] : impl_->bodies) {
        if (!body.dynamic) continue;
        ++stats.dynamic;
        if (impl_->system->GetBodyInterface().IsActive(JPH::BodyID(index))) ++stats.active;
    }
    stats.stepMs = impl_->stepMs;
    return stats;
}

}
