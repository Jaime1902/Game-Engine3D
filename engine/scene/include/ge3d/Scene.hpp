#pragma once

#include "ge3d/Uuid.hpp"
#include "ge3d/Voxel.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

// Id de un material del catálogo (Content/materials.json).
using MaterialId = std::string;
inline constexpr std::size_t maxFaces = 6;
using FaceMaterials = std::array<MaterialId, maxFaces>;

// --- Componentes ---------------------------------------------------------------------------------
// Una entidad es un nombre y una Transformación con componentes opcionales. Cada componente se
// guarda, se edita en el Inspector y se deshace por separado. La etapa 3 añadirá Script.

// Pieza sólida paramétrica (caja o rampa) con un material por cara.
// El orden de las caras es el de makePrimitive (ver faceName en Primitives.hpp).
struct ShapeComponent {
    Primitive primitive{Primitive::box};
    FaceMaterials materials{};
    friend bool operator==(const ShapeComponent&, const ShapeComponent&) = default;
};

[[nodiscard]] MaterialId defaultMaterial(Primitive primitive);
[[nodiscard]] ShapeComponent makeShape(Primitive primitive);
[[nodiscard]] ShapeComponent makeShape(Primitive primitive, const MaterialId& material);

// Terreno de vóxeles. Los datos se comparten entre copias de la entidad (deshacer no duplica
// megabytes); las ediciones de vóxeles se deshacen por chunk con TerrainChange (History.hpp).
// Regenerar crea una rejilla nueva, así que deshacerlo es devolver el puntero anterior.
struct TerrainComponent {
    TerrainSettings settings{};
    std::shared_ptr<VoxelGrid> voxels;
    friend bool operator==(const TerrainComponent&, const TerrainComponent&) = default;
};

[[nodiscard]] TerrainComponent makeTerrain(const TerrainSettings& settings = {});

struct Entity {
    EntityId id{};
    Uuid uuid{};
    std::string name;
    Transform transform{};
    std::optional<ShapeComponent> shape;
    std::optional<TerrainComponent> terrain;
    friend bool operator==(const Entity&, const Entity&) = default;
};

class Scene {
public:
    // Crea una pieza con id y UUID nuevos y la añade al final.
    EntityId create(std::string name, Primitive primitive, Transform transform = {});
    // Prepara una entidad con id y UUID nuevos sin añadirla (para comandos de edición).
    [[nodiscard]] Entity makeEntity(std::string name, Transform transform = {});
    [[nodiscard]] Entity makeEntity(std::string name, Primitive primitive, Transform transform = {});
    // Añade una entidad conservando su UUID (o generando uno si es nulo) y le asigna un id local nuevo.
    EntityId add(Entity entity);
    // Inserta una entidad tal cual, con su mismo id, en la posición indicada. Usado por deshacer/rehacer.
    void insert(Entity entity, std::size_t index);
    bool remove(EntityId id);
    void clear() noexcept;
    // Copia independiente: los terrenos se duplican (el modo Play no debe tocar los de edición).
    [[nodiscard]] Scene deepCopy() const;

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
