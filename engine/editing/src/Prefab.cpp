#include "ge3d/Prefab.hpp"

#include <algorithm>

namespace ge3d {

namespace {

bool sameComponent(const Entity& a, const Entity& b, const std::string& name) {
    if (name == "name") return a.name == b.name;
    if (name == "active") return a.active == b.active;
    if (name == "transform") return a.transform == b.transform;
    if (name == "shape") return a.shape == b.shape;
    if (name == "model") return a.model == b.model;
    // Terrenos: los datos son de cada instancia; se compara su configuración.
    if (name == "terrain") return a.terrain.has_value() == b.terrain.has_value() && (!a.terrain || a.terrain->settings == b.terrain->settings);
    if (name == "heightmap") {
        return a.heightmap.has_value() == b.heightmap.has_value() && (!a.heightmap || a.heightmap->settings == b.heightmap->settings);
    }
    if (name == "script") return a.script == b.script;
    if (name == "body") return a.body == b.body;
    if (name == "audio") return a.audio == b.audio;
    if (name == "player") return a.player == b.player;
    return true;
}

void copyComponent(Entity& to, const Entity& from, const std::string& name) {
    if (name == "name") to.name = from.name;
    else if (name == "active") to.active = from.active;
    else if (name == "transform") to.transform = from.transform;
    else if (name == "shape") to.shape = from.shape;
    else if (name == "model") to.model = from.model;
    else if (name == "terrain") {
        if (!from.terrain) to.terrain.reset();
        else if (!to.terrain) {
            to.terrain = from.terrain;
            if (to.terrain->voxels) to.terrain->voxels = std::make_shared<VoxelGrid>(*to.terrain->voxels);
        } else {
            to.terrain->settings = from.terrain->settings;
        }
    } else if (name == "heightmap") {
        if (!from.heightmap) to.heightmap.reset();
        else if (!to.heightmap) {
            to.heightmap = from.heightmap;
            if (to.heightmap->data) to.heightmap->data = std::make_shared<Heightmap>(*to.heightmap->data);
        } else {
            to.heightmap->settings = from.heightmap->settings;
        }
    } else if (name == "script") to.script = from.script;
    else if (name == "body") to.body = from.body;
    else if (name == "audio") to.audio = from.audio;
    else if (name == "player") to.player = from.player;
}

bool overridden(const Entity& entity, const std::string& name) {
    return entity.prefab && std::find(entity.prefab->overrides.begin(), entity.prefab->overrides.end(), name) != entity.prefab->overrides.end();
}

const Entity* findByUuid(const Scene& scene, const Uuid& uuid) {
    const auto& entities = scene.entities();
    const auto it = std::find_if(entities.begin(), entities.end(), [&](const Entity& e) { return e.uuid == uuid; });
    return it == entities.end() ? nullptr : &*it;
}

// Copia de una entidad con datos de terreno propios.
Entity ownCopy(const Entity& source) {
    Entity copy = source;
    if (copy.terrain && copy.terrain->voxels) copy.terrain->voxels = std::make_shared<VoxelGrid>(*copy.terrain->voxels);
    if (copy.heightmap && copy.heightmap->data) copy.heightmap->data = std::make_shared<Heightmap>(*copy.heightmap->data);
    return copy;
}

// Raíz y descendencia de una entidad, padres antes que hijos.
std::vector<EntityId> subtree(const Scene& scene, EntityId root) {
    std::vector<EntityId> ids{root};
    const auto rest = descendantsOf(scene, root);
    ids.insert(ids.end(), rest.begin(), rest.end());
    return ids;
}

// Escena de prefab a partir de entidades (padres antes que hijos) con los UUID indicados.
Scene buildContent(const Scene& scene, const std::vector<EntityId>& ids, const std::map<EntityId, Uuid>& uuids) {
    Scene content;
    std::map<EntityId, EntityId> remap;
    for (const auto id : ids) {
        auto copy = ownCopy(*scene.find(id));
        const bool isRoot = id == ids.front();
        copy.uuid = uuids.at(id);
        copy.prefab.reset();
        copy.parent = isRoot ? 0 : (remap.contains(copy.parent) ? remap.at(copy.parent) : 0);
        if (isRoot) copy.transform.position = {};
        remap[id] = content.add(std::move(copy));
    }
    return content;
}

}

const Entity* prefabRoot(const Scene& prefab) {
    const auto& entities = prefab.entities();
    const auto it = std::find_if(entities.begin(), entities.end(), [](const Entity& e) { return e.parent == 0; });
    return it == entities.end() ? nullptr : &*it;
}

const Entity* prefabSource(const Scene& prefab, const Entity& instance) {
    return instance.prefab ? findByUuid(prefab, instance.prefab->source) : nullptr;
}

Scene makePrefabContent(const Scene& scene, EntityId root, std::map<EntityId, Uuid>* mapping) {
    if (!scene.find(root)) return {};
    const auto ids = subtree(scene, root);
    std::map<EntityId, Uuid> uuids;
    for (const auto id : ids) uuids[id] = Uuid::generate();
    if (mapping) *mapping = uuids;
    return buildContent(scene, ids, uuids);
}

Scene prefabFromInstance(const Scene& scene, EntityId instanceRoot, std::map<EntityId, Uuid>* newLinks) {
    const auto* root = scene.find(instanceRoot);
    if (!root || !root->prefab) return {};
    const auto& asset = root->prefab->asset;
    const auto ids = subtree(scene, instanceRoot);
    std::map<EntityId, Uuid> uuids;
    for (const auto id : ids) {
        const auto* entity = scene.find(id);
        if (entity->prefab && entity->prefab->asset == asset) {
            uuids[id] = entity->prefab->source;
        } else {
            uuids[id] = Uuid::generate();
            if (newLinks) (*newLinks)[id] = uuids[id];
        }
    }
    return buildContent(scene, ids, uuids);
}

EditCommand makeInstantiateCommand(Scene& scene, const Scene& prefab, const std::string& asset, const Transform& rootTransform,
                                   EntityId parent, EntityId* rootId) {
    EditCommand command;
    const auto* root = prefabRoot(prefab);
    if (!root) return command;
    command.label = "Instanciar " + root->name;
    std::map<EntityId, EntityId> remap;   // id en el prefab -> id en la escena
    std::size_t at = scene.entities().size();
    const auto ids = subtree(prefab, root->id);
    for (const auto prefabId : ids) {
        const auto& source = *prefab.find(prefabId);
        auto entity = ownCopy(source);
        const auto fresh = scene.makeEntity(source.name);
        entity.id = fresh.id;
        entity.uuid = fresh.uuid;
        entity.prefab = PrefabLink{asset, source.uuid, {}};
        if (prefabId == root->id) {
            entity.parent = parent;
            entity.transform = rootTransform;
            if (rootId) *rootId = entity.id;
        } else {
            entity.parent = remap.contains(source.parent) ? remap.at(source.parent) : 0;
        }
        remap[prefabId] = entity.id;
        command.changes.push_back({std::nullopt, std::move(entity), at++});
    }
    return command;
}

std::vector<std::string> prefabDifferences(const Entity& instance, const Entity& source, bool isRoot) {
    std::vector<std::string> differences;
    for (const char* name : prefabComponents) {
        if (isRoot && std::string_view(name) == "transform") continue;
        if (!sameComponent(instance, source, name)) differences.emplace_back(name);
    }
    return differences;
}

void refreshPrefabOverrides(Scene& scene, const std::string& asset, const Scene& prefab) {
    const auto* root = prefabRoot(prefab);
    for (const auto& entity : scene.entities()) {
        if (!entity.prefab || entity.prefab->asset != asset) continue;
        const auto* source = findByUuid(prefab, entity.prefab->source);
        auto* target = scene.find(entity.id);
        target->prefab->overrides = source ? prefabDifferences(entity, *source, root && source->uuid == root->uuid) : std::vector<std::string>{};
    }
}

EntityId prefabInstanceRoot(const Scene& scene, EntityId id) {
    const Entity* entity = scene.find(id);
    if (!entity || !entity->prefab) return 0;
    const auto& asset = entity->prefab->asset;
    // Sube mientras el padre sea del mismo prefab.
    for (int depth = 0; depth < 64; ++depth) {
        const Entity* parent = entity->parent ? scene.find(entity->parent) : nullptr;
        if (!parent || !parent->prefab || parent->prefab->asset != asset) return entity->id;
        entity = parent;
    }
    return entity->id;
}

std::optional<EditCommand> makePrefabSyncCommand(Scene& scene, const std::string& asset, const Scene& prefab) {
    const auto* prefabRootEntity = prefabRoot(prefab);
    if (!prefabRootEntity) return std::nullopt;
    EditCommand command;
    command.label = "Actualizar instancias de " + prefabRootEntity->name;
    std::vector<EntityChange> modifications, creations, deletions;
    std::size_t at = scene.entities().size();
    std::vector<EntityId> roots;
    for (const auto& entity : scene.entities()) {
        if (entity.prefab && entity.prefab->asset == asset && entity.prefab->source == prefabRootEntity->uuid) roots.push_back(entity.id);
    }
    for (const auto rootId : roots) {
        // Entidades de esta instancia por su UUID en el prefab.
        std::map<Uuid, EntityId> instance;
        for (const auto id : subtree(scene, rootId)) {
            const auto* entity = scene.find(id);
            if (entity->prefab && entity->prefab->asset == asset) instance[entity->prefab->source] = id;
        }
        std::map<EntityId, EntityId> prefabToScene;
        for (const auto prefabId : subtree(prefab, prefabRootEntity->id)) {
            const auto& source = *prefab.find(prefabId);
            const bool isRoot = prefabId == prefabRootEntity->id;
            if (const auto found = instance.find(source.uuid); found != instance.end()) {
                const auto& current = *scene.find(found->second);
                Entity after = current;
                for (const char* name : prefabComponents) {
                    if ((isRoot && std::string_view(name) == "transform") || overridden(current, name)) continue;
                    if (!sameComponent(after, source, name)) copyComponent(after, source, name);
                }
                if (!(after == current)) modifications.push_back({current, after, *scene.indexOf(current.id)});
                prefabToScene[prefabId] = current.id;
                continue;
            }
            // Nueva en el prefab: se añade a la instancia bajo su padre correspondiente.
            auto entity = ownCopy(source);
            const auto fresh = scene.makeEntity(source.name);
            entity.id = fresh.id;
            entity.uuid = fresh.uuid;
            entity.prefab = PrefabLink{asset, source.uuid, {}};
            entity.parent = prefabToScene.contains(source.parent) ? prefabToScene.at(source.parent) : rootId;
            prefabToScene[prefabId] = entity.id;
            creations.push_back({std::nullopt, std::move(entity), at++});
        }
        // Lo que el prefab ya no tiene se quita de la instancia (con su descendencia).
        for (const auto& [uuid, id] : instance) {
            if (findByUuid(prefab, uuid)) continue;
            for (const auto removed : subtree(scene, id)) {
                if (std::any_of(deletions.begin(), deletions.end(), [&](const EntityChange& c) { return c.before->id == removed; })) continue;
                deletions.push_back({*scene.find(removed), std::nullopt, *scene.indexOf(removed)});
            }
        }
    }
    // Primero modificaciones, después altas (al final de la lista) y por último bajas de la última a la primera.
    std::sort(deletions.begin(), deletions.end(), [](const EntityChange& a, const EntityChange& b) { return a.index > b.index; });
    for (auto* group : {&modifications, &creations, &deletions}) {
        for (auto& change : *group) command.changes.push_back(std::move(change));
    }
    if (command.changes.empty()) return std::nullopt;
    return command;
}

std::optional<EditCommand> makeRevertCommand(const Scene& scene, EntityId id, const Scene& prefab, const std::string& component) {
    const auto* entity = scene.find(id);
    if (!entity || !entity->prefab) return std::nullopt;
    const auto* source = findByUuid(prefab, entity->prefab->source);
    if (!source) return std::nullopt;
    const auto* root = prefabRoot(prefab);
    const bool isRoot = root && root->uuid == source->uuid;
    Entity after = *entity;
    for (const char* name : prefabComponents) {
        if (!component.empty() && component != name) continue;
        if (isRoot && std::string_view(name) == "transform") continue;
        copyComponent(after, *source, name);
    }
    std::erase_if(after.prefab->overrides, [&](const std::string& name) { return component.empty() || name == component; });
    if (after == *entity) return std::nullopt;
    EditCommand command;
    command.label = component.empty() ? "Revertir al prefab" : "Revertir " + component;
    command.changes.push_back({*entity, after, *scene.indexOf(id)});
    return command;
}

}
