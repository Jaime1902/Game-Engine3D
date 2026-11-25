#pragma once

#include "ge3d/Palette.hpp"
#include "ge3d/Uuid.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ge3d {

// Identificador local a la escena abierta. No se guarda: el identificador persistente es el UUID.
using EntityId = std::uint64_t;

enum class Primitive { box, ramp };

struct Vec3 {
    float x{};
    float y{};
    float z{};
    friend bool operator==(const Vec3&, const Vec3&) = default;
};

struct Transform {
    Vec3 position{};
    Vec3 rotationDegrees{};
    Vec3 scale{1, 1, 1};
    friend bool operator==(const Transform&, const Transform&) = default;
};

// Color de cada cara como índice de la paleta. La caja usa las 6; la rampa, las 5 primeras.
// El orden de las caras es el de makePrimitive (ver faceName en Primitives.hpp).
inline constexpr std::size_t maxFaces = 6;
using FaceColors = std::array<PaletteIndex, maxFaces>;

[[nodiscard]] constexpr FaceColors defaultFaceColors(Primitive primitive) {
    const PaletteIndex color = primitive == Primitive::box ? paletteOrange : paletteSeaGreen;
    return {color, color, color, color, color, color};
}

struct Entity {
    EntityId id{};
    Uuid uuid{};
    std::string name;
    Primitive primitive{Primitive::box};
    Transform transform{};
    FaceColors faces = defaultFaceColors(Primitive::box);
    friend bool operator==(const Entity&, const Entity&) = default;
};

class Scene {
public:
    // Crea una entidad con id y UUID nuevos y la añade al final.
    EntityId create(std::string name, Primitive primitive, Transform transform = {});
    // Prepara una entidad con id y UUID nuevos sin añadirla (para comandos de edición).
    [[nodiscard]] Entity makeEntity(std::string name, Primitive primitive, Transform transform = {});
    // Añade una entidad conservando su UUID (o generando uno si es nulo) y le asigna un id local nuevo.
    EntityId add(Entity entity);
    // Inserta una entidad tal cual, con su mismo id, en la posición indicada. Usado por deshacer/rehacer.
    void insert(Entity entity, std::size_t index);
    bool remove(EntityId id);
    void clear() noexcept;

    [[nodiscard]] const std::vector<Entity>& entities() const noexcept;
    [[nodiscard]] Entity* find(EntityId id) noexcept;
    [[nodiscard]] const Entity* find(EntityId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> indexOf(EntityId id) const noexcept;

private:
    std::vector<Entity> entities_;
    EntityId nextId_{1};
};

[[nodiscard]] Scene makeStarterScene();

}
