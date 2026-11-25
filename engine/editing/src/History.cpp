#include "ge3d/History.hpp"

#include <algorithm>
#include <map>
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
    // Con sus hijos, como en Unity.
    return makeDeleteCommand(scene, std::vector<EntityId>{id});
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

// Las entidades indicadas más su descendencia, sin repetir.
std::vector<EntityId> withDescendants(const Scene& scene, const std::vector<EntityId>& ids) {
    std::vector<EntityId> all;
    for (const auto id : ids) {
        if (!scene.find(id) || std::find(all.begin(), all.end(), id) != all.end()) continue;
        all.push_back(id);
        for (const auto child : descendantsOf(scene, id)) {
            if (std::find(all.begin(), all.end(), child) == all.end()) all.push_back(child);
        }
    }
    return all;
}

// Copias con identidad nueva; los padres dentro del grupo apuntan a las copias.
std::vector<Entity> freshGroup(Scene& scene, const std::vector<Entity>& sources, bool keepOutsideParent, const std::string& suffix) {
    std::map<EntityId, EntityId> remap;
    std::vector<Entity> copies;
    for (const auto& source : sources) {
        auto copy = freshCopy(scene, source, source.name + suffix);
        remap[source.id] = copy.id;
        copies.push_back(std::move(copy));
    }
    for (auto& copy : copies) {
        if (const auto it = remap.find(copy.parent); it != remap.end()) copy.parent = it->second;
        else if (!keepOutsideParent || !scene.find(copy.parent)) copy.parent = 0;
    }
    return copies;
}

std::optional<EditCommand> makeDeleteCommand(const Scene& scene, const std::vector<EntityId>& requested) {
    const auto ids = withDescendants(scene, requested);
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
    // Solo las raíces de la selección (si se eligen padre e hijo, el hijo ya va con el padre).
    std::vector<EntityId> roots;
    for (const auto id : ids) {
        if (!scene.find(id)) continue;
        const bool insideSelection = std::any_of(ids.begin(), ids.end(), [&](EntityId other) { return other != id && isAncestor(scene, other, id); });
        if (!insideSelection) roots.push_back(id);
    }
    if (roots.empty()) return std::nullopt;
    std::vector<Entity> sources;
    std::size_t last = 0;
    for (const auto& entity : scene.entities()) {
        const auto group = withDescendants(scene, roots);
        if (std::find(group.begin(), group.end(), entity.id) == group.end()) continue;
        sources.push_back(entity);
        last = std::max(last, *scene.indexOf(entity.id));
    }
    EditCommand command;
    command.label = roots.size() == 1 ? "Duplicar " + scene.find(roots[0])->name : "Duplicar " + std::to_string(roots.size()) + " objetos";
    auto copies = freshGroup(scene, sources, true, "");
    std::size_t at = last + 1;
    for (std::size_t i = 0; i < copies.size(); ++i) {
        auto& copy = copies[i];
        const bool root = std::find(roots.begin(), roots.end(), sources[i].id) != roots.end();
        if (root) {
            copy.name += " copia";
            copy.transform.position.x += offset.x;
            copy.transform.position.y += offset.y;
            copy.transform.position.z += offset.z;
            if (created) created->push_back(copy.id);
        }
        command.changes.push_back({std::nullopt, std::move(copy), at++});
    }
    return command;
}

EditCommand makePasteCommand(Scene& scene, const std::vector<Entity>& entities, std::vector<EntityId>* created) {
    EditCommand command;
    command.label = entities.size() == 1 ? "Pegar " + entities.front().name : "Pegar " + std::to_string(entities.size()) + " objetos";
    std::size_t at = scene.entities().size();
    std::map<EntityId, bool> inGroup;
    for (const auto& source : entities) inGroup[source.id] = true;
    auto copies = freshGroup(scene, entities, true, "");
    for (std::size_t i = 0; i < copies.size(); ++i) {
        // Las raíces de lo pegado (su padre no venía en el portapapeles) son lo que queda seleccionado.
        if (created && !inGroup.contains(entities[i].parent)) created->push_back(copies[i].id);
        command.changes.push_back({std::nullopt, std::move(copies[i]), at++});
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

std::optional<EditCommand> makeReparentCommand(const Scene& scene, const std::vector<EntityId>& ids, EntityId parent, EntityId anchor,
                                               bool after) {
    std::vector<std::size_t> indices;
    for (const auto id : ids) {
        if (!scene.find(id) || id == parent || (parent && isAncestor(scene, id, parent))) continue;
        // Si un antepasado también se mueve, este ya va con él.
        if (std::any_of(ids.begin(), ids.end(), [&](EntityId other) { return other != id && isAncestor(scene, other, id); })) continue;
        indices.push_back(*scene.indexOf(id));
    }
    if (indices.empty() || (parent && !scene.find(parent))) return std::nullopt;
    std::sort(indices.begin(), indices.end());
    EditCommand command;
    command.label = indices.size() == 1 ? "Mover " + scene.entities()[indices[0]].name + " en la jerarquia" : "Mover en la jerarquia";
    // Orden resultante: se quitan las entidades movidas y se insertan junto al ancla.
    std::vector<EntityId> remaining;
    for (const auto& entity : scene.entities()) {
        if (std::find(indices.begin(), indices.end(), *scene.indexOf(entity.id)) == indices.end()) remaining.push_back(entity.id);
    }
    std::size_t position = remaining.size();
    if (anchor) {
        const auto it = std::find(remaining.begin(), remaining.end(), anchor);
        if (it != remaining.end()) position = static_cast<std::size_t>(it - remaining.begin()) + (after ? 1 : 0);
    }
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) command.changes.push_back({scene.entities()[*it], std::nullopt, *it});
    for (std::size_t k = 0; k < indices.size(); ++k) {
        Entity moved = scene.entities()[indices[k]];
        const Transform world = worldTransform(scene, moved);
        moved.parent = parent;
        moved.transform = localTransform(scene, parent, world);
        command.changes.push_back({std::nullopt, std::move(moved), position + k});
    }
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
