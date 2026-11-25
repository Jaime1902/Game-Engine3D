#include "ge3d/History.hpp"

#include <algorithm>
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

void applyTerrain(Scene& scene, const TerrainChange& change, bool forward) {
    auto* entity = scene.find(change.entity);
    if (!entity || !entity->terrain || !entity->terrain->voxels) {
        throw std::logic_error("History: el terreno a modificar no existe");
    }
    entity->terrain->voxels->setChunkData(change.chunk, forward ? change.after : change.before);
}

void applyHeightmap(Scene& scene, const HeightmapChange& change, bool forward) {
    auto* entity = scene.find(change.entity);
    if (!entity || !entity->heightmap || !entity->heightmap->data) {
        throw std::logic_error("History: el terreno a modificar no existe");
    }
    entity->heightmap->data->setChunkData(change.chunk, forward ? change.after : change.before);
}

void apply(Scene& scene, const EditCommand& command, bool forward) {
    if (forward) {
        for (const auto& change : command.changes) applyChange(scene, change, true);
        for (const auto& change : command.terrainChanges) applyTerrain(scene, change, true);
        for (const auto& change : command.heightmapChanges) applyHeightmap(scene, change, true);
    } else {
        for (auto it = command.heightmapChanges.rbegin(); it != command.heightmapChanges.rend(); ++it) {
            applyHeightmap(scene, *it, false);
        }
        for (auto it = command.terrainChanges.rbegin(); it != command.terrainChanges.rend(); ++it) {
            applyTerrain(scene, *it, false);
        }
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
    if (command.empty()) return;
    apply(scene, command, true);
    push(std::move(command));
}

void History::record(EditCommand command) {
    if (command.empty()) return;
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
    // Misma entidad con identidad nueva; el terreno duplicado recibe sus propios vóxeles.
    auto copy = source;
    const auto fresh = scene.makeEntity(source.name + " copia");
    copy.id = fresh.id;
    copy.uuid = fresh.uuid;
    copy.name = fresh.name;
    if (copy.terrain && copy.terrain->voxels) copy.terrain->voxels = std::make_shared<VoxelGrid>(*copy.terrain->voxels);
    if (copy.heightmap && copy.heightmap->data) copy.heightmap->data = std::make_shared<Heightmap>(*copy.heightmap->data);
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

namespace {
// Copia con identidad nueva y datos de terreno propios.
Entity freshCopy(Scene& scene, const Entity& source, const std::string& name) {
    auto copy = source;
    const auto fresh = scene.makeEntity(name);
    copy.id = fresh.id;
    copy.uuid = fresh.uuid;
    copy.name = name;
    if (copy.terrain && copy.terrain->voxels) copy.terrain->voxels = std::make_shared<VoxelGrid>(*copy.terrain->voxels);
    if (copy.heightmap && copy.heightmap->data) copy.heightmap->data = std::make_shared<Heightmap>(*copy.heightmap->data);
    return copy;
}
}

std::optional<EditCommand> makeDeleteCommand(const Scene& scene, const std::vector<EntityId>& ids) {
    std::vector<std::pair<std::size_t, EntityId>> found;
    for (const auto id : ids) {
        if (const auto index = scene.indexOf(id)) found.emplace_back(*index, id);
    }
    if (found.empty()) return std::nullopt;
    // De la última a la primera: al deshacer se reinsertan en orden y cada una vuelve a su sitio.
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    EditCommand command;
    command.label = found.size() == 1 ? "Eliminar " + scene.entities()[found[0].first].name
                                      : "Eliminar " + std::to_string(found.size()) + " objetos";
    for (const auto& [index, id] : found) command.changes.push_back({scene.entities()[index], std::nullopt, index});
    return command;
}

std::optional<EditCommand> makeDuplicateCommand(Scene& scene, const std::vector<EntityId>& ids, Vec3 offset,
                                                std::vector<EntityId>* created) {
    std::vector<std::size_t> indices;
    for (const auto id : ids) {
        if (const auto index = scene.indexOf(id)) indices.push_back(*index);
    }
    if (indices.empty()) return std::nullopt;
    std::sort(indices.begin(), indices.end());
    EditCommand command;
    command.label = indices.size() == 1 ? "Duplicar " + scene.entities()[indices[0]].name
                                        : "Duplicar " + std::to_string(indices.size()) + " objetos";
    std::size_t at = indices.back() + 1;
    for (const auto index : indices) {
        const auto& source = scene.entities()[index];
        auto copy = freshCopy(scene, source, source.name + " copia");
        copy.transform.position.x += offset.x;
        copy.transform.position.y += offset.y;
        copy.transform.position.z += offset.z;
        if (created) created->push_back(copy.id);
        command.changes.push_back({std::nullopt, std::move(copy), at++});
    }
    return command;
}

EditCommand makePasteCommand(Scene& scene, const std::vector<Entity>& entities, std::vector<EntityId>* created) {
    EditCommand command;
    command.label = entities.size() == 1 ? "Pegar " + entities.front().name : "Pegar " + std::to_string(entities.size()) + " objetos";
    std::size_t at = scene.entities().size();
    for (const auto& source : entities) {
        auto copy = freshCopy(scene, source, source.name);
        if (created) created->push_back(copy.id);
        command.changes.push_back({std::nullopt, std::move(copy), at++});
    }
    return command;
}

std::optional<EditCommand> makeMoveCommand(const Scene& scene, EntityId id, std::size_t newIndex) {
    const auto index = scene.indexOf(id);
    if (!index || scene.entities().empty()) return std::nullopt;
    newIndex = std::min(newIndex, scene.entities().size() - 1);
    if (newIndex == *index) return std::nullopt;
    const auto& entity = scene.entities()[*index];
    EditCommand command;
    command.label = "Reordenar " + entity.name;
    command.changes.push_back({entity, std::nullopt, *index});
    command.changes.push_back({std::nullopt, entity, newIndex});
    return command;
}

std::optional<EditCommand> makeModifyCommand(const Scene& scene, const std::vector<Entity>& before, std::string label) {
    EditCommand command;
    command.label = std::move(label);
    for (const auto& entity : before) {
        const auto index = scene.indexOf(entity.id);
        if (!index || scene.entities()[*index] == entity) continue;
        command.changes.push_back({entity, scene.entities()[*index], *index});
    }
    if (command.changes.empty()) return std::nullopt;
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
