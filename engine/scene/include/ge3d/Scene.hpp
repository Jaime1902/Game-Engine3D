#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ge3d {

using EntityId = std::uint64_t;

enum class Primitive { box, ramp };

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

struct Transform {
    Vec3 position{};
    Vec3 rotationDegrees{};
    Vec3 scale{1, 1, 1};
};

struct Entity {
    EntityId id;
    std::string name;
    Primitive primitive;
    Transform transform;
};

class Scene {
public:
    EntityId create(std::string name, Primitive primitive, Transform transform = {});
    [[nodiscard]] const std::vector<Entity>& entities() const noexcept;

private:
    std::vector<Entity> entities_;
    EntityId nextId_{1};
};

[[nodiscard]] Scene makeStarterScene();

}
