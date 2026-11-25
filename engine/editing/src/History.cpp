#include "ge3d/History.hpp"

#include <stdexcept>
#include <utility>

namespace ge3d {

namespace {

void applyChange(Scene& scene, const EntityChange& change, bool forward) {
    const auto& from = forward ? change.before : change.after;
    const auto& to = forward ? change.after : change.before;
    if (from && to) {
        auto* entity = scene.find(from->id);
        if (!entity) throw std::logic_error("History: la entidad a modificar no existe");
        *entity = *to;
    } else if (to) {
        scene.insert(*to, change.index);
    } else if (from) {
        if (!scene.remove(from->id)) throw std::logic_error("History: la entidad a borrar no existe");
    }
}

void apply(Scene& scene, const EditCommand& command, bool forward) {
    if (forward) {
        for (const auto& change : command.changes) applyChange(scene, change, true);
    } else {
        for (auto it = command.changes.rbegin(); it != command.changes.rend(); ++it) applyChange(scene, *it, false);
    }
}

}

History::History(std::size_t limit) : limit_(limit == 0 ? 1 : limit) {}

void History::push(EditCommand command) {
    redo_.clear();
    undo_.push_back({std::move(command), nextSerial_++});
    while (undo_.size() > limit_) undo_.pop_front();
}

void History::execute(Scene& scene, EditCommand command) {
    if (command.changes.empty()) return;
    apply(scene, command, true);
    push(std::move(command));
}

void History::record(EditCommand command) {
    if (command.changes.empty()) return;
    push(std::move(command));
}

bool History::undo(Scene& scene) {
    if (undo_.empty()) return false;
    auto entry = std::move(undo_.back());
    undo_.pop_back();
    apply(scene, entry.command, false);
    redo_.push_back(std::move(entry));
    return true;
}

bool History::redo(Scene& scene) {
    if (redo_.empty()) return false;
    auto entry = std::move(redo_.back());
    redo_.pop_back();
    apply(scene, entry.command, true);
    undo_.push_back(std::move(entry));
    return true;
}

void History::clear() noexcept {
    undo_.clear();
    redo_.clear();
}

const std::string* History::undoLabel() const noexcept {
    return undo_.empty() ? nullptr : &undo_.back().command.label;
}

const std::string* History::redoLabel() const noexcept {
    return redo_.empty() ? nullptr : &redo_.back().command.label;
}

std::uint64_t History::state() const noexcept {
    return undo_.empty() ? 0 : undo_.back().serial;
}

EditCommand makeCreateCommand(const Scene& scene, Entity entity) {
    EditCommand command;
    command.label = "Crear " + entity.name;
    command.changes.push_back({std::nullopt, std::move(entity), scene.entities().size()});
    return command;
}

std::optional<EditCommand> makeDuplicateCommand(Scene& scene, EntityId id, Vec3 offset) {
    const auto index = scene.indexOf(id);
    if (!index) return std::nullopt;
    const Entity source = scene.entities()[*index];
    auto copy = scene.makeEntity(source.name + " copia", source.primitive, source.transform);
    copy.transform.position.x += offset.x;
    copy.transform.position.y += offset.y;
    copy.transform.position.z += offset.z;
    EditCommand command;
    command.label = "Duplicar " + source.name;
    command.changes.push_back({std::nullopt, std::move(copy), *index + 1});
    return command;
}

std::optional<EditCommand> makeDeleteCommand(const Scene& scene, EntityId id) {
    const auto index = scene.indexOf(id);
    if (!index) return std::nullopt;
    const auto& entity = scene.entities()[*index];
    EditCommand command;
    command.label = "Eliminar " + entity.name;
    command.changes.push_back({entity, std::nullopt, *index});
    return command;
}

std::optional<EditCommand> makeModifyCommand(const Scene& scene, const Entity& before, std::string label) {
    const auto index = scene.indexOf(before.id);
    if (!index) return std::nullopt;
    const auto& current = scene.entities()[*index];
    if (current == before) return std::nullopt;
    EditCommand command;
    command.label = std::move(label);
    command.changes.push_back({before, current, *index});
    return command;
}

}
