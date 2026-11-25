#pragma once

#include "ge3d/Heightmap.hpp"
#include "ge3d/Uuid.hpp"
#include "ge3d/Voxel.hpp"
#include <array>
#include <map>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
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

// Terreno de malla (rejilla de alturas con capas de material). Igual que los vóxeles, los datos se
// comparten entre copias de la entidad y sus ediciones se deshacen por chunk (HeightmapChange).
struct HeightmapComponent {
    HeightmapSettings settings{};
    std::shared_ptr<Heightmap> data;
    friend bool operator==(const HeightmapComponent&, const HeightmapComponent&) = default;
};

[[nodiscard]] HeightmapComponent makeHeightmap(const HeightmapSettings& settings = {});

// Comportamiento programado en Luau. El script vive en Content (p. ej. "scripts/puerta.luau") y declara
// sus parámetros con valores por defecto; aquí se guardan los valores elegidos en el Inspector.
using ScriptValue = std::variant<double, bool, std::string>;

struct ScriptComponent {
    std::string path;
    bool enabled{true};
    std::map<std::string, ScriptValue> parameters;
    friend bool operator==(const ScriptComponent&, const ScriptComponent&) = default;
};

// Ruta de script válida: relativa, sin "..", con extensión .luau.
[[nodiscard]] bool isValidScriptPath(const std::string& path);

// --- Física y audio (etapa 4) ---------------------------------------------------------------------
// Las piezas (Forma) y los terrenos sin cuerpo físico son colisionadores estáticos: suelo y paredes.
// El componente Cuerpo físico las hace dinámicas (caen y se empujan), cinemáticas (las mueve un script)
// o zonas (no chocan; avisan a los scripts con onTriggerEnter / onTriggerExit).
enum class BodyType { dynamic, kinematic, trigger };

struct BodyComponent {
    BodyType type{BodyType::dynamic};
    float mass{0.0f};           // kg; 0 = según el volumen (caja de madera hueca: 150 kg/m³)
    float friction{0.6f};
    float restitution{0.1f};    // rebote (0-1)
    friend bool operator==(const BodyComponent&, const BodyComponent&) = default;
};

// Sonido en un archivo WAV de Content (p. ej. "sounds/fuente.wav"). Con spatial, suena desde la
// entidad y se atenúa con la distancia al oyente (la cámara o el jugador).
struct AudioSourceComponent {
    std::string clip;
    float volume{1.0f};
    float minDistance{2.0f};    // metros a volumen completo
    float maxDistance{30.0f};   // metros a partir de los que ya no se oye
    bool loop{true};
    bool playOnStart{true};
    bool spatial{true};
    friend bool operator==(const AudioSourceComponent&, const AudioSourceComponent&) = default;
};

// Jugador en primera persona: en Play aparece donde está la entidad y la cámara pasa a sus ojos.
struct PlayerComponent {
    float height{1.75f};
    float radius{0.3f};
    float walkSpeed{4.0f};
    float runSpeed{7.0f};
    float jumpSpeed{5.0f};
    float mouseSensitivity{0.12f};
    friend bool operator==(const PlayerComponent&, const PlayerComponent&) = default;
};

// Ruta de sonido válida: relativa, sin "..", .wav.
[[nodiscard]] bool isValidSoundPath(const std::string& path);

struct Entity {
    EntityId id{};
    Uuid uuid{};
    std::string name;
    // Inactiva: se queda en la escena pero no se dibuja, no choca, no suena y sus scripts no corren.
    bool active{true};
    Transform transform{};
    std::optional<ShapeComponent> shape;
    std::optional<TerrainComponent> terrain;
    std::optional<HeightmapComponent> heightmap;
    std::optional<ScriptComponent> script;
    std::optional<BodyComponent> body;
    std::optional<AudioSourceComponent> audio;
    std::optional<PlayerComponent> player;
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
