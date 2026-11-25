// Prefabs en el editor: crear (arrastrar a Proyecto), instanciar (arrastrar a la Escena o la Jerarquía),
// editar (doble clic: se abre como una escena), aplicar, revertir y mantener las instancias al día.
#include "EditorLog.hpp"
#include "EditorView.hpp"
#include "ge3d/SceneIO.hpp"

#include <algorithm>
#include <cctype>

namespace {

bool isPrefabPath(const std::filesystem::path& path) {
    return ge3d::pathToUtf8(path.filename()).ends_with(".prefab.json");
}

std::string fileStem(const std::string& asset) {
    auto name = asset.substr(asset.find_last_of('/') == std::string::npos ? 0 : asset.find_last_of('/') + 1);
    if (name.ends_with(".prefab.json")) name.resize(name.size() - 12);
    return name;
}

}

bool EditorView::editingPrefab() const {
    return isPrefabPath(scenePath_);
}

std::vector<std::string> EditorView::prefabAssetsInScene() const {
    std::vector<std::string> assets;
    for (const auto& entity : scene_.entities()) {
        if (entity.prefab && std::find(assets.begin(), assets.end(), entity.prefab->asset) == assets.end()) assets.push_back(entity.prefab->asset);
    }
    return assets;
}

const ge3d::Scene* EditorView::prefabContent(const std::string& asset) {
    const auto path = contentRoot_ / ge3d::pathFromUtf8(asset);
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    auto& entry = prefabs_[asset];
    if (error) {
        if (entry.valid || !entry.reported) log("No se encuentra el prefab " + asset, true);
        entry.valid = false;
        entry.reported = true;
        return nullptr;
    }
    if (entry.valid && entry.time == time) return &entry.content;
    try {
        entry.content = ge3d::loadSceneFile(path);
        entry.time = time;
        entry.valid = ge3d::prefabRoot(entry.content) != nullptr;
        entry.reported = false;
    } catch (const std::exception& failure) {
        if (!entry.reported) log(std::string("No se pudo leer el prefab ") + asset + ": " + failure.what(), true);
        entry.valid = false;
        entry.reported = true;
    }
    return entry.valid ? &entry.content : nullptr;
}

void EditorView::refreshOverrides() {
    for (const auto& asset : prefabAssetsInScene()) {
        if (const auto* content = prefabContent(asset)) ge3d::refreshPrefabOverrides(scene_, asset, *content);
    }
}

void EditorView::syncPrefabs(bool fromLoad) {
    if (isPlaying()) return;
    for (const auto& asset : prefabAssetsInScene()) {
        const auto path = contentRoot_ / ge3d::pathFromUtf8(asset);
        std::error_code error;
        const auto time = std::filesystem::last_write_time(path, error);
        if (error) continue;
        const auto cached = prefabs_.find(asset);
        const bool changed = cached == prefabs_.end() || !cached->second.valid || cached->second.time != time;
        if (!fromLoad && !changed) continue;
        // Antes de leer la versión nueva, los overrides se calculan contra la anterior (lo que cada instancia
        // había cambiado). Al abrir una escena valen los guardados en el archivo.
        if (!fromLoad && cached != prefabs_.end() && cached->second.valid) {
            ge3d::refreshPrefabOverrides(scene_, asset, cached->second.content);
        }
        const auto* content = prefabContent(asset);
        if (!content) continue;
        if (auto command = ge3d::makePrefabSyncCommand(scene_, asset, *content)) {
            if (fromLoad) {
                // Al abrir, la escena simplemente queda al día (no es una edición que deshacer).
                ge3d::History temporary;
                temporary.execute(scene_, std::move(*command));
            } else {
                execute(std::move(*command));
                log("Prefab " + fileStem(asset) + " actualizado en sus instancias");
            }
        }
        ge3d::refreshPrefabOverrides(scene_, asset, *content);
    }
}

void EditorView::createPrefab(ge3d::EntityId id, const std::filesystem::path& folder) {
    const auto* entity = scene_.find(id);
    if (!entity || editInProgress() || isPlaying()) {
        if (isPlaying()) log("Los prefabs se crean fuera de Play", true);
        return;
    }
    std::string name;
    for (const char c : entity->name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') name += c;
        else if (c == ' ') name += '_';
    }
    if (name.empty()) name = "prefab";
    auto path = folder / ge3d::pathFromUtf8(name + ".prefab.json");
    std::error_code error;
    for (int n = 2; std::filesystem::exists(path, error); ++n) path = folder / ge3d::pathFromUtf8(name + "_" + std::to_string(n) + ".prefab.json");
    std::map<ge3d::EntityId, ge3d::Uuid> mapping;
    const auto content = ge3d::makePrefabContent(scene_, id, &mapping);
    try {
        ge3d::saveSceneFile(content, path);
    } catch (const std::exception& failure) {
        log(std::string("No se pudo crear el prefab: ") + failure.what(), true);
        return;
    }
    // El objeto original pasa a ser una instancia del prefab nuevo.
    const auto asset = assetPath(path);
    std::vector<ge3d::Entity> before;
    for (const auto& [entityId, uuid] : mapping) {
        auto* linked = scene_.find(entityId);
        before.push_back(*linked);
        linked->prefab = ge3d::PrefabLink{asset, uuid, {}};
    }
    if (auto command = ge3d::makeModifyCommand(scene_, before, "Crear prefab")) activeHistory().record(std::move(*command));
    prefabs_.erase(asset);
    refreshProject();
    log("Prefab creado: " + asset + " (arrastralo a la Escena para crear copias)");
}

void EditorView::instantiatePrefab(const std::string& asset, std::optional<glm::vec3> position, ge3d::EntityId parent) {
    if (editInProgress()) return;
    if (editingPrefab() && assetPath(scenePath_) == asset) {
        log("Un prefab no puede contenerse a si mismo", true);
        return;
    }
    const auto* content = prefabContent(asset);
    if (!content) return;
    const auto* root = ge3d::prefabRoot(*content);
    ge3d::Transform transform = root->transform;   // rotación y escala de la raíz del prefab
    if (position) {
        ge3d::Transform world = transform;
        world.position = {position->x, position->y, position->z};
        // Las piezas tienen el centro en medio: se levanta media altura para apoyarla sobre lo apuntado.
        if (root->shape && !parent) world.position.y += transform.scale.y * 0.5f;
        transform = ge3d::localTransform(scene_, parent, world);
    } else if (!parent) {
        transform.position = {std::round(pivot_.x), 0.0f, std::round(pivot_.z)};
    }
    ge3d::EntityId rootId{};
    execute(ge3d::makeInstantiateCommand(scene_, *content, asset, transform, parent, &rootId));
    select(rootId);
}

void EditorView::applyPrefab(ge3d::EntityId instanceRoot) {
    const auto* root = scene_.find(instanceRoot);
    if (!root || !root->prefab || editInProgress() || isPlaying()) return;
    const auto asset = root->prefab->asset;
    const auto* current = prefabContent(asset);
    if (!current) return;
    const ge3d::Scene previous = *current;
    std::map<ge3d::EntityId, ge3d::Uuid> newLinks;
    auto content = ge3d::prefabFromInstance(scene_, instanceRoot, &newLinks);
    // La colocación de la instancia no se aplica: la raíz del prefab conserva su rotación y escala.
    if (const auto* oldRoot = ge3d::prefabRoot(previous)) {
        if (const auto* newRoot = ge3d::prefabRoot(content)) content.find(newRoot->id)->transform = oldRoot->transform;
    }
    try {
        ge3d::saveSceneFile(content, contentRoot_ / ge3d::pathFromUtf8(asset));
    } catch (const std::exception& failure) {
        log(std::string("No se pudo guardar el prefab: ") + failure.what(), true);
        return;
    }
    // Lo añadido en la instancia queda enlazado al prefab.
    std::vector<ge3d::Entity> before;
    for (const auto& [id, uuid] : newLinks) {
        auto* linked = scene_.find(id);
        before.push_back(*linked);
        linked->prefab = ge3d::PrefabLink{asset, uuid, {}};
    }
    if (auto command = ge3d::makeModifyCommand(scene_, before, "Enlazar al prefab")) activeHistory().record(std::move(*command));
    // Las demás instancias: overrides respecto a la versión anterior, y después la nueva.
    ge3d::refreshPrefabOverrides(scene_, asset, previous);
    for (auto& entity : scene_.entities()) {
        // La instancia de origen ya es igual al prefab nuevo: no conserva overrides.
        if (entity.prefab && entity.prefab->asset == asset && ge3d::prefabInstanceRoot(scene_, entity.id) == instanceRoot) {
            scene_.find(entity.id)->prefab->overrides.clear();
        }
    }
    prefabs_.erase(asset);
    if (const auto* updated = prefabContent(asset)) {
        if (auto command = ge3d::makePrefabSyncCommand(scene_, asset, *updated)) execute(std::move(*command));
        ge3d::refreshPrefabOverrides(scene_, asset, *updated);
    }
    log("Cambios aplicados al prefab " + fileStem(asset));
}

void EditorView::revertPrefab(ge3d::EntityId instanceRoot) {
    const auto* root = scene_.find(instanceRoot);
    if (!root || !root->prefab || editInProgress()) return;
    const auto asset = root->prefab->asset;
    const auto* content = prefabContent(asset);
    if (!content) return;
    ge3d::EditCommand command;
    command.label = "Revertir al prefab";
    std::vector<ge3d::EntityId> ids{instanceRoot};
    const auto rest = ge3d::descendantsOf(scene_, instanceRoot);
    ids.insert(ids.end(), rest.begin(), rest.end());
    for (const auto id : ids) {
        if (auto revert = ge3d::makeRevertCommand(scene_, id, *content, "")) {
            for (auto& change : revert->changes) command.changes.push_back(std::move(change));
        }
    }
    if (!command.empty()) execute(std::move(command));
}

void EditorView::unpackPrefab(ge3d::EntityId instanceRoot) {
    std::vector<ge3d::Entity> before;
    std::vector<ge3d::EntityId> ids{instanceRoot};
    const auto rest = ge3d::descendantsOf(scene_, instanceRoot);
    ids.insert(ids.end(), rest.begin(), rest.end());
    const auto* root = scene_.find(instanceRoot);
    if (!root || !root->prefab) return;
    const auto asset = root->prefab->asset;
    for (const auto id : ids) {
        auto* entity = scene_.find(id);
        if (!entity->prefab || entity->prefab->asset != asset) continue;
        before.push_back(*entity);
        entity->prefab.reset();
    }
    if (auto command = ge3d::makeModifyCommand(scene_, before, "Desempaquetar prefab")) activeHistory().record(std::move(*command));
}

void EditorView::openPrefab(const std::filesystem::path& path) {
    // Se edita como una escena; "Volver" regresa a la escena de antes.
    if (!editingPrefab()) prefabReturn_ = scenePath_;
    requestSceneChange(SceneChange::open, path);
}

void EditorView::drawPrefabBanner(ge3d::Entity& entity) {
    if (!entity.prefab) return;
    const auto instanceRoot = ge3d::prefabInstanceRoot(scene_, entity.id);
    const auto* content = prefabContent(entity.prefab->asset);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.16f, 0.24f, 0.34f, 1.0f});
    if (ImGui::BeginChild("##prefab", {0, ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().WindowPadding.y * 2},
                          ImGuiChildFlags_AlwaysUseWindowPadding)) {
        ImGui::TextColored(ImVec4{0.55f, 0.78f, 1.0f, 1.0f}, "Prefab: %s", fileStem(entity.prefab->asset).c_str());
        if (instanceRoot != entity.id) {
            if (const auto* rootEntity = scene_.find(instanceRoot)) {
                ImGui::SameLine();
                ImGui::TextDisabled("(parte de %s)", rootEntity->name.c_str());
            }
        }
        if (!entity.prefab->overrides.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %zu cambio%s propio%s", entity.prefab->overrides.size(), entity.prefab->overrides.size() == 1 ? "" : "s",
                                entity.prefab->overrides.size() == 1 ? "" : "s");
        }
        if (!content) {
            ImGui::TextColored(ImVec4{1.0f, 0.45f, 0.4f, 1.0f}, "No se encuentra el archivo");
            ImGui::SameLine();
            if (ImGui::SmallButton("Desempaquetar")) unpackPrefab(instanceRoot);
        } else {
            if (ImGui::SmallButton("Abrir")) openPrefab(contentRoot_ / ge3d::pathFromUtf8(entity.prefab->asset));
            ImGui::SetItemTooltip("Editar el prefab (al guardar se actualizan todas sus copias)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Seleccionar")) {
                showProject_ = true;
                projectMaterials_ = false;
                projectFolder_ = (contentRoot_ / ge3d::pathFromUtf8(entity.prefab->asset)).parent_path();
                projectSelected_ = entity.prefab->asset;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(isPlaying());
            if (ImGui::SmallButton("Revertir")) revertPrefab(instanceRoot);
            ImGui::SetItemTooltip("Deshacer los cambios propios de esta copia");
            ImGui::SameLine();
            if (ImGui::SmallButton("Aplicar")) applyPrefab(instanceRoot);
            ImGui::SetItemTooltip("Guardar los cambios de esta copia en el prefab (y en todas las demas copias)");
            ImGui::EndDisabled();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
