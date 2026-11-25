// Preferencias del editor que se recuerdan entre sesiones, como la carpeta UserSettings de Unity:
// distribución de paneles (imgui.ini), última escena, escenas recientes, cámara de cada escena,
// estilo pixel art y opciones de la vista. Es por usuario y no se sube al repositorio.
#include "EditorLog.hpp"
#include "EditorView.hpp"
#include "ge3d/SceneIO.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace {
using Json = nlohmann::json;
constexpr std::size_t maxRecent = 8;

template <class T>
void read(const Json& object, const char* key, T& value) {
    const auto it = object.find(key);
    if (it == object.end()) return;
    try {
        value = it->get<T>();
    } catch (const std::exception&) {
        // Un valor de otro tipo (archivo editado a mano o de otra versión): se ignora.
    }
}
}

std::filesystem::path EditorView::settingsFolder() const {
    return contentRoot_.parent_path() / "UserSettings";
}

void EditorView::loadSettings() {
    std::error_code error;
    std::filesystem::create_directories(settingsFolder(), error);
    // Distribución de paneles: si ya hay una guardada, se usa en lugar de la de fábrica.
    iniPath_ = ge3d::pathToUtf8(settingsFolder() / "imgui.ini");
    ImGui::GetIO().IniFilename = iniPath_.c_str();
    if (std::filesystem::exists(settingsFolder() / "imgui.ini", error)) layoutCreated_ = true;

    const auto path = settingsFolder() / "editor.json";
    if (!std::filesystem::exists(path, error)) return;
    Json root;
    try {
        root = Json::parse(ge3d::readTextFile(path));
    } catch (const std::exception&) {
        editorLog().add(LogLevel::warning, "Preferencias del editor ilegibles: se usan las de fabrica");
        return;
    }
    if (!root.is_object()) return;
    std::string last;
    read(root, "lastScene", last);
    if (!last.empty()) lastScene_ = ge3d::pathFromUtf8(last);
    std::vector<std::string> recent;
    read(root, "recent", recent);
    recentScenes_.clear();
    for (const auto& item : recent) recentScenes_.push_back(ge3d::pathFromUtf8(item));
    read(root, "showGrid", showGrid_);
    read(root, "snap", snap_);
    read(root, "localAxes", localAxes_);
    read(root, "flySpeed", flySpeed_);
    read(root, "clearOnPlay", clearOnPlay_);
    read(root, "pauseOnError", pauseOnError_);
    if (const auto it = root.find("panels"); it != root.end() && it->is_object()) {
        read(*it, "hierarchy", showHierarchy_);
        read(*it, "inspector", showInspector_);
        read(*it, "project", showProject_);
        read(*it, "console", showConsole_);
        read(*it, "code", showCode_);
    }
    if (const auto it = root.find("style"); it != root.end() && it->is_object()) {
        read(*it, "pixelSize", style_.pixelSize);
        read(*it, "outlines", style_.outlines);
        read(*it, "outlineDarken", style_.outlineDarken);
        read(*it, "edgeLighten", style_.edgeLighten);
        read(*it, "lightBands", style_.lightBands);
        read(*it, "colorLevels", style_.colorLevels);
        read(*it, "snapCamera", style_.snapCamera);
        style_.pixelSize = std::clamp(style_.pixelSize, 1, 8);
    }
    if (const auto it = root.find("cameras"); it != root.end() && it->is_object()) {
        for (const auto& [scene, values] : it->items()) {
            if (values.is_array() && values.size() == 6) {
                std::array<float, 6> camera{};
                for (std::size_t i = 0; i < 6; ++i) camera[i] = values[i].is_number() ? values[i].get<float>() : 0.0f;
                cameras_[scene] = camera;
            }
        }
    }
}

void EditorView::saveSettings() {
    rememberCamera();
    Json root = Json::object();
    root["lastScene"] = ge3d::pathToUtf8(scenePath_);
    Json recent = Json::array();
    for (const auto& path : recentScenes_) recent.push_back(ge3d::pathToUtf8(path));
    root["recent"] = std::move(recent);
    root["showGrid"] = showGrid_;
    root["snap"] = snap_;
    root["localAxes"] = localAxes_;
    root["flySpeed"] = flySpeed_;
    root["clearOnPlay"] = clearOnPlay_;
    root["pauseOnError"] = pauseOnError_;
    root["panels"] = {{"hierarchy", showHierarchy_}, {"inspector", showInspector_}, {"project", showProject_},
                      {"console", showConsole_}, {"code", showCode_}};
    root["style"] = {{"pixelSize", style_.pixelSize}, {"outlines", style_.outlines}, {"outlineDarken", style_.outlineDarken},
                     {"edgeLighten", style_.edgeLighten}, {"lightBands", style_.lightBands},
                     {"colorLevels", style_.colorLevels}, {"snapCamera", style_.snapCamera}};
    Json cameras = Json::object();
    for (const auto& [scene, camera] : cameras_) cameras[scene] = camera;
    root["cameras"] = std::move(cameras);
    try {
        ge3d::writeTextFileAtomic(settingsFolder() / "editor.json", root.dump(2) + "\n");
    } catch (const std::exception& error) {
        editorLog().add(LogLevel::warning, std::string("No se pudieron guardar las preferencias: ") + error.what());
    }
}

void EditorView::addRecentScene(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    std::erase_if(recentScenes_, [&](const std::filesystem::path& p) { return std::filesystem::absolute(p).lexically_normal() == absolute; });
    recentScenes_.insert(recentScenes_.begin(), absolute);
    if (recentScenes_.size() > maxRecent) recentScenes_.resize(maxRecent);
}

void EditorView::rememberCamera() {
    if (scenePath_.empty() || firstPerson_) return;
    cameras_[ge3d::pathToUtf8(std::filesystem::absolute(scenePath_).lexically_normal())] = {pivot_.x, pivot_.y, pivot_.z, yaw_, pitch_, distance_};
}

void EditorView::restoreCamera() {
    const auto it = cameras_.find(ge3d::pathToUtf8(std::filesystem::absolute(scenePath_).lexically_normal()));
    if (it == cameras_.end()) return;
    const auto& c = it->second;
    pivot_ = {c[0], c[1], c[2]};
    yaw_ = c[3];
    pitch_ = std::clamp(c[4], -1.55f, 1.55f);
    distance_ = std::clamp(c[5], 0.5f, 600.0f);
}
