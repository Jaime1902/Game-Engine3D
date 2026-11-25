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

// Una operación de edición que se deshace de una vez (un trazo, un arrastre de gizmo, un duplicado...).
struct EditCommand {
    std::string label;
    std::vector<EntityChange> changes;
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
