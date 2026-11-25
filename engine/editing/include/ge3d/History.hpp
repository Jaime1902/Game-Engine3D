#pragma once

#include "ge3d/Scene.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace ge3d {

// Un cambio sobre una entidad. before vacío = creación, after vacío = borrado, ambos = modificación.
// index es la posición de la entidad en la escena al crear o borrar.
struct EntityChange {
    std::optional<Entity> before;
    std::optional<Entity> after;
    std::size_t index{};
};

// Cambio de los vóxeles de un chunk de terreno: contenido antes y después (32 KB cada uno).
struct TerrainChange {
    EntityId entity{};
    int chunk{};
    std::vector<VoxelGrid::Voxel> before;
    std::vector<VoxelGrid::Voxel> after;
};

// Cambio de las alturas y capas de un chunk de terreno de malla.
struct HeightmapChange {
    EntityId entity{};
    int chunk{};
    Heightmap::Patch before;
    Heightmap::Patch after;
};

// Una operación de edición que se deshace de una vez (un trazo, un arrastre de gizmo, un duplicado...).
// Al aplicarla primero van los cambios de entidad y después los de terreno; al deshacer, al revés.
struct EditCommand {
    std::string label;
    std::vector<EntityChange> changes;
    std::vector<TerrainChange> terrainChanges;
    std::vector<HeightmapChange> heightmapChanges;
    [[nodiscard]] bool empty() const noexcept { return changes.empty() && terrainChanges.empty() && heightmapChanges.empty(); }
};

class History {
public:
    explicit History(std::size_t limit = 256);

    // Aplica el comando a la escena y lo guarda para deshacer. Borra lo que hubiera para rehacer.
    void execute(Scene& scene, EditCommand command);
    // Guarda un comando cuyos cambios ya están aplicados (p. ej. un arrastre de gizmo ya previsualizado).
    void record(EditCommand command);
    bool undo(Scene& scene);
    bool redo(Scene& scene);
    void clear() noexcept;

    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] const std::string* undoLabel() const noexcept;
    [[nodiscard]] const std::string* redoLabel() const noexcept;
    // Identifica el estado actual del historial. Compararlo con el guardado indica si hay cambios sin guardar.
    [[nodiscard]] std::uint64_t state() const noexcept;

private:
    struct Entry {
        EditCommand command;
        std::uint64_t serial{};
    };
    void push(EditCommand command);

    std::deque<Entry> undo_;
    std::vector<Entry> redo_;
    std::size_t limit_;
    std::uint64_t nextSerial_{1};
};

// Comandos habituales. Devuelven nullopt si no hay nada que hacer.
[[nodiscard]] EditCommand makeCreateCommand(const Scene& scene, Entity entity);
[[nodiscard]] std::optional<EditCommand> makeDuplicateCommand(Scene& scene, EntityId id, Vec3 offset);
[[nodiscard]] std::optional<EditCommand> makeDeleteCommand(const Scene& scene, EntityId id);
// Compara "before" con el estado actual de la misma entidad en la escena.
[[nodiscard]] std::optional<EditCommand> makeModifyCommand(const Scene& scene, const Entity& before, std::string label);

}
