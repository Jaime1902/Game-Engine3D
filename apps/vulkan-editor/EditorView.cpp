#include "EditorView.hpp"

#include "EditorLog.hpp"
#include "ge3d/Primitives.hpp"
#include "ge3d/SceneIO.hpp"
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <cmath>
#include <cstdio>
#include <limits>
#include <system_error>
#include <utility>

#ifndef GE3D_DEFAULT_SCENE
#define GE3D_DEFAULT_SCENE "main.scene.json"
#endif
#ifndef GE3D_CONTENT_DIR
#define GE3D_CONTENT_DIR "."
#endif

namespace {

glm::vec3 vector(ge3d::Vec3 v) { return {v.x, v.y, v.z}; }

// --- Colores del tema --------------------------------------------------------------------------
// Grises oscuros con acento azul, la paleta habitual de los editores de escenas.
ImVec4 gray(float value, float alpha = 1.0f) { return {value, value, value, alpha}; }
const ImVec4 accent{0.17f, 0.36f, 0.53f, 1.0f};
const ImVec4 accentHover{0.22f, 0.44f, 0.63f, 1.0f};
const ImVec4 warningColor{0.95f, 0.78f, 0.30f, 1.0f};
const ImVec4 errorColor{1.0f, 0.42f, 0.38f, 1.0f};

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowPadding = {8, 6};
    style.FramePadding = {6, 3};
    style.ItemSpacing = {6, 4};
    style.ItemInnerSpacing = {4, 4};
    style.IndentSpacing = 14;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 8;
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 3;
    style.PopupRounding = 3;
    style.ScrollbarRounding = 6;
    style.GrabRounding = 3;
    style.TabRounding = 0;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;
    style.TabBorderSize = 0;
    style.TabBarBorderSize = 1;
    style.TabBarOverlineSize = 0;
    style.DockingSeparatorSize = 2;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = gray(0.82f);
    c[ImGuiCol_TextDisabled] = gray(0.52f);
    c[ImGuiCol_WindowBg] = gray(0.22f);
    c[ImGuiCol_ChildBg] = gray(0.22f, 0.0f);
    c[ImGuiCol_PopupBg] = gray(0.20f);
    c[ImGuiCol_Border] = gray(0.10f);
    c[ImGuiCol_BorderShadow] = gray(0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = gray(0.165f);
    c[ImGuiCol_FrameBgHovered] = gray(0.20f);
    c[ImGuiCol_FrameBgActive] = gray(0.14f);
    c[ImGuiCol_TitleBg] = gray(0.11f);
    c[ImGuiCol_TitleBgActive] = gray(0.11f);
    c[ImGuiCol_TitleBgCollapsed] = gray(0.11f);
    c[ImGuiCol_MenuBarBg] = gray(0.16f);
    c[ImGuiCol_ScrollbarBg] = gray(0.19f);
    c[ImGuiCol_ScrollbarGrab] = gray(0.33f);
    c[ImGuiCol_ScrollbarGrabHovered] = gray(0.40f);
    c[ImGuiCol_ScrollbarGrabActive] = gray(0.45f);
    c[ImGuiCol_CheckMark] = gray(0.85f);
    c[ImGuiCol_SliderGrab] = gray(0.45f);
    c[ImGuiCol_SliderGrabActive] = gray(0.55f);
    c[ImGuiCol_Button] = gray(0.345f);
    c[ImGuiCol_ButtonHovered] = gray(0.40f);
    c[ImGuiCol_ButtonActive] = gray(0.27f);
    c[ImGuiCol_Header] = accent;
    c[ImGuiCol_HeaderHovered] = gray(0.27f);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Separator] = gray(0.13f);
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = gray(0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = accentHover;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = gray(0.16f);
    c[ImGuiCol_TabHovered] = gray(0.27f);
    c[ImGuiCol_TabSelected] = gray(0.235f);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = gray(0.16f);
    c[ImGuiCol_TabDimmedSelected] = gray(0.22f);
    c[ImGuiCol_TabDimmedSelectedOverline] = gray(0.22f);
    c[ImGuiCol_DockingPreview] = {accent.x, accent.y, accent.z, 0.6f};
    c[ImGuiCol_DockingEmptyBg] = gray(0.16f);
    c[ImGuiCol_TableHeaderBg] = gray(0.25f);
    c[ImGuiCol_TextSelectedBg] = {accent.x, accent.y, accent.z, 0.6f};
    c[ImGuiCol_NavCursor] = accentHover;
    c[ImGuiCol_ModalWindowDimBg] = gray(0.0f, 0.45f);

    style.ScaleAllSizes(scale);
}

ImFont* loadFonts(float scale, ImFont*& bold) {
    ImGuiIO& io = ImGui::GetIO();
    bold = nullptr;
    std::error_code error;
    const char* regular = "C:/Windows/Fonts/segoeui.ttf";
    const char* semibold = "C:/Windows/Fonts/seguisb.ttf";
    if (!std::filesystem::exists(regular, error)) return nullptr;
    ImFont* font = io.Fonts->AddFontFromFileTTF(regular, std::round(15.0f * scale));
    if (std::filesystem::exists(semibold, error)) bold = io.Fonts->AddFontFromFileTTF(semibold, std::round(15.0f * scale));
    return font;
}

// --- Iconos dibujados con primitivas (la fuente no trae iconos) -------------------------------
enum class Icon { hand, move, rotate, scale, paint, build, terrain, play, pause, step, cube, ramp, folder, file, scene, empty };

void drawIcon(ImDrawList* draw, Icon icon, ImVec2 center, float r, ImU32 color) {
    const float t = std::max(1.0f, r * 0.17f);
    const auto p = [&](float x, float y) { return ImVec2{center.x + x * r, center.y + y * r}; };
    switch (icon) {
    case Icon::hand:
        draw->AddRectFilled(p(-0.5f, -0.1f), p(0.5f, 0.85f), color, r * 0.25f);
        for (int i = 0; i < 4; ++i) {
            const float x = -0.5f + static_cast<float>(i) * 0.27f;
            const float top = (i == 1 || i == 2) ? -0.95f : -0.78f;
            draw->AddRectFilled(p(x, top), p(x + 0.2f, 0.1f), color, r * 0.1f);
        }
        draw->AddQuadFilled(p(-0.48f, 0.2f), p(-0.9f, -0.2f), p(-0.75f, -0.35f), p(-0.38f, -0.02f), color);
        break;
    case Icon::move: {
        draw->AddLine(p(-0.7f, 0), p(0.7f, 0), color, t);
        draw->AddLine(p(0, -0.7f), p(0, 0.7f), color, t);
        const float a = 0.32f;
        draw->AddTriangleFilled(p(0, -1), p(a, -0.62f), p(-a, -0.62f), color);
        draw->AddTriangleFilled(p(0, 1), p(-a, 0.62f), p(a, 0.62f), color);
        draw->AddTriangleFilled(p(-1, 0), p(-0.62f, -a), p(-0.62f, a), color);
        draw->AddTriangleFilled(p(1, 0), p(0.62f, a), p(0.62f, -a), color);
        break;
    }
    case Icon::rotate:
        draw->PathArcTo(center, r * 0.72f, IM_PI * 0.15f, IM_PI * 1.65f, 24);
        draw->PathStroke(color, 0, t);
        draw->AddTriangleFilled(p(0.05f, -0.95f), p(0.65f, -0.62f), p(0.05f, -0.3f), color);
        break;
    case Icon::scale:
        draw->AddRect(p(-0.85f, -0.85f), p(0.85f, 0.85f), color, 0, 0, t);
        draw->AddRectFilled(p(0.1f, -0.85f), p(0.85f, -0.1f), color);
        draw->AddLine(p(-0.55f, 0.55f), p(0.15f, -0.15f), color, t);
        break;
    case Icon::paint:
        // Brocha: mango en diagonal, virola y punta.
        draw->AddLine(p(0.85f, -0.85f), p(0.05f, -0.05f), color, t * 1.6f);
        draw->AddQuadFilled(p(0.12f, -0.32f), p(0.32f, -0.12f), p(-0.05f, 0.25f), p(-0.25f, 0.05f), color);
        draw->AddTriangleFilled(p(-0.25f, 0.05f), p(-0.05f, 0.25f), p(-0.85f, 0.85f), color);
        break;
    case Icon::build:
        // Tres bloques apilados.
        draw->AddRectFilled(p(-0.9f, 0.1f), p(-0.05f, 0.9f), color, r * 0.08f);
        draw->AddRectFilled(p(0.05f, 0.1f), p(0.9f, 0.9f), color, r * 0.08f);
        draw->AddRectFilled(p(-0.42f, -0.85f), p(0.42f, -0.05f), color, r * 0.08f);
        break;
    case Icon::play:
        draw->AddTriangleFilled(p(-0.55f, -0.8f), p(0.8f, 0), p(-0.55f, 0.8f), color);
        break;
    case Icon::pause:
        draw->AddRectFilled(p(-0.65f, -0.75f), p(-0.15f, 0.75f), color);
        draw->AddRectFilled(p(0.15f, -0.75f), p(0.65f, 0.75f), color);
        break;
    case Icon::step:
        draw->AddTriangleFilled(p(-0.75f, -0.75f), p(0.3f, 0), p(-0.75f, 0.75f), color);
        draw->AddRectFilled(p(0.4f, -0.75f), p(0.75f, 0.75f), color);
        break;
    case Icon::cube:
    case Icon::scene: {
        const ImVec2 hex[] = {p(0, -0.92f), p(0.82f, -0.46f), p(0.82f, 0.46f), p(0, 0.92f), p(-0.82f, 0.46f), p(-0.82f, -0.46f)};
        draw->AddPolyline(hex, 6, color, ImDrawFlags_Closed, t);
        draw->AddLine(p(-0.82f, -0.46f), p(0, 0), color, t);
        draw->AddLine(p(0.82f, -0.46f), p(0, 0), color, t);
        draw->AddLine(p(0, 0), p(0, 0.92f), color, t);
        break;
    }
    case Icon::ramp: {
        const ImVec2 points[] = {p(-0.9f, 0.6f), p(0.9f, 0.6f), p(0.9f, -0.65f)};
        draw->AddPolyline(points, 3, color, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::terrain: {
        // Dos montañas sobre una línea de suelo.
        draw->AddTriangleFilled(p(-0.95f, 0.7f), p(-0.25f, -0.55f), p(0.45f, 0.7f), color);
        draw->AddTriangleFilled(p(0.0f, 0.7f), p(0.5f, -0.1f), p(0.98f, 0.7f), color);
        draw->AddLine(p(-0.95f, 0.85f), p(0.98f, 0.85f), color, t);
        break;
    }
    case Icon::empty:
        draw->AddCircle(center, r * 0.6f, color, 12, t);
        break;
    case Icon::folder:
        draw->AddRectFilled(p(-0.9f, -0.62f), p(-0.1f, -0.25f), color, r * 0.1f);
        draw->AddRectFilled(p(-0.9f, -0.4f), p(0.9f, 0.72f), color, r * 0.12f);
        break;
    case Icon::file: {
        const ImVec2 outline[] = {p(-0.62f, -0.88f), p(0.25f, -0.88f), p(0.62f, -0.5f), p(0.62f, 0.88f), p(-0.62f, 0.88f)};
        draw->AddPolyline(outline, 5, color, ImDrawFlags_Closed, t);
        draw->AddLine(p(0.25f, -0.88f), p(0.25f, -0.5f), color, t);
        draw->AddLine(p(0.25f, -0.5f), p(0.62f, -0.5f), color, t);
        break;
    }
    }
}

// Botón cuadrado con icono, resaltado cuando está activo.
bool iconButton(const char* id, Icon icon, bool active, ImVec2 size, const char* tooltip, bool enabled = true) {
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool held = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    ImVec4 background = active ? accent : held ? gray(0.27f) : hovered ? gray(0.40f) : gray(0.30f);
    draw->AddRectFilled(min, max, ImGui::GetColorU32(background), ImGui::GetStyle().FrameRounding);
    draw->AddRect(min, max, ImGui::GetColorU32(gray(0.12f)), ImGui::GetStyle().FrameRounding);
    const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    drawIcon(draw, icon, center, std::min(size.x, size.y) * 0.3f, ImGui::GetColorU32(active ? gray(1.0f) : gray(0.85f)));
    if (hovered && tooltip) ImGui::SetItemTooltip("%s", tooltip);
    if (!enabled) ImGui::EndDisabled();
    return pressed && enabled;
}

// Botón de texto que se mantiene resaltado mientras está activo (interruptores de las barras).
bool toggleButton(const char* label, bool active, const char* tooltip = nullptr) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentHover);
    }
    const bool pressed = ImGui::Button(label);
    if (active) ImGui::PopStyleColor(2);
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

// Nodo de árbol con un icono delante del texto (Jerarquía y Proyecto).
bool treeItem(const void* id, const char* label, Icon icon, ImU32 iconColor, ImGuiTreeNodeFlags flags) {
    const float iconSize = ImGui::GetFontSize();
    const float space = std::max(1.0f, ImGui::CalcTextSize(" ").x);
    std::string padded(static_cast<std::size_t>(std::ceil((iconSize + 4) / space)), ' ');
    padded += label;
    const bool open = ImGui::TreeNodeEx(id, flags, "%s", padded.c_str());
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float x = min.x + ImGui::GetTreeNodeToLabelSpacing();
    drawIcon(ImGui::GetWindowDrawList(), icon, {x + iconSize * 0.5f, (min.y + max.y) * 0.5f}, iconSize * 0.4f, iconColor);
    return open;
}

float hitTriangle(glm::vec3 origin, glm::vec3 direction, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const auto ab = b - a;
    const auto ac = c - a;
    const auto cross = glm::cross(direction, ac);
    const auto det = glm::dot(ab, cross);
    if (std::abs(det) < 0.00001f) return -1;
    const auto offset = origin - a;
    const auto u = glm::dot(offset, cross) / det;
    const auto q = glm::cross(offset, ab);
    const auto v = glm::dot(direction, q) / det;
    if (u < 0 || v < 0 || u + v > 1) return -1;
    return glm::dot(ac, q) / det;
}

// Fila "Etiqueta   X [  ] Y [  ] Z [  ]" del Inspector.
void vectorRow(const char* label, ge3d::Vec3& value, float speed, float minimum, float maximum,
               bool& activated, bool& deactivated) {
    ImGui::PushID(label);
    const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.30f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float letter = ImGui::CalcTextSize("X").x;
    const float width = (ImGui::GetContentRegionAvail().x - 3 * (letter + spacing) - 2 * spacing) / 3;
    float* parts[] = {&value.x, &value.y, &value.z};
    const char* letters[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, spacing);
        ImGui::TextDisabled("%s", letters[i]);
        ImGui::SameLine(0, spacing);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(std::max(30.0f, width));
        ImGui::DragFloat("##v", parts[i], speed, minimum, maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        activated = activated || ImGui::IsItemActivated();
        deactivated = deactivated || ImGui::IsItemDeactivated();
        ImGui::PopID();
    }
    ImGui::PopID();
}

// Color de vista previa (relleno semitransparente) para pinceles.
constexpr ImU32 previewFill = IM_COL32(255, 255, 255, 70);

// Cabecera plegable de componente en el Inspector (gris, no del color de selección).
bool componentHeader(const char* label, ImFont* bold) {
    ImGui::PushStyleColor(ImGuiCol_Header, gray(0.27f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, gray(0.31f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, gray(0.24f));
    if (bold) ImGui::PushFont(bold);
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    if (bold) ImGui::PopFont();
    ImGui::PopStyleColor(3);
    return open;
}

void copyText(char* buffer, std::size_t size, const std::string& text) {
    std::snprintf(buffer, size, "%s", text.c_str());
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool contains(const std::string& text, const char* filter) {
    return filter[0] == '\0' || lower(text).find(lower(filter)) != std::string::npos;
}

Icon entityIcon(const ge3d::Entity& entity) {
    if (entity.terrain || entity.heightmap) return Icon::terrain;
    if (entity.shape) return entity.shape->primitive == ge3d::Primitive::box ? Icon::cube : Icon::ramp;
    return Icon::empty;
}

const char* displayName(const ge3d::Entity& entity) {
    return entity.name.empty() ? "(sin nombre)" : entity.name.c_str();
}

std::string sceneName(const std::filesystem::path& path) {
    auto name = ge3d::pathToUtf8(path.filename());
    const std::string suffix = ".scene.json";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.resize(name.size() - suffix.size());
    }
    return name;
}

bool isSceneFile(const std::filesystem::path& path) {
    const auto name = ge3d::pathToUtf8(path.filename());
    const std::string suffix = ".scene.json";
    return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}

// =============================================================================================

EditorView::EditorView(float uiScale) : scale_(std::clamp(uiScale, 0.75f, 3.0f)) {
    applyTheme(scale_);
    loadFonts(scale_, boldFont_);

    scenePath_ = ge3d::pathFromUtf8(GE3D_DEFAULT_SCENE);
    contentRoot_ = ge3d::pathFromUtf8(GE3D_CONTENT_DIR);
    std::error_code error;
    if (std::filesystem::exists(scenePath_, error)) {
        loadScene();
    } else {
        log("Escena de ejemplo sin guardar. Pulsa Ctrl+S para guardarla en " + ge3d::pathToUtf8(scenePath_));
    }
    refreshProject();
}

std::string EditorView::windowTitle() const {
    std::string title = "Game-Engine3D | " + sceneName(scenePath_);
    if (hasUnsavedChanges()) title += " *";
    if (isPlaying()) title += paused_ ? "  [Pausa]" : "  [Play]";
    return title;
}

void EditorView::log(std::string message, bool error) {
    editorLog().add(error ? LogLevel::error : LogLevel::info, std::move(message));
}

// --- Materiales en la interfaz -----------------------------------------------------------------

void EditorView::setMaterials(const ge3d::MaterialLibrary& library, const std::vector<ImTextureID>& thumbnails) {
    library_ = library;
    thumbnails_ = thumbnails;
    if (!library_.empty() && !library_.find(paintMaterial_)) paintMaterial_ = library_.materials().front().id;
}

const char* EditorView::materialName(const ge3d::MaterialId& material) const {
    const auto* definition = library_.find(material);
    return definition ? definition->name.c_str() : material.c_str();
}

bool EditorView::materialSwatch(const char* id, const ge3d::MaterialId& material, bool selected, float size) {
    const auto index = library_.indexOf(material);
    bool pressed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {1, 1});
    if (index && *index < thumbnails_.size()) {
        pressed = ImGui::ImageButton(id, thumbnails_[*index], {size, size});
    } else {
        // Material que no está en el catálogo: se ve igual que en la escena (cuadros magenta).
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.85f, 0.1f, 0.75f, 1});
        pressed = ImGui::Button(id, {size + 2, size + 2});
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    if (selected) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect({min.x - 1, min.y - 1}, {max.x + 1, max.y + 1}, IM_COL32(255, 255, 255, 255), 0, 0, 2);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(index ? "%s  (%s)" : "%s  (%s, no esta en materials.json)", materialName(material), material.c_str());
    }
    return pressed;
}

bool EditorView::materialPicker(ge3d::MaterialId& material, float size, int perRow) {
    bool changed = false;
    const auto& materials = library_.materials();
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (i % static_cast<std::size_t>(perRow) != 0) ImGui::SameLine(0, 4);
        ImGui::PushID(static_cast<int>(i));
        if (materialSwatch("##material", materials[i].id, materials[i].id == material, size)) {
            material = materials[i].id;
            changed = true;
        }
        ImGui::PopID();
    }
    if (materials.empty()) ImGui::TextDisabled("No hay materiales: revisa Content/materials.json");
    return changed;
}

void EditorView::materialStrip() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Material");
    const float swatch = ImGui::GetFrameHeight() - 4;
    for (const auto& material : library_.materials()) {
        ImGui::SameLine(0, 3);
        ImGui::PushID(material.id.c_str());
        if (materialSwatch("##tira", material.id, material.id == paintMaterial_, swatch)) paintMaterial_ = material.id;
        ImGui::PopID();
    }
}

// --- Terreno -------------------------------------------------------------------------------------

glm::mat4 EditorView::terrainMatrix(const ge3d::Entity& entity) {
    const float voxel = entity.terrain ? entity.terrain->settings.voxelSize : 1.0f;
    return modelMatrix(entity.transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{voxel});
}

std::optional<EditorView::Hit> EditorView::raycastTerrain(const ge3d::Entity& entity, const Ray& ray) const {
    if (!entity.terrain || !entity.terrain->voxels) return std::nullopt;
    const auto& grid = *entity.terrain->voxels;
    const glm::mat4 model = terrainMatrix(entity);
    const glm::mat4 inverse = glm::inverse(model);
    const glm::vec3 origin = glm::vec3(inverse * glm::vec4(ray.start, 1.0f));
    const glm::vec3 direction = glm::vec3(inverse * glm::vec4(ray.direction, 0.0f));
    const glm::vec3 size{static_cast<float>(grid.sizeX()), static_cast<float>(grid.sizeY()), static_cast<float>(grid.sizeZ())};

    // Entrada y salida de la caja del terreno (método de las losas). El parámetro t es el mismo en el
    // mundo y en el espacio del terreno, porque la transformación es lineal.
    float enter = 0.0f, leave = std::numeric_limits<float>::max();
    int enterAxis = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-9f) {
            if (origin[axis] < 0 || origin[axis] > size[axis]) return std::nullopt;
            continue;
        }
        float a = (0.0f - origin[axis]) / direction[axis];
        float b = (size[axis] - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        if (a > enter) {
            enter = a;
            enterAxis = axis;
        }
        leave = std::min(leave, b);
        if (enter > leave) return std::nullopt;
    }

    // Recorrido vóxel a vóxel (Amanatides y Woo).
    const glm::vec3 start = origin + direction * (enter + 1e-4f);
    glm::ivec3 voxel = glm::clamp(glm::ivec3(glm::floor(start)), glm::ivec3(0), glm::ivec3(size) - 1);
    glm::ivec3 step{0};
    glm::vec3 next{std::numeric_limits<float>::max()};
    glm::vec3 delta{std::numeric_limits<float>::max()};
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] > 0) {
            step[axis] = 1;
            next[axis] = (static_cast<float>(voxel[axis] + 1) - origin[axis]) / direction[axis];
        } else if (direction[axis] < 0) {
            step[axis] = -1;
            next[axis] = (static_cast<float>(voxel[axis]) - origin[axis]) / direction[axis];
        }
        if (step[axis] != 0) delta[axis] = std::abs(1.0f / direction[axis]);
    }
    float t = enter;
    int axis = enterAxis;
    const int maxSteps = grid.sizeX() + grid.sizeY() + grid.sizeZ();
    for (int i = 0; i <= maxSteps; ++i) {
        if (grid.get(voxel.x, voxel.y, voxel.z) != 0) {
            glm::vec3 localNormal{0.0f};
            localNormal[axis] = step[axis] != 0 ? -static_cast<float>(step[axis]) : 1.0f;
            Hit hit;
            hit.entity = entity.id;
            hit.face = static_cast<std::uint32_t>(axis * 2 + (localNormal[axis] > 0 ? 1 : 0));
            hit.distance = t;
            hit.point = ray.start + ray.direction * t;
            hit.normal = glm::normalize(glm::vec3(glm::transpose(inverse) * glm::vec4(localNormal, 0.0f)));
            hit.terrain = true;
            hit.voxel = voxel;
            return hit;
        }
        axis = next.x < next.y ? (next.x < next.z ? 0 : 2) : (next.y < next.z ? 1 : 2);
        t = next[axis];
        if (t > leave) break;
        voxel[axis] += step[axis];
        next[axis] += delta[axis];
        if (voxel[axis] < 0 || voxel[axis] >= static_cast<int>(size[axis])) break;
    }
    return std::nullopt;
}

std::optional<EditorView::Hit> EditorView::raycastHeightmap(const ge3d::Entity& entity, const Ray& ray) const {
    if (!entity.heightmap || !entity.heightmap->data) return std::nullopt;
    const auto& map = *entity.heightmap->data;
    const float cell = entity.heightmap->settings.cellSize;
    const glm::mat4 model = modelMatrix(entity.transform);
    const glm::mat4 inverse = glm::inverse(model);
    const glm::vec3 origin = glm::vec3(inverse * glm::vec4(ray.start, 1.0f));
    const glm::vec3 direction = glm::vec3(inverse * glm::vec4(ray.direction, 0.0f));

    // Caja del terreno en su espacio local (metros): xz del tamaño de la rejilla, y de la altura mínima a la máxima.
    float low = ge3d::Heightmap::maxHeight, high = -ge3d::Heightmap::maxHeight;
    for (int chunk = 0; chunk < map.chunkCount(); ++chunk) {
        const auto range = map.chunkHeightRange(chunk);
        low = std::min(low, range[0]);
        high = std::max(high, range[1]);
    }
    const glm::vec3 boxMin{0.0f, low - 0.01f, 0.0f};
    const glm::vec3 boxMax{static_cast<float>(map.cellsX()) * cell, high + 0.01f, static_cast<float>(map.cellsZ()) * cell};
    float enter = 0.0f, leave = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-9f) {
            if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) return std::nullopt;
            continue;
        }
        float a = (boxMin[axis] - origin[axis]) / direction[axis];
        float b = (boxMax[axis] - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
        if (enter > leave) return std::nullopt;
    }
    // Marcha a pasos de media celda y bisección al cruzar la superficie.
    const auto above = [&](float t) {
        const glm::vec3 p = origin + direction * t;
        return p.y - map.surfaceHeight(p.x / cell, p.z / cell);
    };
    const float largest = std::max({std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)});
    const float step = cell * 0.5f / std::max(largest, 1e-6f);
    float previous = enter;
    float previousValue = above(enter);
    if (previousValue < 0.0f) return std::nullopt;   // la cámara está bajo el terreno
    for (float t = enter + step; previous < leave; t += step) {
        t = std::min(t, leave);
        const float value = above(t);
        if (value <= 0.0f) {
            float a = previous, b = t;
            for (int i = 0; i < 12; ++i) {
                const float middle = (a + b) * 0.5f;
                (above(middle) > 0.0f ? a : b) = middle;
            }
            const glm::vec3 local = origin + direction * b;
            const auto n = map.normal(static_cast<int>(std::lround(local.x / cell)), static_cast<int>(std::lround(local.z / cell)), cell);
            Hit hit;
            hit.entity = entity.id;
            hit.distance = b;
            hit.point = ray.start + ray.direction * b;
            hit.normal = glm::normalize(glm::vec3(glm::transpose(inverse) * glm::vec4(n[0], n[1], n[2], 0.0f)));
            hit.terrain = true;
            hit.heightmap = true;
            return hit;
        }
        previous = t;
        previousValue = value;
    }
    return std::nullopt;
}

void EditorView::createHeightmap() {
    const auto settings = newHeightmap_;
    const auto count = std::count_if(scene_.entities().begin(), scene_.entities().end(), [](const ge3d::Entity& entity) {
        return entity.terrain.has_value() || entity.heightmap.has_value();
    });
    // Centrado en el origen; las alturas del generador oscilan alrededor de y = 0.
    ge3d::Transform transform;
    const float width = static_cast<float>(settings.chunksX * ge3d::Heightmap::chunkCells) * settings.cellSize;
    const float depth = static_cast<float>(settings.chunksZ * ge3d::Heightmap::chunkCells) * settings.cellSize;
    transform.position = {-width * 0.5f, 0.0f, -depth * 0.5f};
    const auto started = std::chrono::steady_clock::now();
    auto entity = scene_.makeEntity("Terreno " + std::to_string(count + 1), transform);
    entity.heightmap = ge3d::makeHeightmap(settings);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const auto id = entity.id;
    execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
    selected_ = id;
    pivot_ = {0.0f, 0.0f, 0.0f};
    distance_ = std::max(distance_, width * 0.6f);
    char message[96];
    std::snprintf(message, sizeof message, "Terreno generado en %.0f ms", elapsed);
    log(message);
}

void EditorView::applyHeightBrush(const Hit& hit, float seconds) {
    auto* entity = scene_.find(sculptTarget_);
    if (!entity || !entity->heightmap || !entity->heightmap->data) return;
    auto& map = *entity->heightmap->data;
    const float cell = entity->heightmap->settings.cellSize;
    const glm::vec3 local = glm::vec3(glm::inverse(modelMatrix(entity->transform)) * glm::vec4(hit.point, 1.0f));
    const float cx = local.x / cell, cz = local.z / cell;
    const float radius = static_cast<float>(terrainRadius_);
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius))), x1 = std::min(map.verticesX() - 1, static_cast<int>(std::ceil(cx + radius)));
    const int z0 = std::max(0, static_cast<int>(std::floor(cz - radius))), z1 = std::min(map.verticesZ() - 1, static_cast<int>(std::ceil(cz + radius)));
    if (x0 > x1 || z0 > z1) return;

    // Guarda el contenido previo de cada chunk que use alguno de los vértices tocados (los bordes son compartidos).
    constexpr int size = ge3d::Heightmap::chunkCells;
    for (int chunkZ = std::max(0, (z0 - 1) / size); chunkZ <= std::min(map.chunksZ() - 1, z1 / size); ++chunkZ) {
        for (int chunkX = std::max(0, (x0 - 1) / size); chunkX <= std::min(map.chunksX() - 1, x1 / size); ++chunkX) {
            const int chunk = map.chunkIndex(chunkX, chunkZ);
            if (!heightBefore_.contains(chunk)) heightBefore_.emplace(chunk, map.chunkData(chunk));
        }
    }

    // Suavizar lee de una copia para que el resultado no dependa del orden del recorrido.
    std::vector<float> source;
    const int width = x1 - x0 + 1;
    if (terrainBrush_ == TerrainBrush::smooth) {
        source.reserve(static_cast<std::size_t>(width * (z1 - z0 + 1)));
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) source.push_back(map.height(x, z));
        }
    }
    const auto sourceHeight = [&](int x, int z) {
        if (x < x0 || x > x1 || z < z0 || z > z1) return map.height(x, z);
        return source[static_cast<std::size_t>((z - z0) * width + (x - x0))];
    };
    // Velocidad: un pincel grande sube más rápido para que el tamaño no cambie la sensación.
    const float rate = (2.0f + radius * cell * 0.6f) * brushStrength_ * seconds;
    const float blend = std::min(1.0f, 8.0f * brushStrength_ * seconds);
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float distance = std::hypot(static_cast<float>(x) - cx, static_cast<float>(z) - cz);
            if (distance > radius) continue;
            const float falloff = 0.5f + 0.5f * std::cos(3.14159265f * distance / radius);
            const float height = map.height(x, z);
            switch (terrainBrush_) {
            case TerrainBrush::add: map.setHeight(x, z, height + rate * falloff); break;
            case TerrainBrush::remove: map.setHeight(x, z, height - rate * falloff); break;
            case TerrainBrush::flatten: map.setHeight(x, z, height + (flattenLevel_ - height) * blend * falloff); break;
            case TerrainBrush::smooth: {
                const float average = (sourceHeight(x - 1, z) + sourceHeight(x + 1, z) + sourceHeight(x, z - 1) +
                                       sourceHeight(x, z + 1) + sourceHeight(x, z) * 4.0f) / 8.0f;
                map.setHeight(x, z, height + (average - height) * blend * falloff);
                break;
            }
            case TerrainBrush::paint:
                map.setWeights(x, z, ge3d::blendTowardLayer(map.weights(x, z), paintLayer_, blend * falloff));
                break;
            case TerrainBrush::water: {
                // Echar agua (Shift: quitarla). La simulación la reparte cuesta abajo.
                const float amount = rate * falloff * (ImGui::GetIO().KeyShift ? -1.5f : 1.0f);
                map.setWater(x, z, std::max(0.0f, map.water(x, z) + amount));
                break;
            }
            }
        }
    }
    // El agua alrededor del pincel vuelve a moverse (fluye hacia lo excavado o se aparta de lo elevado).
    auto& simulation = waterSimulation(entity->id);
    simulation.wakeAround(map, static_cast<int>(cx), static_cast<int>(cz), terrainRadius_ + 2);
    if (!isPlaying()) simulation.advance(map, cell, seconds);
}

void EditorView::simulateWater(float seconds) {
    for (auto& entity : scene_.entities()) {
        if (!entity.heightmap || !entity.heightmap->data) continue;
        auto* target = scene_.find(entity.id);
        waterSimulation(entity.id).advance(*target->heightmap->data, target->heightmap->settings.cellSize, seconds);
    }
}

void EditorView::editWater(ge3d::Entity& entity, const char* label, bool fill, float level) {
    auto& map = *entity.heightmap->data;
    ge3d::EditCommand command;
    command.label = label;
    for (int chunk = 0; chunk < map.chunkCount(); ++chunk) {
        auto before = map.chunkData(chunk);
        const auto [cx, cz] = map.chunkCoords(chunk);
        for (int z = 0; z < ge3d::Heightmap::chunkVertices; ++z) {
            for (int x = 0; x < ge3d::Heightmap::chunkVertices; ++x) {
                const int gx = cx * ge3d::Heightmap::chunkCells + x, gz = cz * ge3d::Heightmap::chunkCells + z;
                map.setWater(gx, gz, fill ? std::max(0.0f, level - map.height(gx, gz)) : 0.0f);
            }
        }
        auto after = map.chunkData(chunk);
        if (after != before) command.heightmapChanges.push_back({entity.id, chunk, std::move(before), std::move(after)});
    }
    waterSimulation(entity.id).reset();
    activeHistory().record(std::move(command));
}

void EditorView::createTerrain() {
    auto settings = newTerrain_;
    const auto count = std::count_if(scene_.entities().begin(), scene_.entities().end(),
                                     [](const ge3d::Entity& entity) { return entity.terrain.has_value(); });
    // Centrado en el origen con la altura media del relieve a la altura del suelo (y = 0).
    ge3d::Transform transform;
    const float voxel = settings.voxelSize;
    transform.position = {-static_cast<float>(settings.chunksX * ge3d::VoxelGrid::chunkSize) * voxel * 0.5f,
                          -std::round(settings.generator.baseHeight) * voxel,
                          -static_cast<float>(settings.chunksZ * ge3d::VoxelGrid::chunkSize) * voxel * 0.5f};
    const auto started = std::chrono::steady_clock::now();
    auto entity = scene_.makeEntity("Terreno " + std::to_string(count + 1), transform);
    entity.terrain = ge3d::makeTerrain(settings);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const auto id = entity.id;
    execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
    selected_ = id;
    pivot_ = {0.0f, 0.0f, 0.0f};
    distance_ = std::max(distance_, static_cast<float>(settings.chunksX * ge3d::VoxelGrid::chunkSize) * voxel * 0.6f);
    char message[96];
    std::snprintf(message, sizeof message, "Terreno generado en %.0f ms", elapsed);
    log(message);
}

void EditorView::applyTerrainBrush(const Hit& hit) {
    auto* entity = scene_.find(sculptTarget_);
    if (!entity || !entity->terrain || !entity->terrain->voxels) return;
    auto& grid = *entity->terrain->voxels;
    const auto material = terrainBrush_ == TerrainBrush::remove ? ge3d::VoxelGrid::Voxel{0} : grid.materialIndex(paintMaterial_);
    if (terrainBrush_ != TerrainBrush::remove && material == 0) {
        log("El terreno ya usa 255 materiales: no se pueden anadir mas", true);
        return;
    }
    // Guarda el contenido previo de cada chunk la primera vez que el trazo lo toca.
    const auto change = [&](int x, int y, int z, ge3d::VoxelGrid::Voxel value) {
        if (!grid.contains(x, y, z) || grid.get(x, y, z) == value) return;
        const int chunk = grid.chunkIndex(x / ge3d::VoxelGrid::chunkSize, y / ge3d::VoxelGrid::chunkSize,
                                          z / ge3d::VoxelGrid::chunkSize);
        if (!sculptBefore_.contains(chunk)) sculptBefore_.emplace(chunk, grid.chunkData(chunk));
        grid.set(x, y, z, value);
    };
    const int r = terrainRadius_;
    glm::ivec3 center = hit.voxel;
    if (terrainBrush_ == TerrainBrush::add) center[static_cast<int>(hit.face / 2)] += hit.face % 2 ? 1 : -1;
    if (terrainBrush_ == TerrainBrush::flatten) {
        // Aplanar: vacía por encima de la altura inicial del trazo y rellena por debajo, en un cilindro.
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dz * dz > r * r + r) continue;
                const int x = center.x + dx, z = center.z + dz;
                for (int y = flattenHeight_ + 1; y <= flattenHeight_ + r * 2 + 2; ++y) change(x, y, z, 0);
                for (int y = flattenHeight_ - r; y <= flattenHeight_; ++y) {
                    if (grid.contains(x, y, z) && grid.get(x, y, z) == 0) change(x, y, z, material);
                }
            }
        }
        return;
    }
    for (int dz = -r; dz <= r; ++dz) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy + dz * dz > r * r + r) continue;
                const int x = center.x + dx, y = center.y + dy, z = center.z + dz;
                if (!grid.contains(x, y, z)) continue;
                const auto current = grid.get(x, y, z);
                if (terrainBrush_ == TerrainBrush::add && current == 0) change(x, y, z, material);
                if (terrainBrush_ == TerrainBrush::remove) change(x, y, z, 0);
                if (terrainBrush_ == TerrainBrush::paint && current != 0) change(x, y, z, material);
            }
        }
    }
}

void EditorView::finishSculpt() {
    sculpting_ = false;
    const auto* entity = scene_.find(sculptTarget_);
    if (entity && entity->heightmap && entity->heightmap->data && !heightBefore_.empty()) {
        static constexpr const char* labels[] = {"Elevar terreno", "Bajar terreno", "Pintar terreno", "Aplanar terreno",
                                                 "Suavizar terreno", "Agua"};
        ge3d::EditCommand command;
        command.label = labels[static_cast<int>(terrainBrush_)];
        auto& map = *entity->heightmap->data;
        if (!isPlaying() && !waterBefore_.empty()) {
            // El agua termina de asentarse ahora para que el trazo entero (con su caudal) se deshaga de una vez.
            waterSimulation(entity->id).settle(map, entity->heightmap->settings.cellSize, 400.0);
            constexpr int size = ge3d::Heightmap::chunkCells;
            for (int chunk = 0; chunk < map.chunkCount(); ++chunk) {
                if (heightBefore_.contains(chunk)) continue;
                const auto [cx, cz] = map.chunkCoords(chunk);
                auto before = map.chunkData(chunk);
                bool changed = false;
                for (int z = 0; z < ge3d::Heightmap::chunkVertices; ++z) {
                    for (int x = 0; x < ge3d::Heightmap::chunkVertices; ++x) {
                        const auto index = static_cast<std::size_t>(cz * size + z) * static_cast<std::size_t>(map.verticesX()) +
                                           static_cast<std::size_t>(cx * size + x);
                        auto& depth = before.water[static_cast<std::size_t>(z * ge3d::Heightmap::chunkVertices + x)];
                        changed = changed || depth != waterBefore_[index];
                        depth = waterBefore_[index];
                    }
                }
                if (changed) heightBefore_.emplace(chunk, std::move(before));
            }
        }
        waterBefore_.clear();
        for (auto& [chunk, before] : heightBefore_) {
            auto after = entity->heightmap->data->chunkData(chunk);
            if (after != before) command.heightmapChanges.push_back({entity->id, chunk, std::move(before), std::move(after)});
        }
        activeHistory().record(std::move(command));
    }
    heightBefore_.clear();
    if (entity && entity->terrain && entity->terrain->voxels && !sculptBefore_.empty()) {
        static constexpr const char* labels[] = {"Elevar terreno", "Excavar terreno", "Pintar terreno", "Aplanar terreno",
                                                 "Suavizar terreno"};
        ge3d::EditCommand command;
        command.label = labels[static_cast<int>(terrainBrush_)];
        for (auto& [chunk, before] : sculptBefore_) {
            auto after = entity->terrain->voxels->chunkData(chunk);
            if (after != before) command.terrainChanges.push_back({entity->id, chunk, std::move(before), std::move(after)});
        }
        activeHistory().record(std::move(command));
    }
    sculptBefore_.clear();
}

void EditorView::handleTerrainTool(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool canUseMouse) {
    const auto& io = ImGui::GetIO();
    auto hit = canUseMouse ? raycast(io.MousePos, origin, size) : std::nullopt;
    if (hit && !hit->terrain) hit.reset();
    if (!hit) return;
    const auto* entity = scene_.find(hit->entity);
    if (!entity || (!entity->terrain && !entity->heightmap)) return;
    if (hit->heightmap && !entity->heightmap) return;

    // Vista previa: círculo del pincel sobre la superficie (en el terreno de malla, pegado al relieve).
    const glm::mat4 clip = projection() * view();
    const ImU32 colors[] = {IM_COL32(120, 230, 120, 230), IM_COL32(255, 100, 90, 230), IM_COL32(255, 255, 255, 230),
                            IM_COL32(250, 210, 90, 230), IM_COL32(120, 200, 255, 230), IM_COL32(80, 160, 255, 230)};
    ImVec2 ring[49];
    int points = 0;
    const auto project = [&](const glm::vec3& world) {
        const glm::vec4 c = clip * glm::vec4(world, 1.0f);
        if (c.w <= 0.001f) return;
        ring[points++] = {origin.x + (c.x / c.w * 0.5f + 0.5f) * size.x, origin.y + (0.5f - c.y / c.w * 0.5f) * size.y};
    };
    if (hit->heightmap) {
        const auto& map = *entity->heightmap->data;
        const float cell = entity->heightmap->settings.cellSize;
        const glm::mat4 model = modelMatrix(entity->transform);
        const glm::vec3 local = glm::vec3(glm::inverse(model) * glm::vec4(hit->point, 1.0f));
        const float radius = static_cast<float>(terrainRadius_) * cell;
        for (int i = 0; i <= 48; ++i) {
            const float angle = static_cast<float>(i) / 48.0f * 6.2831853f;
            const float x = local.x + std::cos(angle) * radius, z = local.z + std::sin(angle) * radius;
            project(glm::vec3(model * glm::vec4(x, map.surfaceHeight(x / cell, z / cell) + 0.05f, z, 1.0f)));
        }
    } else {
        const float voxel = entity->terrain->settings.voxelSize;
        const float radius = (static_cast<float>(terrainRadius_) + 0.5f) * voxel;
        const glm::vec3 n = hit->normal;
        const glm::vec3 tangent = glm::normalize(glm::abs(n.y) < 0.9f ? glm::cross(n, glm::vec3{0, 1, 0}) : glm::cross(n, glm::vec3{1, 0, 0}));
        const glm::vec3 bitangent = glm::cross(n, tangent);
        for (int i = 0; i <= 32; ++i) {
            const float angle = static_cast<float>(i) / 32.0f * 6.2831853f;
            project(hit->point + n * 0.02f + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius);
        }
    }
    if (points > 2) draw->AddPolyline(ring, points, colors[static_cast<int>(terrainBrush_)], 0, 1.5f);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImGui::SetWindowFocus();
        sculpting_ = true;
        sculptTarget_ = hit->entity;
        selected_ = hit->entity;
        sculptBefore_.clear();
        heightBefore_.clear();
        waterBefore_.clear();
        if (hit->heightmap && (terrainBrush_ == TerrainBrush::water || entity->heightmap->data->hasWater())) {
            waterBefore_ = entity->heightmap->data->allWater();
        }
        flattenHeight_ = hit->voxel.y;
        flattenLevel_ = glm::vec3(glm::inverse(modelMatrix(entity->transform)) * glm::vec4(hit->point, 1.0f)).y;
        lastSculptTime_ = -1.0;
    }
    if (!sculpting_ || hit->entity != sculptTarget_ || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    if (hit->heightmap) {
        // Terreno de malla: el pincel actúa de forma continua según el tiempo transcurrido.
        applyHeightBrush(*hit, std::min(ImGui::GetIO().DeltaTime, 0.05f));
    } else if (ImGui::GetTime() - lastSculptTime_ >= 0.06) {
        // Vóxeles: a ritmo fijo mientras se mantiene pulsado (no depende de los FPS).
        lastSculptTime_ = ImGui::GetTime();
        applyTerrainBrush(*hit);
    }
}

void EditorView::drawTerrainComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& terrain = *entity.terrain;
    if (draftOwner_ != entity.id) {
        terrainDraft_ = terrain.settings;
        draftOwner_ = entity.id;
    }
    auto& draft = terrainDraft_;
    const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    const auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
    };

    if (terrain.voxels) {
        const auto& grid = *terrain.voxels;
        ImGui::TextDisabled("%d x %d x %d voxeles  (%d chunks de 32)", grid.sizeX(), grid.sizeY(), grid.sizeZ(), grid.chunkCount());
        ImGui::TextDisabled("%.0f x %.0f x %.0f m", grid.sizeX() * terrain.settings.voxelSize,
                            grid.sizeY() * terrain.settings.voxelSize, grid.sizeZ() * terrain.settings.voxelSize);
    }
    if (terrainStats_.terrains > 0) {
        ImGui::TextDisabled("Vista: %d/%d chunks, %zu caras", terrainStats_.visibleChunks, terrainStats_.chunks, terrainStats_.visibleQuads);
        if (terrainStats_.pendingChunks > 0) ImGui::TextDisabled("Mallando: %d chunks pendientes", terrainStats_.pendingChunks);
    }

    ImGui::SeparatorText("Generador");
    int seed = static_cast<int>(draft.generator.seed & 0x7FFFFFFF);
    row("Semilla");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2.6f);
    if (ImGui::InputInt("##semilla", &seed, 1, 100)) draft.generator.seed = static_cast<std::uint32_t>(std::max(0, seed));
    ImGui::SameLine();
    if (ImGui::Button("Azar")) draft.generator.seed = static_cast<std::uint32_t>(ge3d::Uuid::generate().low & 0x7FFFFFFF);
    const float maxHeight = static_cast<float>(draft.chunksY * ge3d::VoxelGrid::chunkSize);
    row("Altura base");
    ImGui::SliderFloat("##base", &draft.generator.baseHeight, 0.0f, maxHeight, "%.0f vox");
    row("Relieve");
    ImGui::SliderFloat("##amplitud", &draft.generator.amplitude, 0.0f, maxHeight * 0.75f, "%.0f vox");
    row("Anchura colinas");
    ImGui::SliderFloat("##escala", &draft.generator.scale, 4.0f, 256.0f, "%.0f vox", ImGuiSliderFlags_Logarithmic);
    row("Detalle");
    ImGui::SliderInt("##octavas", &draft.generator.octaves, 1, 8);
    row("Nivel del agua");
    ImGui::SliderInt("##agua", &draft.generator.waterLevel, 0, static_cast<int>(maxHeight), draft.generator.waterLevel ? "%d vox" : "sin agua");

    const auto materialRow = [&](const char* label, ge3d::MaterialId& material) {
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        if (materialSwatch("##muestra", material, false, ImGui::GetFrameHeight() - 2)) ImGui::OpenPopup("materiales");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", materialName(material));
        if (ImGui::BeginPopup("materiales")) {
            if (materialPicker(material, ImGui::GetFrameHeight() * 1.4f, 6)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    materialRow("Superficie", draft.generator.top);
    materialRow("Subsuelo", draft.generator.under);
    materialRow("Roca", draft.generator.deep);
    materialRow("Orilla", draft.generator.shore);
    materialRow("Agua", draft.generator.water);

    ImGui::SeparatorText("Tamano");
    row("Chunks X / Z");
    int horizontal[2] = {draft.chunksX, draft.chunksZ};
    if (ImGui::SliderInt2("##chunksxz", horizontal, 1, ge3d::VoxelGrid::maxChunksXZ)) {
        draft.chunksX = horizontal[0];
        draft.chunksZ = horizontal[1];
    }
    row("Chunks Y");
    ImGui::SliderInt("##chunksy", &draft.chunksY, 1, ge3d::VoxelGrid::maxChunksY);
    row("Tamano del voxel");
    static const float voxelSizes[] = {0.25f, 0.5f, 1.0f, 2.0f};
    static const char* voxelNames[] = {"0,25 m", "0,5 m", "1 m", "2 m"};
    int voxelIndex = draft.voxelSize < 0.375f ? 0 : draft.voxelSize < 0.75f ? 1 : draft.voxelSize < 1.5f ? 2 : 3;
    if (ImGui::Combo("##voxel", &voxelIndex, voxelNames, 4)) draft.voxelSize = voxelSizes[voxelIndex];

    ImGui::Spacing();
    const bool changed = draft != terrain.settings;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    if (ImGui::Button("Generar", {buttonWidth, 0})) {
        const auto started = std::chrono::steady_clock::now();
        std::optional<ge3d::Entity> before = frameStart;
        entity.terrain = ge3d::makeTerrain(draft);
        commitModification(before, "Generar terreno");
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        char message[96];
        std::snprintf(message, sizeof message, "Terreno generado en %.0f ms", elapsed);
        log(message);
    }
    ImGui::SetItemTooltip("Crea el relieve con esta configuracion. Sustituye las ediciones a mano (se puede deshacer).");
    ImGui::SameLine();
    if (ImGui::Button("Plano", {buttonWidth, 0})) {
        std::optional<ge3d::Entity> before = frameStart;
        entity.terrain = ge3d::TerrainComponent{draft, ge3d::makeFlatTerrain(draft, static_cast<int>(draft.generator.baseHeight),
                                                                             draft.generator.top)};
        commitModification(before, "Terreno plano");
    }
    ImGui::SetItemTooltip("Terreno llano a la altura base, para esculpirlo a mano con la herramienta Terreno (T)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Revertir", {buttonWidth, 0})) draft = terrain.settings;
    ImGui::EndDisabled();
    if (changed) ImGui::TextDisabled("Hay cambios sin aplicar: pulsa Generar");
}

void EditorView::drawHeightmapComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& terrain = *entity.heightmap;
    if (heightDraftOwner_ != entity.id) {
        heightDraft_ = terrain.settings;
        heightDraftOwner_ = entity.id;
    }
    auto& draft = heightDraft_;
    draft.layers = terrain.settings.layers;   // las capas se aplican al momento, no forman parte del borrador
    draft.waterMaterial = terrain.settings.waterMaterial;
    const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    const auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
    };
    if (terrain.data) {
        const auto& map = *terrain.data;
        ImGui::TextDisabled("%d x %d celdas  (%d chunks de 64)", map.cellsX(), map.cellsZ(), map.chunkCount());
        ImGui::TextDisabled("%.0f x %.0f m", static_cast<float>(map.cellsX()) * terrain.settings.cellSize,
                            static_cast<float>(map.cellsZ()) * terrain.settings.cellSize);
    }

    ImGui::SeparatorText("Capas");
    ImGui::TextDisabled("Se pintan con la herramienta Terreno (T)");
    static constexpr const char* layerNames[] = {"Base", "Capa 2", "Capa 3", "Capa 4"};
    for (int layer = 0; layer < ge3d::Heightmap::layerCount; ++layer) {
        auto& material = terrain.settings.layers[static_cast<std::size_t>(layer)];
        ImGui::PushID(layer);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(layerNames[layer]);
        ImGui::SameLine(labelWidth);
        if (materialSwatch("##muestra", material, paintLayer_ == layer, ImGui::GetFrameHeight() - 2)) ImGui::OpenPopup("materiales");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", materialName(material));
        if (ImGui::BeginPopup("materiales")) {
            auto chosen = material;
            if (materialPicker(chosen, ImGui::GetFrameHeight() * 1.4f, 6)) {
                std::optional<ge3d::Entity> before = frameStart;
                material = chosen;
                commitModification(before, "Cambiar capa del terreno");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Generador");
    int seed = static_cast<int>(draft.generator.seed & 0x7FFFFFFF);
    row("Semilla");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 2.6f);
    if (ImGui::InputInt("##semilla", &seed, 1, 100)) draft.generator.seed = static_cast<std::uint32_t>(std::max(0, seed));
    ImGui::SameLine();
    if (ImGui::Button("Azar")) draft.generator.seed = static_cast<std::uint32_t>(ge3d::Uuid::generate().low & 0x7FFFFFFF);
    row("Relieve");
    ImGui::SliderFloat("##amplitud", &draft.generator.amplitude, 0.0f, 80.0f, "%.1f m");
    row("Anchura colinas");
    ImGui::SliderFloat("##escala", &draft.generator.scale, 4.0f, 512.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
    row("Detalle");
    ImGui::SliderInt("##octavas", &draft.generator.octaves, 1, 8);
    row("Roca en pendientes");
    ImGui::SliderFloat("##roca", &draft.generator.rockSlope, 0.0f, 1.0f, draft.generator.rockSlope > 0 ? "%.2f" : "no pintar");

    ImGui::SeparatorText("Agua");
    row("Mar y lagos");
    ImGui::Checkbox("##mar", &draft.generator.water);
    ImGui::SetItemTooltip("Al generar, se llena de agua todo lo que quede por debajo del nivel");
    row("Nivel del agua");
    ImGui::SliderFloat("##nivel", &draft.generator.waterLevel, -40.0f, 40.0f, "%.1f m");
    {
        auto& material = terrain.settings.waterMaterial;
        ImGui::PushID("agua");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Textura");
        ImGui::SameLine(labelWidth);
        if (materialSwatch("##muestra", material, false, ImGui::GetFrameHeight() - 2)) ImGui::OpenPopup("materiales");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", materialName(material));
        if (ImGui::BeginPopup("materiales")) {
            auto chosen = material;
            if (materialPicker(chosen, ImGui::GetFrameHeight() * 1.4f, 6)) {
                std::optional<ge3d::Entity> before = frameStart;
                material = chosen;
                commitModification(before, "Cambiar textura del agua");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (terrain.data) {
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Llenar hasta el nivel", {half, 0})) {
            editWater(entity, "Llenar de agua", true, draft.generator.waterLevel);
        }
        ImGui::SetItemTooltip("Pone agua en todo lo que queda por debajo del nivel (sin regenerar el relieve)");
        ImGui::SameLine();
        if (ImGui::Button("Vaciar agua", {half, 0})) editWater(entity, "Vaciar agua", false, 0.0f);
        ImGui::TextDisabled("Pincel Agua (T) para echar agua; en Play fluye sola");
    }

    ImGui::SeparatorText("Tamano");
    row("Chunks X / Z");
    int horizontal[2] = {draft.chunksX, draft.chunksZ};
    if (ImGui::SliderInt2("##chunksxz", horizontal, 1, ge3d::Heightmap::maxChunks)) {
        draft.chunksX = horizontal[0];
        draft.chunksZ = horizontal[1];
    }
    row("Tamano de celda");
    static const float cellSizes[] = {0.25f, 0.5f, 1.0f, 2.0f};
    static const char* cellNames[] = {"0,25 m", "0,5 m", "1 m", "2 m"};
    int cellIndex = draft.cellSize < 0.375f ? 0 : draft.cellSize < 0.75f ? 1 : draft.cellSize < 1.5f ? 2 : 3;
    if (ImGui::Combo("##celda", &cellIndex, cellNames, 4)) draft.cellSize = cellSizes[cellIndex];

    ImGui::Spacing();
    const bool changed = draft != terrain.settings;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
    if (ImGui::Button("Generar", {buttonWidth, 0})) {
        const auto started = std::chrono::steady_clock::now();
        std::optional<ge3d::Entity> before = frameStart;
        entity.heightmap = ge3d::makeHeightmap(draft);
        commitModification(before, "Generar terreno");
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        char message[96];
        std::snprintf(message, sizeof message, "Terreno generado en %.0f ms", elapsed);
        log(message);
    }
    ImGui::SetItemTooltip("Crea el relieve con esta configuracion. Sustituye las ediciones a mano (se puede deshacer).");
    ImGui::SameLine();
    if (ImGui::Button("Plano", {buttonWidth, 0})) {
        std::optional<ge3d::Entity> before = frameStart;
        entity.heightmap = ge3d::HeightmapComponent{draft, ge3d::makeFlatHeightmap(draft)};
        commitModification(before, "Terreno plano");
    }
    ImGui::SetItemTooltip("Terreno llano para modelarlo a mano con la herramienta Terreno (T)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Revertir", {buttonWidth, 0})) draft = terrain.settings;
    ImGui::EndDisabled();
    if (changed) ImGui::TextDisabled("Hay cambios sin aplicar: pulsa Generar");
}

// --- Cámara ----------------------------------------------------------------------------------

glm::mat4 EditorView::view() const {
    const glm::vec3 orbit{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
                          std::cos(yaw_) * std::cos(pitch_)};
    return glm::lookAtRH(pivot_ + orbit * distance_, pivot_, {0, 1, 0});
}

// Distancia de dibujo y zoom máximo. El mundo no tiene límites; esto solo acota lo que se ve.
constexpr float nearPlane = 0.1f;
constexpr float farPlane = 2000.0f;
constexpr float maxDistance = 600.0f;
constexpr float fieldOfView = 48.0f;   // grados en vertical
constexpr int sceneMargin = 1;         // píxeles extra por lado para desplazar la imagen sin bordes vacíos

glm::mat4 EditorView::projection() const {
    return glm::perspectiveRH_NO(glm::radians(fieldOfView), aspect_, nearPlane, farPlane);
}

EditorView::CameraSnap EditorView::cameraSnap() const {
    if (!style_.snapCamera || style_.pixelSize <= 1) return {};
    // Tamaño en el mundo de un píxel de la escena a la distancia del punto que se mira. Si la cámara
    // solo se mueve en múltiplos de ese tamaño, lo que se ve no cambia de píxel al desplazarse; el resto
    // se compensa moviendo la imagen una fracción de píxel en pantalla (el movimiento sigue siendo suave).
    const glm::mat4 inverseView = glm::inverse(view());
    const glm::vec3 right{inverseView[0]};
    const glm::vec3 up{inverseView[1]};
    const float texel = 2.0f * distance_ * std::tan(glm::radians(fieldOfView) * 0.5f) / static_cast<float>(sceneSize_.y);
    if (texel <= 0.0f) return {};
    const float x = glm::dot(pivot_, right) / texel;
    const float y = glm::dot(pivot_, up) / texel;
    const float dx = std::round(x) - x;
    const float dy = std::round(y) - y;
    return {(right * dx + up * dy) * texel, {-dx, dy}};
}

EditorView::GridView EditorView::gridView() const {
    // La separación crece con el zoom (1, 10, 100 m) para que la cuadrícula no se vuelva ruido.
    const float spacing = distance_ < 40.0f ? 1.0f : distance_ < 400.0f ? 10.0f : 100.0f;
    const glm::vec2 focus{pivot_.x, pivot_.z};
    const glm::vec2 offset = glm::round(focus / spacing) * spacing;
    // A 320 x 180 las líneas lejanas se convierten en ruido: se desvanecen antes.
    const float fadeRadius = std::max(12.0f, distance_ * 2.2f);
    return {offset, focus, spacing, fadeRadius};
}

glm::vec3 EditorView::cameraPosition() const {
    const glm::vec3 orbit{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)};
    return pivot_ + orbit * distance_;
}

EditorView::SceneView EditorView::sceneView() const {
    // De OpenGL (y arriba, z de -1 a 1) a Vulkan (y abajo, z de 0 a 1).
    glm::mat4 clip{1.0f};
    clip[1][1] = -1;
    clip[2][2] = 0.5f;
    clip[3][2] = 0.5f;
    const auto snap = cameraSnap();
    const glm::mat4 snapped = view() * glm::translate(glm::mat4{1.0f}, -snap.offset);
    // La imagen tiene un margen: la proyección se amplía para que la zona central coincida con la vista.
    const float width = static_cast<float>(sceneSize_.x + 2 * sceneMargin);
    const float height = static_cast<float>(sceneSize_.y + 2 * sceneMargin);
    const float tangent = std::tan(glm::radians(fieldOfView) * 0.5f) * height / static_cast<float>(sceneSize_.y);
    const glm::mat4 projection = glm::perspectiveRH_NO(2.0f * std::atan(tangent), width / height, nearPlane, farPlane);
    SceneView result;
    result.viewProj = clip * projection * snapped;
    result.extent = {sceneSize_.x + 2 * sceneMargin, sceneSize_.y + 2 * sceneMargin};
    result.nearPlane = nearPlane;
    result.farPlane = farPlane;
    return result;
}

void EditorView::setViewFromMatrix(const glm::mat4& matrix) {
    // La columna Z de la inversa de la vista apunta desde el objetivo hacia la cámara.
    const auto inverse = glm::inverse(matrix);
    const glm::vec3 back = glm::normalize(glm::vec3(inverse[2]));
    pitch_ = std::clamp(std::asin(std::clamp(back.y, -1.0f, 1.0f)), -1.56f, 1.56f);
    if (std::abs(back.x) > 1e-5f || std::abs(back.z) > 1e-5f) yaw_ = std::atan2(back.x, back.z);
}

glm::mat4 EditorView::modelMatrix(const ge3d::Transform& transform) {
    const float translation[] = {transform.position.x, transform.position.y, transform.position.z};
    const float rotation[] = {transform.rotationDegrees.x, transform.rotationDegrees.y, transform.rotationDegrees.z};
    const float scale[] = {transform.scale.x, transform.scale.y, transform.scale.z};
    glm::mat4 model{1.0f};
    ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, glm::value_ptr(model));
    return model;
}

namespace {
const ge3d::Mesh& meshOf(ge3d::Primitive primitive) {
    static const auto box = ge3d::makePrimitive(ge3d::Primitive::box);
    static const auto ramp = ge3d::makePrimitive(ge3d::Primitive::ramp);
    return primitive == ge3d::Primitive::box ? box : ramp;
}
}

EditorView::Ray EditorView::mouseRay(ImVec2 mouse, ImVec2 origin, ImVec2 size) const {
    const auto inverse = glm::inverse(projection() * view());
    const float x = (mouse.x - origin.x) / size.x * 2 - 1;
    const float y = 1 - (mouse.y - origin.y) / size.y * 2;
    const auto near = inverse * glm::vec4(x, y, -1, 1);
    const auto far = inverse * glm::vec4(x, y, 1, 1);
    const auto start = glm::vec3(near) / near.w;
    return {start, glm::normalize(glm::vec3(far) / far.w - start)};
}

std::optional<EditorView::Hit> EditorView::raycast(ImVec2 mouse, ImVec2 origin, ImVec2 size) const {
    const auto ray = mouseRay(mouse, origin, size);
    std::optional<Hit> best;
    for (const auto& entity : scene_.entities()) {
        if (entity.terrain) {
            if (auto hit = raycastTerrain(entity, ray); hit && (!best || hit->distance < best->distance)) best = hit;
        }
        if (entity.heightmap) {
            if (auto hit = raycastHeightmap(entity, ray); hit && (!best || hit->distance < best->distance)) best = hit;
        }
        if (!entity.shape) continue;
        const auto model = modelMatrix(entity.transform);
        const auto& mesh = meshOf(entity.shape->primitive);
        for (const auto& triangle : mesh.triangles) {
            const auto point = [&](std::uint32_t index) {
                return glm::vec3(model * glm::vec4(vector(mesh.positions[index]), 1));
            };
            const auto a = point(triangle.indices[0]);
            const auto b = point(triangle.indices[1]);
            const auto c = point(triangle.indices[2]);
            const auto distance = hitTriangle(ray.start, ray.direction, a, b, c);
            if (distance >= 0 && (!best || distance < best->distance)) {
                auto normal = glm::normalize(glm::cross(b - a, c - a));
                if (glm::dot(normal, ray.direction) > 0) normal = -normal;  // hacia la cámara
                best = Hit{entity.id, triangle.face, distance, ray.start + ray.direction * distance, normal};
            }
        }
    }
    return best;
}

void EditorView::pick(ImVec2 mouse, ImVec2 origin, ImVec2 size) {
    const auto hit = raycast(mouse, origin, size);
    selected_ = hit ? hit->entity : 0;
}

void EditorView::drawPieceOutline(ImDrawList* draw, ge3d::Primitive primitive, const glm::mat4& model, ImVec2 origin,
                                  ImVec2 size, ImU32 lineColor, ImU32 fillColor, int face) const {
    const auto matrix = projection() * view() * model;
    const auto& mesh = meshOf(primitive);
    const bool wholePiece = face < 0;
    const auto project = [&](std::uint32_t index, ImVec2& out) {
        const auto clip = matrix * glm::vec4(vector(mesh.positions[index]), 1);
        if (clip.w <= 0.001f) return false;
        const glm::vec2 ndc{clip.x / clip.w, clip.y / clip.w};
        out = {origin.x + (ndc.x * 0.5f + 0.5f) * size.x, origin.y + (0.5f - ndc.y * 0.5f) * size.y};
        return true;
    };
    // Se rellenan los triángulos y se perfilan solo los bordes de cada cara (las aristas que
    // aparecen una vez dentro de su cara), sin las diagonales internas.
    std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, int> edges;
    for (const auto& triangle : mesh.triangles) {
        if (!wholePiece && triangle.face != static_cast<std::uint32_t>(face)) continue;
        ImVec2 points[3];
        bool visible = true;
        for (std::size_t i = 0; i < 3; ++i) visible = visible && project(triangle.indices[i], points[i]);
        if (!visible) continue;
        if (fillColor & IM_COL32_A_MASK) draw->AddTriangleFilled(points[0], points[1], points[2], fillColor);
        for (std::size_t i = 0; i < 3; ++i) {
            const auto a = triangle.indices[i];
            const auto b = triangle.indices[(i + 1) % 3];
            ++edges[{triangle.face, std::min(a, b), std::max(a, b)}];
        }
    }
    for (const auto& [edge, count] : edges) {
        ImVec2 a, b;
        if (count == 1 && project(std::get<1>(edge), a) && project(std::get<2>(edge), b)) {
            draw->AddLine(a, b, lineColor, 1.5f);
        }
    }
}

// --- Construcción ------------------------------------------------------------------------------

std::optional<EditorView::Cell> EditorView::buildCellAt(ImVec2 mouse, ImVec2 origin, ImVec2 size) const {
    // Contra una pieza: la celda vecina a la cara apuntada. Si no, contra el suelo (y = 0).
    glm::vec3 point{};
    glm::vec3 normal{0, 1, 0};
    const auto ray = mouseRay(mouse, origin, size);
    const float groundDistance = ray.direction.y < -1e-4f ? -ray.start.y / ray.direction.y : -1.0f;
    const auto hit = raycast(mouse, origin, size);
    if (hit && (groundDistance < 0 || hit->distance <= groundDistance)) {
        point = hit->point;
        normal = hit->normal;
    } else if (groundDistance > 0 && groundDistance < farPlane) {
        point = ray.start + ray.direction * groundDistance;
    } else {
        return std::nullopt;
    }
    const glm::vec3 target = point + normal * (buildCell_ * 0.5f);
    const glm::ivec3 cell = glm::ivec3(glm::floor(target / buildCell_));
    return Cell{cell.x, cell.y, cell.z};
}

ge3d::Transform EditorView::cellTransform(const Cell& cell) const {
    const auto [x, y, z] = cell;
    ge3d::Transform transform;
    transform.position = {(static_cast<float>(x) + 0.5f) * buildCell_, (static_cast<float>(y) + 0.5f) * buildCell_,
                          (static_cast<float>(z) + 0.5f) * buildCell_};
    transform.rotationDegrees = {0, static_cast<float>(buildRotation_ * 90), 0};
    transform.scale = {buildCell_, buildCell_, buildCell_};
    return transform;
}

bool EditorView::cellOccupied(const Cell& cell) const {
    const auto center = cellTransform(cell).position;
    const float tolerance = buildCell_ * 0.25f;
    return std::any_of(scene_.entities().begin(), scene_.entities().end(), [&](const ge3d::Entity& entity) {
        const auto& p = entity.transform.position;
        return std::abs(p.x - center.x) < tolerance && std::abs(p.y - center.y) < tolerance &&
               std::abs(p.z - center.z) < tolerance;
    });
}

std::vector<EditorView::Cell> EditorView::pendingCells() const {
    std::vector<Cell> cells;
    if (!building_) return cells;
    if (buildRect_) {
        // Rectángulo en la capa (altura) donde empezó el trazo.
        const auto [x0, y0, z0] = buildStart_;
        const auto [x1, y1, z1] = buildCurrent_;
        (void)y1;
        constexpr int limit = 64;
        for (int x = std::min(x0, x1); x <= std::max(x0, x1) && x - std::min(x0, x1) < limit; ++x) {
            for (int z = std::min(z0, z1); z <= std::max(z0, z1) && z - std::min(z0, z1) < limit; ++z) {
                cells.emplace_back(x, y0, z);
            }
        }
    } else {
        cells.assign(buildStroke_.begin(), buildStroke_.end());
    }
    cells.erase(std::remove_if(cells.begin(), cells.end(), [&](const Cell& cell) { return cellOccupied(cell); }),
                cells.end());
    return cells;
}

void EditorView::handleBuildTool(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool canUseMouse) {
    const auto& io = ImGui::GetIO();
    const ImU32 white = IM_COL32(255, 255, 255, 230);
    const ImU32 red = IM_COL32(255, 90, 80, 240);
    const ImU32 ghostFill = previewFill;

    // Borrar (Ctrl): marca en rojo las piezas tocadas y las elimina al soltar. El terreno no se borra
    // con este pincel (para eso está la herramienta Terreno).
    if (erasing_ || (canUseMouse && io.KeyCtrl && !building_)) {
        auto hit = canUseMouse ? raycast(io.MousePos, origin, size) : std::nullopt;
        if (hit && hit->terrain) hit.reset();
        if (canUseMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) erasing_ = true;
        if (erasing_ && hit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) eraseStroke_.insert(hit->entity);
        for (const auto id : eraseStroke_) {
            if (const auto* entity = scene_.find(id); entity && entity->shape) {
                drawPieceOutline(draw, entity->shape->primitive, modelMatrix(entity->transform), origin, size, red,
                                 IM_COL32(255, 60, 50, 90));
            }
        }
        if (hit && !eraseStroke_.contains(hit->entity)) {
            if (const auto* entity = scene_.find(hit->entity); entity && entity->shape) {
                drawPieceOutline(draw, entity->shape->primitive, modelMatrix(entity->transform), origin, size, red, 0);
            }
        }
        return;
    }

    // Colocar: clic = una pieza, arrastre = trazo, Shift + arrastre = rectángulo.
    const auto cell = canUseMouse || building_ ? buildCellAt(io.MousePos, origin, size) : std::nullopt;
    if (canUseMouse && cell && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        building_ = true;
        buildRect_ = io.KeyShift;
        buildStart_ = *cell;
        buildStroke_.clear();
    }
    if (building_ && cell && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (buildRect_) {
            // El rectángulo se mantiene en la capa inicial aunque el cursor pase sobre otras piezas.
            buildCurrent_ = {std::get<0>(*cell), std::get<1>(buildStart_), std::get<2>(*cell)};
        } else {
            buildStroke_.insert(*cell);
        }
    }

    if (building_) {
        for (const auto& pending : pendingCells()) {
            drawPieceOutline(draw, buildShape_, modelMatrix(cellTransform(pending)), origin, size, white, ghostFill);
        }
    } else if (cell && canUseMouse) {
        const bool occupied = cellOccupied(*cell);
        drawPieceOutline(draw, buildShape_, modelMatrix(cellTransform(*cell)), origin, size,
                         occupied ? red : white, occupied ? 0 : ghostFill);
    }
}

void EditorView::finishBuildStroke() {
    const auto cells = pendingCells();
    building_ = false;
    buildStroke_.clear();
    if (cells.empty()) return;
    // Todas las piezas del trazo se crean (y se deshacen) en una sola operación.
    const bool box = buildShape_ == ge3d::Primitive::box;
    const std::string prefix = box ? "Bloque " : "Rampa ";
    auto count = std::count_if(scene_.entities().begin(), scene_.entities().end(),
                               [&](const ge3d::Entity& entity) { return entity.name.starts_with(prefix); });
    ge3d::EditCommand command;
    command.label = cells.size() == 1 ? (box ? "Colocar bloque" : "Colocar rampa")
                                      : "Construir " + std::to_string(cells.size()) + " piezas";
    std::size_t index = scene_.entities().size();
    for (const auto& cell : cells) {
        auto entity = scene_.makeEntity(prefix + std::to_string(++count), cellTransform(cell));
        entity.shape = ge3d::makeShape(buildShape_, paintMaterial_);
        command.changes.push_back({std::nullopt, std::move(entity), index++});
    }
    execute(std::move(command));
}

void EditorView::finishEraseStroke() {
    erasing_ = false;
    if (eraseStroke_.empty()) return;
    // Se borran de la posición más alta a la más baja: así los índices guardados siguen siendo válidos
    // al deshacer (se reinsertan en orden inverso).
    std::vector<std::pair<std::size_t, ge3d::EntityId>> targets;
    for (const auto id : eraseStroke_) {
        if (const auto index = scene_.indexOf(id)) targets.emplace_back(*index, id);
    }
    eraseStroke_.clear();
    std::sort(targets.begin(), targets.end(), std::greater<>());
    ge3d::EditCommand command;
    command.label = targets.size() == 1 ? "Borrar pieza" : "Borrar " + std::to_string(targets.size()) + " piezas";
    for (const auto& [index, id] : targets) command.changes.push_back({*scene_.find(id), std::nullopt, index});
    execute(std::move(command));
}

void EditorView::paintAt(const Hit& hit, bool wholePiece) {
    auto* entity = scene_.find(hit.entity);
    if (!entity || !entity->shape || hit.terrain) return;
    auto materials = entity->shape->materials;
    if (wholePiece) materials.fill(paintMaterial_);
    else materials[hit.face] = paintMaterial_;
    if (materials == entity->shape->materials) return;
    paintStroke_.try_emplace(entity->id, *entity);
    entity->shape->materials = materials;
}

void EditorView::finishPaintStroke() {
    painting_ = false;
    if (paintStroke_.empty()) return;
    // Todo el trazo, aunque toque varias piezas, se deshace de una vez.
    ge3d::EditCommand command;
    command.label = "Pintar";
    for (const auto& [id, before] : paintStroke_) {
        if (auto change = ge3d::makeModifyCommand(scene_, before, "")) command.changes.push_back(change->changes.front());
    }
    paintStroke_.clear();
    activeHistory().record(std::move(command));
}

void EditorView::focusSelection() {
    if (const auto* entity = scene_.find(selected_)) {
        pivot_ = vector(entity->transform.position);
        const auto scale = entity->transform.scale;
        distance_ = std::clamp(std::max({scale.x, scale.y, scale.z}) * 3.2f, 2.0f, maxDistance);
    }
}

// --- Edición -----------------------------------------------------------------------------------

void EditorView::validateSelection() {
    if (!scene_.find(selected_)) selected_ = 0;
}

void EditorView::execute(ge3d::EditCommand command) {
    try {
        activeHistory().execute(scene_, std::move(command));
    } catch (const std::exception& error) {
        log(std::string("No se pudo aplicar la edicion: ") + error.what(), true);
    }
    validateSelection();
}

void EditorView::commitModification(std::optional<ge3d::Entity>& before, const char* label) {
    if (!before) return;
    if (auto command = ge3d::makeModifyCommand(scene_, *before, label)) activeHistory().record(std::move(*command));
    before.reset();
}

void EditorView::createPrimitive(ge3d::Primitive primitive) {
    const bool box = primitive == ge3d::Primitive::box;
    const auto count = std::count_if(scene_.entities().begin(), scene_.entities().end(),
                                     [&](const ge3d::Entity& entity) { return entity.shape && entity.shape->primitive == primitive; });
    ge3d::Transform transform;
    // Se coloca donde mira la cámara, ajustado a la cuadrícula y apoyado sobre el suelo.
    transform.position = {std::round(pivot_.x), 0.5f, std::round(pivot_.z)};
    auto entity = scene_.makeEntity(std::string(box ? "Caja " : "Rampa ") + std::to_string(count + 1), primitive, transform);
    const auto id = entity.id;
    execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
    selected_ = id;
}

void EditorView::duplicateSelection() {
    if (editInProgress()) return;
    if (auto command = ge3d::makeDuplicateCommand(scene_, selected_, {1, 0, 0})) {
        const auto id = command->changes.front().after->id;
        execute(std::move(*command));
        selected_ = id;
    }
}

void EditorView::deleteSelection() {
    if (editInProgress()) return;
    if (auto command = ge3d::makeDeleteCommand(scene_, selected_)) {
        execute(std::move(*command));
        selected_ = 0;
    }
}

void EditorView::undo() {
    if (editInProgress()) return;
    auto& history = activeHistory();
    try {
        if (const auto* label = history.undoLabel()) {
            const auto text = *label;
            if (history.undo(scene_)) log("Deshecho: " + text);
        }
    } catch (const std::exception& error) {
        log(std::string("No se pudo deshacer: ") + error.what(), true);
    }
    validateSelection();
}

void EditorView::redo() {
    if (editInProgress()) return;
    auto& history = activeHistory();
    try {
        if (const auto* label = history.redoLabel()) {
            const auto text = *label;
            if (history.redo(scene_)) log("Rehecho: " + text);
        }
    } catch (const std::exception& error) {
        log(std::string("No se pudo rehacer: ") + error.what(), true);
    }
    validateSelection();
}

// --- Modo Play ---------------------------------------------------------------------------------

void EditorView::togglePlay() {
    if (editInProgress()) return;
    if (!isPlaying()) {
        // La escena de edición se aparta intacta; Play trabaja sobre una copia con sus propios vóxeles.
        playSnapshot_ = scene_;
        scene_ = scene_.deepCopy();
        playHistory_.clear();
        paused_ = false;
        playFrames_ = 0;
        // El agua de la copia empieza despierta: si no estaba en reposo, fluye.
        water_.clear();
        for (const auto& entity : scene_.entities()) {
            if (entity.heightmap && entity.heightmap->data) waterSimulation(entity.id).wakeAll(*entity.heightmap->data);
        }
        log("Play: los cambios que hagas ahora se descartan al detener");
    } else {
        scene_ = std::move(*playSnapshot_);
        playSnapshot_.reset();
        water_.clear();
        playHistory_.clear();
        paused_ = false;
        gizmoEdit_.reset();
        inspectorEdit_.reset();
        validateSelection();
        log("Detenido: se recupera la escena de edicion");
    }
}

void EditorView::togglePause() {
    if (!isPlaying()) return;
    paused_ = !paused_;
}

void EditorView::step() {
    if (isPlaying() && paused_) {
        ++playFrames_;
        simulateWater(1.0f / 60.0f);
    }
}

// --- Escena y archivos -------------------------------------------------------------------------

void EditorView::requestSceneChange(SceneChange change, std::filesystem::path path) {
    pendingChange_ = change;
    pendingPath_ = std::move(path);
    if (hasUnsavedChanges()) {
        openUnsavedPopup_ = true;
    } else {
        performSceneChange();
    }
}

void EditorView::requestQuit() {
    if (pendingChange_ == SceneChange::quit) return;
    requestSceneChange(SceneChange::quit);
}

void EditorView::performSceneChange() {
    const auto change = std::exchange(pendingChange_, SceneChange::none);
    if (isPlaying() && change != SceneChange::none) togglePlay();
    switch (change) {
    case SceneChange::none: break;
    case SceneChange::newScene: newScene(); break;
    case SceneChange::open:
        scenePath_ = pendingPath_;
        loadScene();
        break;
    case SceneChange::reload: loadScene(); break;
    case SceneChange::quit: quitApproved_ = true; break;
    }
}

void EditorView::newScene() {
    ge3d::Scene scene;
    scene.create("Floor", ge3d::Primitive::box, {{0, -0.25f, 0}, {}, {8, 0.5f, 8}});
    scene_ = std::move(scene);
    history_.clear();
    savedState_ = notSaved;
    selected_ = 0;
    gizmoEdit_.reset();
    inspectorEdit_.reset();
    log("Escena nueva");
}

bool EditorView::saveScene() {
    if (editInProgress()) return false;
    if (isPlaying()) {
        editorLog().add(LogLevel::warning, "No se guarda en modo Play: deten el juego primero");
        return false;
    }
    try {
        ge3d::saveSceneFile(scene_, scenePath_);
        savedState_ = history_.state();
        log("Guardado " + ge3d::pathToUtf8(scenePath_));
        refreshProject();
        return true;
    } catch (const std::exception& error) {
        log(std::string("No se pudo guardar: ") + error.what(), true);
        return false;
    }
}

void EditorView::loadScene() {
    try {
        // Se carga en una escena aparte: si el archivo es inválido la escena abierta no cambia.
        auto loaded = ge3d::loadSceneFile(scenePath_);
        scene_ = std::move(loaded);
        history_.clear();
        savedState_ = history_.state();
        selected_ = 0;
        gizmoEdit_.reset();
        inspectorEdit_.reset();
        log("Abierto " + ge3d::pathToUtf8(scenePath_));
    } catch (const std::exception& error) {
        log(std::string("No se pudo abrir la escena (se conserva la actual): ") + error.what(), true);
    }
}

void EditorView::refreshProject() {
    namespace fs = std::filesystem;
    std::error_code error;
    projectFound_ = fs::is_directory(contentRoot_, error);
    project_ = {ge3d::pathToUtf8(contentRoot_.filename()), contentRoot_, true, {}};
    if (!projectFound_) return;
    const auto build = [&](auto& self, ProjectNode& node, int depth) -> void {
        if (depth > 8) return;
        for (fs::directory_iterator it(node.path, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            const bool directory = it->is_directory(error);
            ProjectNode child{ge3d::pathToUtf8(it->path().filename()), it->path(), directory, {}};
            if (child.name.empty() || child.name.ends_with(".tmp")) continue;
            if (directory) self(self, child, depth + 1);
            node.children.push_back(std::move(child));
        }
        error.clear();
        std::sort(node.children.begin(), node.children.end(), [](const ProjectNode& a, const ProjectNode& b) {
            if (a.directory != b.directory) return a.directory;
            return lower(a.name) < lower(b.name);
        });
    };
    build(build, project_, 0);
}

// --- Atajos ------------------------------------------------------------------------------------

void EditorView::handleShortcuts() {
    if (ImGui::GetIO().WantTextInput) return;
    constexpr ImGuiInputFlags repeat = ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat;
    constexpr ImGuiInputFlags once = ImGuiInputFlags_RouteGlobal;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, repeat)) undo();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, repeat) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, repeat)) redo();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, once)) saveScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, once)) requestSceneChange(SceneChange::newScene);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, once)) pathPopup_ = PathPopup::open;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, once)) duplicateSelection();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, once)) togglePlay();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, once)) togglePause();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, once)) step();
    if (ImGui::Shortcut(ImGuiKey_Delete, once)) deleteSelection();
    if (!orbiting_ && !panning_) {
        if (ImGui::Shortcut(ImGuiKey_Q, once)) tool_ = Tool::hand;
        if (ImGui::Shortcut(ImGuiKey_W, once)) tool_ = Tool::move;
        if (ImGui::Shortcut(ImGuiKey_E, once)) tool_ = Tool::rotate;
        if (ImGui::Shortcut(ImGuiKey_R, once)) tool_ = Tool::scale;
        if (ImGui::Shortcut(ImGuiKey_B, once)) tool_ = Tool::paint;
        if (ImGui::Shortcut(ImGuiKey_C, once)) tool_ = Tool::build;
        if (ImGui::Shortcut(ImGuiKey_T, once)) tool_ = Tool::terrain;
        if (tool_ == Tool::terrain && ImGui::Shortcut(ImGuiKey_LeftBracket, once)) terrainRadius_ = std::max(1, terrainRadius_ - 1);
        if (tool_ == Tool::terrain && ImGui::Shortcut(ImGuiKey_RightBracket, once)) terrainRadius_ = std::min(32, terrainRadius_ + 1);
        if (tool_ == Tool::build && ImGui::Shortcut(ImGuiKey_Space, once)) buildRotation_ = (buildRotation_ + 1) % 4;
        if (ImGui::Shortcut(ImGuiKey_F, once)) focusSelection();
    }
}

// --- Menú, barra de herramientas y barra de estado --------------------------------------------

void EditorView::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("Archivo")) {
        if (ImGui::MenuItem("Nueva escena", "Ctrl+N")) requestSceneChange(SceneChange::newScene);
        if (ImGui::MenuItem("Abrir escena...", "Ctrl+O")) pathPopup_ = PathPopup::open;
        if (ImGui::MenuItem("Recargar desde disco")) requestSceneChange(SceneChange::reload);
        ImGui::Separator();
        if (ImGui::MenuItem("Guardar", "Ctrl+S", false, !isPlaying())) saveScene();
        if (ImGui::MenuItem("Guardar como...", nullptr, false, !isPlaying())) pathPopup_ = PathPopup::saveAs;
        ImGui::Separator();
        if (ImGui::MenuItem("Salir")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editar")) {
        auto& history = activeHistory();
        const auto* undoLabel = history.undoLabel();
        const auto* redoLabel = history.redoLabel();
        const auto undoText = undoLabel ? "Deshacer " + *undoLabel : std::string("Deshacer");
        const auto redoText = redoLabel ? "Rehacer " + *redoLabel : std::string("Rehacer");
        if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, history.canUndo())) undo();
        if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, history.canRedo())) redo();
        ImGui::Separator();
        const bool hasSelection = scene_.find(selected_) != nullptr;
        if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, hasSelection)) duplicateSelection();
        if (ImGui::MenuItem("Eliminar", "Supr", false, hasSelection)) deleteSelection();
        if (ImGui::MenuItem("Enfocar seleccion", "F", false, hasSelection)) focusSelection();
        ImGui::Separator();
        if (ImGui::MenuItem(isPlaying() ? "Detener" : "Play", "Ctrl+P")) togglePlay();
        if (ImGui::MenuItem("Pausa", "Ctrl+Shift+P", paused_, isPlaying())) togglePause();
        if (ImGui::MenuItem("Paso", "Ctrl+Alt+P", false, isPlaying() && paused_)) step();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Objeto")) {
        if (ImGui::MenuItem("Caja")) createPrimitive(ge3d::Primitive::box);
        if (ImGui::MenuItem("Rampa")) createPrimitive(ge3d::Primitive::ramp);
        ImGui::Separator();
        if (ImGui::MenuItem("Terreno")) createHeightmap();
        if (ImGui::MenuItem("Terreno de voxeles")) createTerrain();
        if (ImGui::MenuItem("Objeto vacio")) {
            auto entity = scene_.makeEntity("Objeto", ge3d::Transform{{std::round(pivot_.x), 0, std::round(pivot_.z)}, {}, {1, 1, 1}});
            const auto id = entity.id;
            execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
            selected_ = id;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ventana")) {
        ImGui::MenuItem("Jerarquia", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Proyecto", nullptr, &showProject_);
        ImGui::MenuItem("Consola", nullptr, &showConsole_);
        ImGui::Separator();
        if (ImGui::MenuItem("Restablecer distribucion")) {
            showHierarchy_ = showInspector_ = showProject_ = showConsole_ = true;
            layoutCreated_ = false;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda")) {
        if (ImGui::MenuItem("Atajos de teclado")) openShortcutsPopup_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorView::drawToolbar() {
    const float height = std::round(36.0f * scale_);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8 * scale_, 5 * scale_});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, gray(0.15f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (ImGui::BeginViewportSideBar("##barra_herramientas", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags)) {
        const float button = height - 10 * scale_;
        const ImVec2 size{button * 1.15f, button};
        const struct { Tool tool; Icon icon; const char* id; const char* tip; } tools[] = {
            {Tool::hand, Icon::hand, "##mano", "Mano: arrastra para desplazar la vista (Q)"},
            {Tool::move, Icon::move, "##mover", "Mover (W)"},
            {Tool::rotate, Icon::rotate, "##rotar", "Rotar (E)"},
            {Tool::scale, Icon::scale, "##escalar", "Escalar (R)"},
            {Tool::paint, Icon::paint, "##pintar", "Pintar caras (B)\nClic o arrastre: pinta la cara\n"
                                                   "Shift+clic: pinta toda la pieza\nCtrl+clic: toma el color (cuentagotas)"},
            {Tool::build, Icon::build, "##construir", "Construir (C)\nClic: coloca una pieza en la celda\n"
                                                      "Arrastre: coloca un trazo\nShift+arrastre: rellena un rectangulo\n"
                                                      "Ctrl+arrastre: borra piezas\nEspacio: gira la pieza 90 grados"},
            {Tool::terrain, Icon::terrain, "##terreno", "Terreno (T)\nPinceles: elevar, bajar, pintar, aplanar y suavizar\n"
                                                        "Mantener pulsado para seguir aplicando\n[ y ]: tamano del pincel"},
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1.0f, 0.0f});
        for (const auto& item : tools) {
            if (&item != &tools[0]) ImGui::SameLine();
            if (iconButton(item.id, item.icon, tool_ == item.tool, size, item.tip)) tool_ = item.tool;
        }
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 14 * scale_);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8 * scale_, (button - ImGui::GetFontSize()) * 0.5f});
        if (toggleButton(localAxes_ ? "Local" : "Global", false, "Ejes del gizmo: globales o del objeto")) localAxes_ = !localAxes_;
        ImGui::SameLine();
        if (toggleButton("Ajuste", snap_, "Mover en pasos de 0.25 y rotar en pasos de 15 grados")) snap_ = !snap_;
        ImGui::PopStyleVar();

        // Controles de juego centrados.
        const float playWidth = size.x * 3 + 2;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, (ImGui::GetWindowWidth() - playWidth) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1.0f, 0.0f});
        if (iconButton("##play", Icon::play, isPlaying(), size, isPlaying() ? "Detener (Ctrl+P)" : "Play (Ctrl+P)")) togglePlay();
        ImGui::SameLine();
        if (iconButton("##pausa", Icon::pause, paused_, size, "Pausa (Ctrl+Shift+P)", isPlaying())) togglePause();
        ImGui::SameLine();
        if (iconButton("##paso", Icon::step, false, size, "Avanzar un fotograma (Ctrl+Alt+P)", isPlaying() && paused_)) step();
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void EditorView::drawStatusBar() {
    const float height = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8 * scale_, 2 * scale_});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, gray(0.15f));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    if (ImGui::BeginViewportSideBar("##barra_estado", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags)) {
        const auto last = editorLog().last();
        const ImVec4 color = last.level == LogLevel::error ? errorColor
                           : last.level == LogLevel::warning ? warningColor : gray(0.70f);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(last.message.c_str(), last.message.c_str() + std::min<std::size_t>(last.message.size(), 160));
        ImGui::PopStyleColor();

        char right[96];
        if (isPlaying()) {
            std::snprintf(right, sizeof right, "%s  fotograma %llu   |   %.0f FPS", paused_ ? "Pausa" : "Play",
                          static_cast<unsigned long long>(playFrames_), ImGui::GetIO().Framerate);
        } else {
            std::snprintf(right, sizeof right, "Edicion   |   %.0f FPS", ImGui::GetIO().Framerate);
        }
        const float width = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, ImGui::GetWindowWidth() - width - 10 * scale_));
        ImGui::TextDisabled("%s", right);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// --- Ventanas emergentes -----------------------------------------------------------------------

void EditorView::drawPathPopup() {
    static const char* title = "Ruta de escena";
    if (pathPopup_ != PathPopup::none && !ImGui::IsPopupOpen(title)) {
        copyText(pathBuffer_, sizeof pathBuffer_, ge3d::pathToUtf8(scenePath_));
        ImGui::OpenPopup(title);
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        pathPopup_ = PathPopup::none;
        return;
    }
    const bool open = pathPopup_ == PathPopup::open;
    ImGui::TextUnformatted(open ? "Archivo .scene.json que quieres abrir" : "Archivo .scene.json donde guardar la escena");
    ImGui::SetNextItemWidth(560 * scale_);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool entered = ImGui::InputText("##ruta", pathBuffer_, sizeof pathBuffer_, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button(open ? "Abrir" : "Guardar") || entered) {
        const auto path = ge3d::pathFromUtf8(pathBuffer_);
        pathPopup_ = PathPopup::none;
        ImGui::CloseCurrentPopup();
        if (open) {
            requestSceneChange(SceneChange::open, path);
        } else {
            scenePath_ = path;
            savedState_ = notSaved;
            saveScene();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        pathPopup_ = PathPopup::none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorView::drawUnsavedPopup() {
    static const char* title = "Cambios sin guardar";
    if (openUnsavedPopup_) {
        ImGui::OpenPopup(title);
        openUnsavedPopup_ = false;
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("La escena \"%s\" tiene cambios sin guardar.", sceneName(scenePath_).c_str());
    ImGui::TextDisabled("Si continuas sin guardar, se perderan.");
    ImGui::Spacing();
    if (ImGui::Button("Guardar")) {
        ImGui::CloseCurrentPopup();
        if (isPlaying()) togglePlay();
        if (saveScene()) performSceneChange();
        else pendingChange_ = SceneChange::none;
    }
    ImGui::SameLine();
    if (ImGui::Button("No guardar")) {
        ImGui::CloseCurrentPopup();
        performSceneChange();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        pendingChange_ = SceneChange::none;
    }
    ImGui::EndPopup();
}

void EditorView::drawShortcutsPopup() {
    static const char* title = "Atajos de teclado";
    if (openShortcutsPopup_) {
        ImGui::OpenPopup(title);
        openShortcutsPopup_ = false;
    }
    bool open = true;
    if (!ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_AlwaysAutoResize)) return;
    static const char* rows[][2] = {
        {"Q / W / E / R / B / C", "Mano, mover, rotar, escalar, pintar, construir"},
        {"Construir: arrastre", "Trazo de piezas (Shift: rectangulo, Ctrl: borrar)"},
        {"Construir: Espacio", "Girar la pieza 90 grados"},
        {"Pintar: Shift / Ctrl + clic", "Pintar toda la pieza / tomar el color"},
        {"F", "Enfocar la seleccion"},
        {"Clic derecho + arrastrar", "Orbitar la camara (tambien Alt + clic izquierdo)"},
        {"Boton central + arrastrar", "Desplazar la vista"},
        {"Rueda", "Acercar y alejar"},
        {"Ctrl+Z / Ctrl+Y", "Deshacer / rehacer"},
        {"Ctrl+D / Supr", "Duplicar / eliminar"},
        {"Ctrl+N / Ctrl+O / Ctrl+S", "Nueva, abrir, guardar escena"},
        {"Ctrl+P", "Play / detener"},
        {"Ctrl+Shift+P / Ctrl+Alt+P", "Pausa / avanzar un fotograma"},
    };
    if (ImGui::BeginTable("atajos", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row[0]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row[1]);
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Cerrar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// --- Jerarquía ---------------------------------------------------------------------------------

void EditorView::drawHierarchy() {
    if (!showHierarchy_) return;
    enum class Action { none, duplicate, remove };
    Action action = Action::none;
    if (ImGui::Begin("Jerarquia", &showHierarchy_)) {
        if (ImGui::Button("+")) ImGui::OpenPopup("crear_objeto");
        ImGui::SetItemTooltip("Crear objeto");
        if (ImGui::BeginPopup("crear_objeto")) {
            if (ImGui::MenuItem("Caja")) createPrimitive(ge3d::Primitive::box);
            if (ImGui::MenuItem("Rampa")) createPrimitive(ge3d::Primitive::ramp);
            if (ImGui::MenuItem("Terreno")) createHeightmap();
            if (ImGui::MenuItem("Terreno de voxeles")) createTerrain();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##buscar", "Buscar", hierarchyFilter_, sizeof hierarchyFilter_);
        ImGui::Separator();

        if (ImGui::BeginChild("##arbol")) {
            if (boldFont_) ImGui::PushFont(boldFont_);
            const auto rootName = sceneName(scenePath_) + (hasUnsavedChanges() ? "*" : "");
            const bool rootOpen = treeItem("##raiz", rootName.c_str(), Icon::scene, ImGui::GetColorU32(gray(0.85f)),
                                           ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (boldFont_) ImGui::PopFont();
            if (rootOpen) {
                for (const auto& entity : scene_.entities()) {
                    if (!contains(entity.name, hierarchyFilter_)) continue;
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                               ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (selected_ == entity.id) flags |= ImGuiTreeNodeFlags_Selected;
                    treeItem(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entity.id)), displayName(entity),
                             entityIcon(entity), ImGui::GetColorU32(gray(0.75f)), flags);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) selected_ = entity.id;
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusSelection();
                    ImGui::PushID(static_cast<int>(entity.id));
                    if (ImGui::BeginPopupContextItem("menu")) {
                        selected_ = entity.id;
                        if (ImGui::MenuItem("Duplicar", "Ctrl+D")) action = Action::duplicate;
                        if (ImGui::MenuItem("Eliminar", "Supr")) action = Action::remove;
                        ImGui::Separator();
                        if (ImGui::MenuItem("Enfocar", "F")) focusSelection();
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
                selected_ = 0;
            }
            if (ImGui::BeginPopupContextWindow("crear", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Crear caja")) createPrimitive(ge3d::Primitive::box);
                if (ImGui::MenuItem("Crear rampa")) createPrimitive(ge3d::Primitive::ramp);
                if (ImGui::MenuItem("Crear terreno")) createTerrain();
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    // Se aplica después de recorrer la lista para no modificarla mientras se itera.
    if (action == Action::duplicate) duplicateSelection();
    if (action == Action::remove) deleteSelection();
}

// --- Inspector ---------------------------------------------------------------------------------

void EditorView::drawInspector() {
    if (!showInspector_) return;
    if (ImGui::Begin("Inspector", &showInspector_)) {
        auto* entity = scene_.find(selected_);
        if (!entity) {
            commitModification(inspectorEdit_, "Editar transformacion");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Selecciona un objeto en la Jerarquia o en la Escena");
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }
        ImGui::PushID(static_cast<int>(entity->id));
        const ge3d::Entity frameStart = *entity;
        // Cabecera: icono y nombre.
        const float iconSize = ImGui::GetFrameHeight();
        ImGui::Dummy({iconSize, iconSize});
        drawIcon(ImGui::GetWindowDrawList(), entityIcon(*entity),
                 {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                  (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f},
                 iconSize * 0.4f, ImGui::GetColorU32(gray(0.85f)));
        ImGui::SameLine();
        if (!nameEditing_) copyText(nameBuffer_, sizeof nameBuffer_, entity->name);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##nombre", nameBuffer_, sizeof nameBuffer_);
        nameEditing_ = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit() && entity->name != nameBuffer_) {
            std::optional<ge3d::Entity> before = frameStart;
            entity->name = nameBuffer_;
            commitModification(before, "Renombrar");
        }
        ImGui::Spacing();

        // Componente Transformación (siempre presente).
        if (componentHeader("Transformacion", boldFont_)) {
            bool activated = false;
            bool deactivated = false;
            vectorRow("Posicion", entity->transform.position, 0.05f, 0, 0, activated, deactivated);
            vectorRow("Rotacion", entity->transform.rotationDegrees, 0.5f, 0, 0, activated, deactivated);
            vectorRow("Escala", entity->transform.scale, 0.03f, 0.01f, 250.0f, activated, deactivated);
            // Un arrastre completo en el Inspector es una sola operación de deshacer.
            if (activated && !inspectorEdit_) inspectorEdit_ = frameStart;
            if (deactivated) commitModification(inspectorEdit_, "Editar transformacion");
            ImGui::Spacing();
        }

        // Los componentes se quitan con clic derecho en su cabecera.
        enum class Removal { none, shape, terrain, heightmap };
        Removal removal = Removal::none;
        const auto removeMenu = [&](const char* id, Removal kind) {
            if (ImGui::BeginPopupContextItem(id)) {
                if (ImGui::MenuItem("Quitar componente")) removal = kind;
                ImGui::EndPopup();
            }
        };

        // Componente Forma: primitiva y un material por cara.
        if (entity->shape) {
            const bool open = componentHeader("Forma", boldFont_);
            removeMenu("forma_menu", Removal::shape);
            if (open) {
                auto& shape = *entity->shape;
                const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.34f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Primitiva");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);
                int primitive = shape.primitive == ge3d::Primitive::box ? 0 : 1;
                if (ImGui::Combo("##primitiva", &primitive, "Caja\0Rampa\0")) {
                    std::optional<ge3d::Entity> before = frameStart;
                    shape.primitive = primitive == 0 ? ge3d::Primitive::box : ge3d::Primitive::ramp;
                    commitModification(before, "Cambiar forma");
                }
                ImGui::SeparatorText("Materiales");
                const float swatch = ImGui::GetFrameHeight() - 2;
                const auto faceRow = [&](const char* label, int face) {
                    ImGui::PushID(face);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    ImGui::SameLine(labelWidth);
                    const auto shown = shape.materials[face < 0 ? 0 : static_cast<std::size_t>(face)];
                    if (materialSwatch("##muestra", shown, false, swatch)) ImGui::OpenPopup("materiales");
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", materialName(shown));
                    if (ImGui::BeginPopup("materiales")) {
                        auto chosen = shown;
                        if (materialPicker(chosen, swatch * 1.4f, 6)) {
                            std::optional<ge3d::Entity> before = frameStart;
                            if (face < 0) shape.materials.fill(chosen);
                            else shape.materials[static_cast<std::size_t>(face)] = chosen;
                            commitModification(before, "Cambiar material");
                            paintMaterial_ = chosen;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                };
                faceRow("Toda la pieza", -1);
                ImGui::Separator();
                for (std::uint32_t face = 0; face < ge3d::faceCount(shape.primitive); ++face) {
                    faceRow(ge3d::faceName(shape.primitive, face), static_cast<int>(face));
                }
                ImGui::Spacing();
            }
        }

        // Componente Terreno (malla): capas, generador y tamaño (los pinceles están en la herramienta T).
        if (entity->heightmap) {
            const bool open = componentHeader("Terreno", boldFont_);
            removeMenu("malla_menu", Removal::heightmap);
            if (open) drawHeightmapComponent(*entity, frameStart);
            ImGui::Spacing();
        }

        // Componente Terreno de vóxeles: generador procedural y tamaño.
        if (entity->terrain) {
            const bool open = componentHeader("Terreno de voxeles", boldFont_);
            removeMenu("terreno_menu", Removal::terrain);
            if (open) drawTerrainComponent(*entity, frameStart);
            ImGui::Spacing();
        }

        if (removal != Removal::none) {
            std::optional<ge3d::Entity> before = frameStart;
            if (removal == Removal::shape) entity->shape.reset();
            if (removal == Removal::terrain) entity->terrain.reset();
            if (removal == Removal::heightmap) entity->heightmap.reset();
            commitModification(before, "Quitar componente");
        }

        ImGui::Separator();
        ImGui::Spacing();
        const float buttonWidth = std::min(ImGui::GetContentRegionAvail().x, 220 * scale_);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);
        if (ImGui::Button("Agregar componente", {buttonWidth, 0})) ImGui::OpenPopup("agregar_componente");
        if (ImGui::BeginPopup("agregar_componente")) {
            std::optional<ge3d::Entity> before = frameStart;
            bool added = false;
            if (ImGui::MenuItem("Forma: caja", nullptr, false, !entity->shape)) {
                entity->shape = ge3d::makeShape(ge3d::Primitive::box, paintMaterial_);
                added = true;
            }
            if (ImGui::MenuItem("Forma: rampa", nullptr, false, !entity->shape)) {
                entity->shape = ge3d::makeShape(ge3d::Primitive::ramp, paintMaterial_);
                added = true;
            }
            if (ImGui::MenuItem("Terreno", nullptr, false, !entity->heightmap)) {
                entity->heightmap = ge3d::makeHeightmap(newHeightmap_);
                added = true;
            }
            if (ImGui::MenuItem("Terreno de voxeles", nullptr, false, !entity->terrain)) {
                entity->terrain = ge3d::makeTerrain(newTerrain_);
                added = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("Script (etapa 3)", nullptr, false, false);
            ImGui::MenuItem("Cuerpo fisico (etapa 4)", nullptr, false, false);
            ImGui::MenuItem("Fuente de audio (etapa 4)", nullptr, false, false);
            if (added) commitModification(before, "Agregar componente");
            ImGui::EndPopup();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("UUID");
        ImGui::TextDisabled("%s", entity->uuid.toString().c_str());
        ImGui::PopID();
    }
    ImGui::End();
}

// --- Vista de escena ---------------------------------------------------------------------------

void EditorView::drawStylePopup() {
    if (!ImGui::BeginPopup("estilo_pixel")) return;
    ImGui::SeparatorText("Pixel art");
    static constexpr int sizes[] = {1, 2, 3, 4, 5, 6, 8};
    static constexpr const char* sizeNames[] = {"1 (sin pixelar)", "2", "3", "4", "5", "6", "8"};
    int current = 2;
    for (int i = 0; i < 7; ++i) {
        if (sizes[i] == style_.pixelSize) current = i;
    }
    ImGui::SetNextItemWidth(150 * scale_);
    if (ImGui::Combo("Tamano del pixel", &current, sizeNames, 7)) style_.pixelSize = sizes[current];
    ImGui::SetItemTooltip("Pixeles de pantalla por pixel de la escena. La resolucion interna se adapta a la vista.");
    ImGui::Checkbox("Camara estable", &style_.snapCamera);
    ImGui::SetItemTooltip("La camara avanza de pixel en pixel y la imagen se desplaza suavemente:\n"
                          "los bordes no parpadean al moverse");

    ImGui::SeparatorText("Contornos");
    ImGui::Checkbox("Contornos y aristas", &style_.outlines);
    ImGui::BeginDisabled(!style_.outlines);
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderFloat("Silueta", &style_.outlineDarken, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Oscurece el borde exterior de cada objeto");
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderFloat("Aristas", &style_.edgeLighten, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Aclara las esquinas y crestas (cambios bruscos de orientacion)");
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderFloat("Sensibilidad silueta", &style_.outlineThreshold, 0.02f, 0.5f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderFloat("Sensibilidad aristas", &style_.edgeThreshold, 0.05f, 1.5f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Luz y color");
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderInt("Bandas de luz", &style_.lightBands, 0, 6, style_.lightBands ? "%d" : "continua");
    ImGui::SetItemTooltip("Escalones de luz del sol, como el sombreado del pixel art dibujado a mano");
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderInt("Colores por canal", &style_.colorLevels, 0, 32, style_.colorLevels ? "%d" : "todos");
    ImGui::SetItemTooltip("Reduce la paleta con tramado ordenado (aspecto de consola antigua)");
    ImGui::BeginDisabled(style_.colorLevels == 0);
    ImGui::SetNextItemWidth(150 * scale_);
    ImGui::SliderFloat("Tramado", &style_.colorDither, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (ImGui::Button("Valores por defecto")) style_ = RenderStyle{};
    ImGui::EndPopup();
}

void EditorView::drawViewport(ImTextureID sceneTexture) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool visible = ImGui::Begin("Escena", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (visible) {
        // Barra propia de la vista.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, gray(0.19f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6 * scale_, 3 * scale_});
        if (ImGui::BeginChild("##barra_escena", {0, ImGui::GetFrameHeight() + 6 * scale_},
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            if (toggleButton("Cuadricula", showGrid_, "Mostrar u ocultar la cuadricula del suelo")) showGrid_ = !showGrid_;
            ImGui::SameLine();
            if (ImGui::Button("Enfocar")) focusSelection();
            ImGui::SetItemTooltip("Encuadrar la seleccion (F)");
            if (tool_ == Tool::build) {
                // Opciones del pincel de construcción.
                ImGui::SameLine(0, 16 * scale_);
                if (toggleButton("Bloque", buildShape_ == ge3d::Primitive::box)) buildShape_ = ge3d::Primitive::box;
                ImGui::SameLine(0, 2);
                if (toggleButton("Rampa", buildShape_ == ge3d::Primitive::ramp)) buildShape_ = ge3d::Primitive::ramp;
                ImGui::SameLine();
                char rotation[32];
                std::snprintf(rotation, sizeof rotation, "Girar %d##girar", buildRotation_ * 90);
                if (ImGui::Button(rotation)) buildRotation_ = (buildRotation_ + 1) % 4;
                ImGui::SetItemTooltip("Girar la pieza 90 grados (Espacio)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70 * scale_);
                static const float cells[] = {0.5f, 1.0f, 2.0f};
                static const char* cellNames[] = {"0,5 m", "1 m", "2 m"};
                int current = buildCell_ < 0.75f ? 0 : buildCell_ < 1.5f ? 1 : 2;
                if (ImGui::Combo("##celda", &current, cellNames, 3)) buildCell_ = cells[current];
                ImGui::SetItemTooltip("Tamano de la celda");
            }
            if (tool_ == Tool::terrain) {
                // Pinceles del terreno.
                ImGui::SameLine(0, 16 * scale_);
                static constexpr const char* brushes[] = {"Elevar", "Bajar", "Pintar", "Aplanar", "Suavizar", "Agua"};
                static constexpr const char* brushTips[] = {
                    "Sube el terreno (en voxeles, anade del material elegido)", "Baja el terreno (en voxeles, excava)",
                    "Pinta la capa elegida (en voxeles, el material)", "Nivela a la altura donde empieza el trazo",
                    "Suaviza el relieve (solo terreno de malla)",
                    "Echa agua, que corre cuesta abajo y llena los hoyos (Shift: quitar agua). Solo terreno de malla"};
                for (int i = 0; i < 6; ++i) {
                    if (i) ImGui::SameLine(0, 2);
                    if (toggleButton(brushes[i], static_cast<int>(terrainBrush_) == i, brushTips[i])) {
                        terrainBrush_ = static_cast<TerrainBrush>(i);
                    }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80 * scale_);
                ImGui::SliderInt("##radio", &terrainRadius_, 1, 32, "radio %d");
                ImGui::SetItemTooltip("Radio del pincel en celdas o voxeles ([ y ])");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80 * scale_);
                ImGui::SliderFloat("##fuerza", &brushStrength_, 0.05f, 1.0f, "fuerza %.2f");
                ImGui::SetItemTooltip("Velocidad del pincel en el terreno de malla");
            }
            const auto* selectedEntity = scene_.find(selected_);
            const bool layerStrip = tool_ == Tool::terrain && selectedEntity && selectedEntity->heightmap;
            if (layerStrip) {
                // Terreno de malla: se pinta una de sus 4 capas.
                ImGui::SameLine(0, 16 * scale_);
                const auto& layers = selectedEntity->heightmap->settings.layers;
                for (int layer = 0; layer < ge3d::Heightmap::layerCount; ++layer) {
                    ImGui::PushID(layer);
                    if (layer) ImGui::SameLine(0, 2);
                    if (materialSwatch("##capa", layers[static_cast<std::size_t>(layer)], paintLayer_ == layer, ImGui::GetFrameHeight() - 2)) {
                        paintLayer_ = layer;
                        terrainBrush_ = TerrainBrush::paint;
                    }
                    ImGui::SetItemTooltip("Capa %d: %s", layer + 1, materialName(layers[static_cast<std::size_t>(layer)]));
                    ImGui::PopID();
                }
            }
            if (!layerStrip && (tool_ == Tool::paint || tool_ == Tool::build || tool_ == Tool::terrain)) {
                // El material activo queda a mano en la barra de la vista.
                ImGui::SameLine(0, 16 * scale_);
                materialStrip();
            }
            // El texto informativo solo se muestra si cabe a la derecha sin tapar los controles.
            ImGui::SameLine(0, 16 * scale_);
            if (ImGui::Button("Estilo")) ImGui::OpenPopup("estilo_pixel");
            ImGui::SetItemTooltip("Tamano del pixel, contornos, luz por bandas y colores");
            drawStylePopup();
            char info[64];
            std::snprintf(info, sizeof info, "%u x %u  |  pixel %d", sceneSize_.x, sceneSize_.y, style_.pixelSize);
            ImGui::SameLine();
            const float infoWidth = ImGui::CalcTextSize(info).x;
            const float infoX = ImGui::GetWindowWidth() - infoWidth - 10 * scale_;
            if (infoX > ImGui::GetCursorPosX() + 10) {
                ImGui::SetCursorPosX(infoX);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", info);
            } else {
                ImGui::NewLine();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        const auto available = ImGui::GetContentRegionAvail();
        const auto position = ImGui::GetCursorScreenPos();
        const auto& io = ImGui::GetIO();
        if (available.x > 1 && available.y > 1) {
            // Resolución interna: la vista dividida por el tamaño del píxel. Cada píxel de la escena ocupa
            // exactamente pixelSize x pixelSize píxeles de pantalla (todos iguales, sin escalado irregular).
            const auto dpi = io.DisplayFramebufferScale;
            const int pixel = std::max(1, style_.pixelSize);
            const glm::uvec2 wanted{static_cast<unsigned>(std::max(16.0f, std::floor(available.x * dpi.x / static_cast<float>(pixel)))),
                                    static_cast<unsigned>(std::max(16.0f, std::floor(available.y * dpi.y / static_cast<float>(pixel))))};
            requestedExtent_ = wanted + glm::uvec2(2 * sceneMargin);
            sceneSize_ = glm::min(wanted, capacity_ - glm::uvec2(2 * sceneMargin));
            aspect_ = static_cast<float>(sceneSize_.x) / static_cast<float>(sceneSize_.y);
            const ImVec2 size{static_cast<float>(sceneSize_.x * static_cast<unsigned>(pixel)) / dpi.x,
                              static_cast<float>(sceneSize_.y * static_cast<unsigned>(pixel)) / dpi.y};
            const ImVec2 origin{
                std::floor((position.x + (available.x - size.x) * 0.5f) * dpi.x) / dpi.x,
                std::floor((position.y + (available.y - size.y) * 0.5f) * dpi.y) / dpi.y
            };
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end{origin.x + size.x, origin.y + size.y};
            // La imagen se añade al final (canal 0, debajo de todo) con la cámara ya actualizada por la
            // entrada de este fotograma: así su desplazamiento de subpíxel coincide con lo que se dibuja.
            draw->ChannelsSplit(2);
            draw->ChannelsSetCurrent(1);
            if (isPlaying()) draw->AddRect(origin, end, ImGui::GetColorU32(accentHover), 0, 0, 2 * scale_);

            const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);
            const bool altOrbit = io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            orbiting_ = (orbiting_ || (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                                   (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))) &&
                        (ImGui::IsMouseDown(ImGuiMouseButton_Right) || altOrbit);
            const bool handDrag = tool_ == Tool::hand && !io.KeyAlt;
            panning_ = (panning_ || (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                                                 (handDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))) &&
                       (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (handDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
            if (orbiting_) {
                yaw_ -= io.MouseDelta.x * 0.008f;
                pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.008f, -1.48f, 1.48f);
            }
            if (panning_) {
                const auto inverseView = glm::inverse(view());
                pivot_ += (-glm::vec3(inverseView[0]) * io.MouseDelta.x +
                            glm::vec3(inverseView[1]) * io.MouseDelta.y) * distance_ * 0.0018f;
            }
            if (hovered) distance_ = std::clamp(distance_ * std::pow(0.87f, io.MouseWheel), 0.5f, maxDistance);

            draw->PushClipRect(origin, end, true);
            ImGuizmo::SetDrawlist(draw);
            ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
            auto viewMatrix = view();
            const auto projectionMatrix = projection();
            bool overGizmo = false;
            auto* entity = scene_.find(selected_);
            const bool gizmoTool = tool_ == Tool::move || tool_ == Tool::rotate || tool_ == Tool::scale;
            if (entity && gizmoTool && !orbiting_) {
                const ImGuizmo::OPERATION operation = tool_ == Tool::move ? ImGuizmo::TRANSLATE
                                                    : tool_ == Tool::rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
                const float step = tool_ == Tool::rotate ? 15.0f : 0.25f;
                const float snapValues[] = {step, step, step};
                const ge3d::Entity beforeManipulate = *entity;
                auto model = modelMatrix(entity->transform);
                if (ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projectionMatrix),
                        operation,
                        localAxes_ || tool_ == Tool::scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                        glm::value_ptr(model), nullptr, snap_ ? snapValues : nullptr)) {
                    float translation[3], rotation[3], scaling[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scaling);
                    entity->transform = {
                        {translation[0], translation[1], translation[2]},
                        {rotation[0], rotation[1], rotation[2]},
                        {std::max(0.01f, scaling[0]), std::max(0.01f, scaling[1]), std::max(0.01f, scaling[2])}
                    };
                }
                // El arrastre se previsualiza en vivo y se guarda como una sola operación al soltar.
                if (ImGuizmo::IsUsing() && !gizmoEdit_) gizmoEdit_ = beforeManipulate;
                overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
            } else {
                // Sin gizmo visible: una llamada sin operaciones prepara la cámara del cubo de orientación.
                glm::mat4 identity{1.0f};
                ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projectionMatrix),
                                     static_cast<ImGuizmo::OPERATION>(0), ImGuizmo::WORLD, glm::value_ptr(identity));
            }

            // Cubo de orientación en la esquina superior derecha: clic en una cara para alinear la vista.
            const float cube = std::min(90.0f * scale_, size.y * 0.42f);
            const glm::mat4 previousView = viewMatrix;
            ImGuizmo::ViewManipulate(glm::value_ptr(viewMatrix), distance_, {end.x - cube, origin.y}, {cube, cube}, 0x00000000);
            if (viewMatrix != previousView) setViewFromMatrix(viewMatrix);
            const bool overCube = ImGuizmo::IsViewManipulateHovered();

            const bool canUseMouse = hovered && !overGizmo && !overCube && !io.KeyAlt && !orbiting_ && !panning_;
            if (tool_ == Tool::paint) {
                // Pintar: vista previa de la cara bajo el cursor, trazo con arrastre, cuentagotas con Ctrl.
                const auto hit = canUseMouse ? raycast(io.MousePos, origin, size) : std::nullopt;
                if (hit && !io.KeyCtrl && !hit->terrain) {
                    if (const auto* target = scene_.find(hit->entity); target && target->shape) {
                        drawPieceOutline(draw, target->shape->primitive, modelMatrix(target->transform), origin, size,
                                         IM_COL32(255, 255, 255, 220), previewFill,
                                         io.KeyShift ? -1 : static_cast<int>(hit->face));
                    }
                }
                if (hit && io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // Cuentagotas: toma el material de una cara de pieza o de un vóxel del terreno.
                    if (const auto* target = scene_.find(hit->entity)) {
                        if (hit->terrain && target->terrain && target->terrain->voxels) {
                            const auto& grid = *target->terrain->voxels;
                            const auto voxel = grid.get(hit->voxel.x, hit->voxel.y, hit->voxel.z);
                            if (voxel > 0) paintMaterial_ = grid.materials()[voxel - 1u];
                        } else if (target->shape) {
                            paintMaterial_ = target->shape->materials[hit->face];
                        }
                        log(std::string("Material tomado: ") + materialName(paintMaterial_));
                    }
                } else if (canUseMouse && !io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                           (painting_ || ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
                    if (!painting_) ImGui::SetWindowFocus();
                    painting_ = true;
                    if (hit) paintAt(*hit, io.KeyShift);
                }
            }
            if (tool_ == Tool::build) handleBuildTool(draw, origin, size, canUseMouse);
            if (tool_ == Tool::terrain) handleTerrainTool(draw, origin, size, canUseMouse);
            draw->PopClipRect();

            draw->ChannelsSetCurrent(0);
            draw->AddRectFilled(position, {position.x + available.x, position.y + available.y}, IM_COL32(28, 28, 28, 255));
            const auto snap = cameraSnap();
            const glm::vec2 capacity{std::max(1u, capacity_.x), std::max(1u, capacity_.y)};
            const glm::vec2 uv0 = (glm::vec2(static_cast<float>(sceneMargin)) + snap.subpixel) / capacity;
            const glm::vec2 uv1 = uv0 + glm::vec2(sceneSize_) / capacity;
            draw->AddImage(sceneTexture, origin, end, {uv0.x, uv0.y}, {uv1.x, uv1.y});
            draw->ChannelsMerge();

            const bool selectTool = tool_ == Tool::move || tool_ == Tool::rotate || tool_ == Tool::scale;
            if (canUseMouse && selectTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                pick(io.MousePos, origin, size);
            }
            ImGui::Dummy(available);
        }
    }
    ImGui::End();
    if (painting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishPaintStroke();
    if (building_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishBuildStroke();
    if (erasing_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishEraseStroke();
    if (sculpting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishSculpt();
    if (gizmoEdit_ && (!ImGuizmo::IsUsing() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        const char* label = tool_ == Tool::rotate ? "Rotar" : tool_ == Tool::scale ? "Escalar" : "Mover";
        commitModification(gizmoEdit_, label);
    }
}

// --- Proyecto ----------------------------------------------------------------------------------

void EditorView::drawProjectNode(const ProjectNode& node, bool root) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (root) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!node.directory) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool scene = !node.directory && isSceneFile(node.path);
    std::error_code error;
    if (scene && std::filesystem::equivalent(node.path, scenePath_, error)) flags |= ImGuiTreeNodeFlags_Selected;
    const Icon icon = node.directory ? Icon::folder : scene ? Icon::scene : Icon::file;
    const ImU32 color = ImGui::GetColorU32(node.directory ? ImVec4{0.85f, 0.72f, 0.38f, 1} : scene ? accentHover : gray(0.7f));
    const bool open = treeItem(&node, node.name.c_str(), icon, color, flags);
    if (scene) {
        ImGui::SetItemTooltip("Doble clic para abrir\n%s", ge3d::pathToUtf8(node.path).c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            requestSceneChange(SceneChange::open, node.path);
        }
    }
    if (node.directory && open) {
        for (const auto& child : node.children) drawProjectNode(child, false);
        ImGui::TreePop();
    }
}

void EditorView::drawProject() {
    if (!showProject_) return;
    if (ImGui::Begin("Proyecto", &showProject_)) {
        if (ImGui::Button("Actualizar")) refreshProject();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ge3d::pathToUtf8(contentRoot_).c_str());
        ImGui::Separator();
        if (!projectFound_) {
            ImGui::TextDisabled("No existe la carpeta de contenido.");
            if (ImGui::Button("Crear carpeta")) {
                std::error_code error;
                std::filesystem::create_directories(contentRoot_ / "scenes", error);
                if (error) log("No se pudo crear la carpeta: " + error.message(), true);
                refreshProject();
            }
        } else if (ImGui::BeginChild("##archivos")) {
            drawProjectNode(project_, true);
        }
        if (projectFound_) ImGui::EndChild();
    }
    ImGui::End();
}

// --- Consola -----------------------------------------------------------------------------------

void EditorView::drawConsole() {
    if (!showConsole_) return;
    if (ImGui::Begin("Consola", &showConsole_)) {
        auto& log = editorLog();
        if (ImGui::Button("Limpiar")) log.clear();
        ImGui::SameLine();
        const auto entries = log.entries();
        if (ImGui::Button("Copiar")) {
            std::string text;
            for (const auto& entry : entries) text += "[" + entry.time + "] " + entry.message + "\n";
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(220 * scale_, ImGui::GetContentRegionAvail().x * 0.4f));
        ImGui::InputTextWithHint("##filtro", "Filtrar", consoleFilter_, sizeof consoleFilter_);

        const auto counts = log.counts();
        char label[48];
        const float right = 3 * 90 * scale_;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - right));
        std::snprintf(label, sizeof label, "Info %d", counts[0]);
        if (toggleButton(label, showInfo_)) showInfo_ = !showInfo_;
        ImGui::SameLine();
        std::snprintf(label, sizeof label, "Avisos %d", counts[1]);
        if (toggleButton(label, showWarnings_)) showWarnings_ = !showWarnings_;
        ImGui::SameLine();
        std::snprintf(label, sizeof label, "Errores %d", counts[2]);
        if (toggleButton(label, showErrors_)) showErrors_ = !showErrors_;
        ImGui::Separator();

        if (ImGui::BeginChild("##mensajes", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4;
            ImGui::PushTextWrapPos(0);
            for (const auto& entry : entries) {
                const bool visible = (entry.level == LogLevel::info && showInfo_) ||
                                     (entry.level == LogLevel::warning && showWarnings_) ||
                                     (entry.level == LogLevel::error && showErrors_);
                if (!visible || !contains(entry.message, consoleFilter_)) continue;
                const ImVec4 color = entry.level == LogLevel::error ? errorColor
                                   : entry.level == LogLevel::warning ? warningColor : gray(0.82f);
                ImGui::TextDisabled("[%s]", entry.time.c_str());
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                if (entry.repeats > 1) {
                    ImGui::Text("%s  (x%d)", entry.message.c_str(), entry.repeats);
                } else {
                    ImGui::TextUnformatted(entry.message.c_str());
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            const auto revision = log.revision();
            if (revision != consoleRevision_ && atBottom) ImGui::SetScrollHereY(1.0f);
            consoleRevision_ = revision;
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// --- Fotograma ---------------------------------------------------------------------------------

void EditorView::buildDefaultLayout(ImGuiID dock) {
    // Inspector a la derecha a toda altura; Proyecto y Consola abajo; Jerarquía a la izquierda; Escena al centro.
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock;
    const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
    const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Proyecto", bottom);
    ImGui::DockBuilderDockWindow("Consola", bottom);
    ImGui::DockBuilderDockWindow("Jerarquia", left);
    ImGui::DockBuilderDockWindow("Escena", center);
    ImGui::DockBuilderFinish(dock);
}

void EditorView::draw(ImTextureID sceneTexture) {
    if (isPlaying() && !paused_) {
        ++playFrames_;
        simulateWater(std::min(ImGui::GetIO().DeltaTime, 1.0f / 20.0f));
    }

    // En modo Play todo el editor se tiñe para que no se confunda con la edición normal.
    const int tinted = isPlaying() ? 3 : 0;
    if (tinted) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.20f, 0.22f, 0.27f, 1});
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{0.15f, 0.17f, 0.22f, 1});
        ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4{0.15f, 0.17f, 0.22f, 1});
    }

    drawMenuBar();
    drawToolbar();
    drawStatusBar();
    handleShortcuts();

    const auto dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (!layoutCreated_) {
        layoutCreated_ = true;
        buildDefaultLayout(dock);
    }

    drawHierarchy();
    drawViewport(sceneTexture);
    drawInspector();
    drawProject();
    drawConsole();

    drawPathPopup();
    drawUnsavedPopup();
    drawShortcutsPopup();

    if (tinted) ImGui::PopStyleColor(tinted);
}
