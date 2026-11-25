#include "EditorView.hpp"

#include "EditorLog.hpp"
#include "ge3d/Primitives.hpp"
#include "ge3d/SceneIO.hpp"
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include "EditorSystem.hpp"
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

ImFont* loadFonts(float scale, ImFont*& bold, ImFont*& code) {
    ImGuiIO& io = ImGui::GetIO();
    bold = nullptr;
    code = nullptr;
    std::error_code error;
    const char* regular = "C:/Windows/Fonts/segoeui.ttf";
    const char* semibold = "C:/Windows/Fonts/seguisb.ttf";
    if (!std::filesystem::exists(regular, error)) return nullptr;
    ImFont* font = io.Fonts->AddFontFromFileTTF(regular, std::round(15.0f * scale));
    if (std::filesystem::exists(semibold, error)) bold = io.Fonts->AddFontFromFileTTF(semibold, std::round(15.0f * scale));
    const char* mono = "C:/Windows/Fonts/consola.ttf";
    if (std::filesystem::exists(mono, error)) code = io.Fonts->AddFontFromFileTTF(mono, std::round(14.0f * scale));
    return font;
}

// --- Iconos dibujados con primitivas (la fuente no trae iconos) -------------------------------
enum class Icon { hand, move, rotate, scale, paint, build, terrain, play, pause, step, cube, ramp, folder, file, scene, empty, script, sound };

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
    case Icon::script: {
        // Llaves de código.
        draw->AddBezierCubic(p(-0.25f, -0.85f), p(-0.6f, -0.85f), p(-0.35f, 0.0f), p(-0.75f, 0.0f), color, t);
        draw->AddBezierCubic(p(-0.75f, 0.0f), p(-0.35f, 0.0f), p(-0.6f, 0.85f), p(-0.25f, 0.85f), color, t);
        draw->AddBezierCubic(p(0.25f, -0.85f), p(0.6f, -0.85f), p(0.35f, 0.0f), p(0.75f, 0.0f), color, t);
        draw->AddBezierCubic(p(0.75f, 0.0f), p(0.35f, 0.0f), p(0.6f, 0.85f), p(0.25f, 0.85f), color, t);
        break;
    }
    case Icon::sound: {
        // Altavoz con ondas.
        const ImVec2 body[] = {p(-0.85f, -0.3f), p(-0.45f, -0.3f), p(0.05f, -0.75f), p(0.05f, 0.75f), p(-0.45f, 0.3f), p(-0.85f, 0.3f)};
        draw->AddConvexPolyFilled(body, 6, color);
        draw->PathArcTo(p(0.05f, 0.0f), r * 0.45f, -0.9f, 0.9f, 10);
        draw->PathStroke(color, 0, t);
        draw->PathArcTo(p(0.05f, 0.0f), r * 0.8f, -0.9f, 0.9f, 12);
        draw->PathStroke(color, 0, t);
        break;
    }
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

}

// =============================================================================================

EditorView::EditorView(float uiScale, std::filesystem::path initialScene) : scale_(std::clamp(uiScale, 0.75f, 3.0f)) {
    applyTheme(scale_);
    loadFonts(scale_, boldFont_, codeFont_);

    contentRoot_ = ge3d::pathFromUtf8(GE3D_CONTENT_DIR);
    projectFolder_ = contentRoot_;
    loadSettings();
    // Escena inicial: la indicada al abrir el editor, si no la última que se usó, si no la de ejemplo.
    std::error_code lastError;
    if (!initialScene.empty()) scenePath_ = std::move(initialScene);
    else if (!lastScene_.empty() && std::filesystem::exists(lastScene_, lastError)) scenePath_ = lastScene_;
    else scenePath_ = ge3d::pathFromUtf8(GE3D_DEFAULT_SCENE);
    // Scripts: se leen de Content y escriben en la Consola. La VM solo existe durante Play.
    scripts_ = std::make_unique<ge3d::ScriptHost>(
        [this](const std::string& path) { return readScript(path); },
        [](bool error, const std::string& message) {
            editorLog().add(error ? LogLevel::error : LogLevel::info, message);
        });
    watchScripts(true);
    std::error_code error;
    if (std::filesystem::exists(scenePath_, error)) {
        loadScene();
    } else {
        log("Escena de ejemplo sin guardar. Pulsa Ctrl+S para guardarla en " + ge3d::pathToUtf8(scenePath_));
    }
    refreshProject();
}

EditorView::~EditorView() {
    if (scripts_ && scripts_->running()) scripts_->stop(scene_);
    if (isPlaying()) scene_ = *playSnapshot_;   // la cámara y las preferencias son las de edición
    saveSettings();
    // La distribución se guarda ahora: después iniPath_ deja de existir.
    if (!iniPath_.empty()) ImGui::SaveIniSettingsToDisk(iniPath_.c_str());
    ImGui::GetIO().IniFilename = nullptr;
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

glm::mat4 EditorView::worldMatrix(const ge3d::Entity& entity) const {
    return modelMatrix(ge3d::worldTransform(scene_, entity));
}

glm::mat4 EditorView::terrainMatrix(const ge3d::Entity& entity) const {
    const float voxel = entity.terrain ? entity.terrain->settings.voxelSize : 1.0f;
    return worldMatrix(entity) * glm::scale(glm::mat4{1.0f}, glm::vec3{voxel});
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
    const glm::mat4 model = worldMatrix(entity);
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
    const glm::vec3 local = glm::vec3(glm::inverse(worldMatrix(*entity)) * glm::vec4(hit.point, 1.0f));
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
        const glm::mat4 model = worldMatrix(*entity);
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
        flattenLevel_ = glm::vec3(glm::inverse(worldMatrix(*entity)) * glm::vec4(hit->point, 1.0f)).y;
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
    if (firstPerson_) return glm::lookAtRH(fpEye_, fpEye_ + fpForward(), {0, 1, 0});
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
    if (!style_.snapCamera || style_.pixelSize <= 1 || firstPerson_) return {};
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
    if (firstPerson_) return fpEye_;
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
        const auto model = worldMatrix(entity);
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
    const auto& io = ImGui::GetIO();
    const auto hit = raycast(mouse, origin, size);
    const SelectMode mode = io.KeyCtrl ? SelectMode::toggle : io.KeyShift ? SelectMode::add : SelectMode::replace;
    if (hit) {
        select(hit->entity, mode);
    } else {
        // Clic en el vacío: empieza un rectángulo de selección (si no se arrastra, deselecciona).
        marquee_ = true;
        marqueeStart_ = mouse;
    }
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
        const auto p = ge3d::worldTransform(scene_, entity).position;
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
                drawPieceOutline(draw, entity->shape->primitive, worldMatrix(*entity), origin, size, red,
                                 IM_COL32(255, 60, 50, 90));
            }
        }
        if (hit && !eraseStroke_.contains(hit->entity)) {
            if (const auto* entity = scene_.find(hit->entity); entity && entity->shape) {
                drawPieceOutline(draw, entity->shape->primitive, worldMatrix(*entity), origin, size, red, 0);
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
        const auto world = ge3d::worldTransform(scene_, *entity);
        pivot_ = vector(world.position);
        const auto scale = world.scale;
        distance_ = std::clamp(std::max({scale.x, scale.y, scale.z}) * 3.2f, 2.0f, maxDistance);
    }
}

// --- Edición -----------------------------------------------------------------------------------

void EditorView::validateSelection() {
    // Lo que ya no existe sale de la selección. Asignar selected_ directamente equivale a seleccionar
    // solo ese objeto (así el código que crea objetos no necesita conocer la selección múltiple).
    std::erase_if(selection_, [&](ge3d::EntityId id) { return !scene_.find(id); });
    if (selected_ && !scene_.find(selected_)) selected_ = 0;
    if (selected_ && !isSelected(selected_)) selection_ = {selected_};
    if (!selected_ && !selection_.empty()) selected_ = selection_.back();
    if (!selected_) selection_.clear();
}

bool EditorView::isSelected(ge3d::EntityId id) const {
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void EditorView::select(ge3d::EntityId id, SelectMode mode) {
    if (!scene_.find(id)) {
        if (mode == SelectMode::replace) clearSelection();
        return;
    }
    switch (mode) {
    case SelectMode::replace:
        selection_ = {id};
        selected_ = id;
        rangeAnchor_ = id;
        break;
    case SelectMode::add:
        if (!isSelected(id)) selection_.push_back(id);
        selected_ = id;
        rangeAnchor_ = id;
        break;
    case SelectMode::toggle:
        if (isSelected(id)) {
            std::erase(selection_, id);
            selected_ = selection_.empty() ? 0 : selection_.back();
        } else {
            selection_.push_back(id);
            selected_ = id;
        }
        rangeAnchor_ = id;
        break;
    case SelectMode::range: {
        // Desde el ancla hasta este objeto, en el orden de la Jerarquía (solo lo que pasa el filtro).
        const auto anchor = scene_.indexOf(rangeAnchor_ ? rangeAnchor_ : id);
        const auto target = scene_.indexOf(id);
        if (!anchor || !target) return select(id, SelectMode::replace);
        selection_.clear();
        for (auto i = std::min(*anchor, *target); i <= std::max(*anchor, *target); ++i) {
            const auto& entity = scene_.entities()[i];
            if (contains(entity.name, hierarchyFilter_)) selection_.push_back(entity.id);
        }
        selected_ = id;
        break;
    }
    }
}

void EditorView::clearSelection() {
    selection_.clear();
    selected_ = 0;
}

void EditorView::selectAll() {
    selection_.clear();
    for (const auto& entity : scene_.entities()) selection_.push_back(entity.id);
    if (!selection_.empty() && !isSelected(selected_)) selected_ = selection_.back();
}

void EditorView::copySelection() {
    if (selection_.empty()) return;
    clipboard_.clear();
    // En el orden de la escena y con sus propios datos de terreno (lo que se edite después no cambia la copia).
    for (const auto& entity : scene_.entities()) {
        if (!isSelected(entity.id)) continue;
        auto copy = entity;
        if (copy.terrain && copy.terrain->voxels) copy.terrain->voxels = std::make_shared<ge3d::VoxelGrid>(*copy.terrain->voxels);
        if (copy.heightmap && copy.heightmap->data) copy.heightmap->data = std::make_shared<ge3d::Heightmap>(*copy.heightmap->data);
        clipboard_.push_back(std::move(copy));
    }
    log(std::to_string(clipboard_.size()) + (clipboard_.size() == 1 ? " objeto copiado" : " objetos copiados"));
}

void EditorView::pasteClipboard() {
    if (clipboard_.empty() || editInProgress()) return;
    std::vector<ge3d::EntityId> created;
    execute(ge3d::makePasteCommand(scene_, clipboard_, &created));
    selection_ = created;
    selected_ = created.empty() ? 0 : created.back();
}

void EditorView::setActive(bool active) {
    std::vector<ge3d::Entity> before;
    for (const auto id : selection_) {
        if (auto* entity = scene_.find(id); entity && entity->active != active) {
            before.push_back(*entity);
            entity->active = active;
        }
    }
    if (auto command = ge3d::makeModifyCommand(scene_, before, active ? "Activar" : "Desactivar")) activeHistory().record(std::move(*command));
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
    std::vector<ge3d::EntityId> created;
    if (auto command = ge3d::makeDuplicateCommand(scene_, selection_, {1, 0, 0}, &created)) {
        execute(std::move(*command));
        selection_ = created;
        selected_ = created.empty() ? 0 : created.back();
    }
}
void EditorView::deleteSelection() {
    if (editInProgress()) return;
    if (auto command = ge3d::makeDeleteCommand(scene_, selection_)) {
        execute(std::move(*command));
        clearSelection();
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
        if (clearOnPlay_) editorLog().clear();
        lastErrorCount_ = editorLog().counts()[2];
        log("Play: los cambios que hagas ahora se descartan al detener");
        // Física, audio y jugador; después los scripts (que ya pueden usar la física y el sonido).
        startPlaySystems();
        // Scripts: VM nueva para esta copia. Haz clic en un objeto con script para interactuar.
        scripts_->start(scene_);
        ImGui::SetWindowFocus("Escena");
    } else {
        // onStop y destrucción de la VM antes de recuperar la escena: nada de Play sobrevive.
        scripts_->stop(scene_);
        stopPlaySystems();
        scene_ = std::move(*playSnapshot_);
        playSnapshot_.reset();
        water_.clear();
        playHistory_.clear();
        paused_ = false;
        gizmoEdit_.clear();
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
        updatePlay(1.0f / 60.0f);
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
        rememberCamera();
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
    gizmoEdit_.clear();
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
        refreshOverrides();   // los overrides de cada instancia se guardan con la escena
        ge3d::saveSceneFile(scene_, scenePath_);
        savedState_ = history_.state();
        if (editingPrefab()) prefabs_.erase(assetPath(scenePath_));
        else addRecentScene(scenePath_);
        saveSettings();
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
        clearSelection();
        gizmoEdit_.clear();
        inspectorEdit_.reset();
        restoreCamera();
        prefabs_.clear();
        syncPrefabs(true);
        if (!editingPrefab()) {
            addRecentScene(scenePath_);
            prefabReturn_.clear();
        }
        saveSettings();
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
    if (!codeFocused_ && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, once)) saveScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, once)) requestSceneChange(SceneChange::newScene);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, once)) pathPopup_ = PathPopup::open;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, once)) duplicateSelection();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, once)) copySelection();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, once)) pasteClipboard();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A, once)) selectAll();
    if (ImGui::Shortcut(ImGuiKey_F2, once) && selected_) {
        renaming_ = selected_;
        startRename_ = true;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, once)) togglePlay();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, once)) togglePause();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, once)) step();
    if (ImGui::Shortcut(ImGuiKey_Delete, once)) deleteSelection();
    if (!orbiting_ && !panning_ && !flying_ && !(isPlaying() && !paused_)) {
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
        if (ImGui::BeginMenu("Abrir reciente", !recentScenes_.empty())) {
            for (const auto& path : recentScenes_) {
                std::error_code error;
                if (ImGui::MenuItem(ge3d::pathToUtf8(path.filename()).c_str(), nullptr, false, std::filesystem::exists(path, error))) {
                    requestSceneChange(SceneChange::open, path);
                }
                ImGui::SetItemTooltip("%s", ge3d::pathToUtf8(path).c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Vaciar lista")) recentScenes_.clear();
            ImGui::EndMenu();
        }
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
        if (ImGui::MenuItem("Copiar", "Ctrl+C", false, hasSelection)) copySelection();
        if (ImGui::MenuItem("Pegar", "Ctrl+V", false, !clipboard_.empty())) pasteClipboard();
        if (ImGui::MenuItem("Seleccionar todo", "Ctrl+A")) selectAll();
        if (ImGui::MenuItem("Renombrar", "F2", false, hasSelection)) {
            renaming_ = selected_;
            startRename_ = true;
        }
        ImGui::Separator();
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
        ImGui::Separator();
        if (ImGui::MenuItem("Jugador (primera persona)")) createSpecial(Special::player);
        if (ImGui::MenuItem("Zona (trigger)")) createSpecial(Special::zone);
        if (ImGui::MenuItem("Fuente de sonido")) createSpecial(Special::sound);
        ImGui::Separator();
        if (ImGui::MenuItem("Objeto vacio")) createEmptyChild(0);
        if (ImGui::MenuItem("Hijo vacio de la seleccion", nullptr, false, scene_.find(selected_) != nullptr)) createEmptyChild(selected_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ventana")) {
        ImGui::MenuItem("Jerarquia", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Proyecto", nullptr, &showProject_);
        ImGui::MenuItem("Consola", nullptr, &showConsole_);
        ImGui::MenuItem("Codigo", nullptr, &showCode_);
        ImGui::Separator();
        if (ImGui::MenuItem("Restablecer distribucion")) {
            showHierarchy_ = showInspector_ = showProject_ = showConsole_ = showCode_ = true;
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

void EditorView::createEmptyChild(ge3d::EntityId parent) {
    auto entity = scene_.makeEntity("Objeto", ge3d::Transform{});
    entity.parent = scene_.find(parent) ? parent : 0;
    if (!entity.parent) entity.transform.position = {std::round(pivot_.x), 0, std::round(pivot_.z)};
    const auto id = entity.id;
    execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
    select(id);
}

void EditorView::drawHierarchy() {
    if (!showHierarchy_) return;
    enum class Action { none, duplicate, remove, copy, paste, activate, deactivate, unparent, emptyChild, unpack };
    Action action = Action::none;
    ge3d::EntityId actionTarget{};
    struct Reparent {
        std::vector<ge3d::EntityId> ids;
        ge3d::EntityId parent{};
        ge3d::EntityId anchor{};
        bool after{};
        bool pending{};
    } reparent;
    std::string droppedAsset;
    ge3d::EntityId droppedOn{};
    const auto createMenu = [&] {
        if (ImGui::MenuItem("Objeto vacio")) createEmptyChild(0);
        if (ImGui::MenuItem("Caja")) createPrimitive(ge3d::Primitive::box);
        if (ImGui::MenuItem("Rampa")) createPrimitive(ge3d::Primitive::ramp);
        ImGui::Separator();
        if (ImGui::MenuItem("Terreno")) createHeightmap();
        if (ImGui::MenuItem("Terreno de voxeles")) createTerrain();
        ImGui::Separator();
        if (ImGui::MenuItem("Jugador")) createSpecial(Special::player);
        if (ImGui::MenuItem("Zona (trigger)")) createSpecial(Special::zone);
        if (ImGui::MenuItem("Fuente de sonido")) createSpecial(Special::sound);
    };
    // Lo que se arrastra: la selección si el objeto arrastrado forma parte de ella.
    const auto draggedIds = [&](ge3d::EntityId dragged) {
        return isSelected(dragged) ? selection_ : std::vector<ge3d::EntityId>{dragged};
    };
    if (ImGui::Begin("Jerarquia", &showHierarchy_)) {
        if (editingPrefab()) {
            // Modo prefab: se edita el archivo del prefab; al guardar se actualizan sus copias.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.16f, 0.24f, 0.34f, 1.0f});
            if (ImGui::BeginChild("##modo_prefab", {0, ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2},
                                  ImGuiChildFlags_AlwaysUseWindowPadding)) {
                std::error_code error;
                if (!prefabReturn_.empty() && std::filesystem::exists(prefabReturn_, error)) {
                    if (ImGui::SmallButton("< Volver")) requestSceneChange(SceneChange::open, prefabReturn_);
                    ImGui::SetItemTooltip("Volver a %s", sceneName(prefabReturn_).c_str());
                    ImGui::SameLine();
                }
                ImGui::TextColored(ImVec4{0.55f, 0.78f, 1.0f, 1.0f}, "Editando prefab");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        if (ImGui::Button("+")) ImGui::OpenPopup("crear_objeto");
        ImGui::SetItemTooltip("Crear objeto");
        if (ImGui::BeginPopup("crear_objeto")) {
            createMenu();
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
            const bool filtering = hierarchyFilter_[0] != '\0';
            std::function<void(ge3d::EntityId)> drawNode = [&](ge3d::EntityId id) {
                const auto* found = scene_.find(id);
                if (!found) return;
                const ge3d::Entity entity = *found;   // copia: el menú puede cambiar la escena
                const auto children = filtering ? std::vector<ge3d::EntityId>{} : ge3d::childrenOf(scene_, id);
                ImGui::PushID(static_cast<int>(entity.id));
                if (renaming_ == entity.id) {
                    // Renombrar en el sitio (F2 o menú): Intro confirma, Esc cancela.
                    if (startRename_) {
                        copyText(renameBuffer_, sizeof renameBuffer_, entity.name);
                        ImGui::SetKeyboardFocusHere();
                        startRename_ = false;
                    }
                    ImGui::SetNextItemWidth(-1);
                    const bool done = ImGui::InputText("##renombrar", renameBuffer_, sizeof renameBuffer_,
                                                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        renaming_ = 0;
                    } else if (done || ImGui::IsItemDeactivated()) {
                        if (auto* target = scene_.find(entity.id); target && target->name != renameBuffer_ && renameBuffer_[0]) {
                            std::optional<ge3d::Entity> before = *target;
                            target->name = renameBuffer_;
                            commitModification(before, "Renombrar");
                        }
                        renaming_ = 0;
                    }
                    ImGui::PopID();
                    return;
                }
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
                if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (isSelected(entity.id)) flags |= ImGuiTreeNodeFlags_Selected;
                // Inactivos atenuados; instancias de prefab en azul, como en Unity.
                const bool active = ge3d::activeInHierarchy(scene_, entity);
                ImVec4 textColor = entity.prefab ? ImVec4{0.50f, 0.74f, 1.0f, 1.0f} : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                if (!active) textColor.w *= 0.45f;
                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                const bool open = treeItem(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entity.id)), displayName(entity),
                                           entity.prefab && ge3d::prefabInstanceRoot(scene_, entity.id) == entity.id ? Icon::cube : entityIcon(entity),
                                           ImGui::GetColorU32(entity.prefab ? ImVec4{0.50f, 0.74f, 1.0f, active ? 1.0f : 0.45f}
                                                                            : gray(active ? 0.75f : 0.45f)),
                                           flags);
                ImGui::PopStyleColor();
                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const ImVec2 itemMax = ImGui::GetItemRectMax();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
                    const auto& io = ImGui::GetIO();
                    select(entity.id, io.KeyCtrl ? SelectMode::toggle : io.KeyShift ? SelectMode::range : SelectMode::replace);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusSelection();
                if (ImGui::BeginDragDropSource()) {
                    const ge3d::EntityId dragged = entity.id;
                    ImGui::SetDragDropPayload("GE3D_ENTITY", &dragged, sizeof dragged);
                    const auto count = draggedIds(entity.id).size();
                    if (count > 1) ImGui::Text("%zu objetos", count);
                    else ImGui::TextUnformatted(entity.name.c_str());
                    ImGui::TextDisabled("Encima: hacer hijo  |  Bordes: colocar  |  Proyecto: crear prefab");
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    // Tercio superior: antes; inferior: después (mismo padre); centro: dentro, como hijo.
                    const float y = ImGui::GetMousePos().y;
                    const float h = itemMax.y - itemMin.y;
                    const int zone = y < itemMin.y + h * 0.25f ? 0 : y > itemMax.y - h * 0.25f ? 2 : 1;
                    if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ENTITY", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                             ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                        auto* draw = ImGui::GetWindowDrawList();
                        const ImU32 color = ImGui::GetColorU32(accentHover);
                        if (zone == 1) draw->AddRect(itemMin, itemMax, color, 3.0f, 0, 2.0f);
                        else draw->AddLine({itemMin.x, zone == 0 ? itemMin.y : itemMax.y}, {itemMax.x, zone == 0 ? itemMin.y : itemMax.y}, color, 2.0f);
                        if (payload->IsDelivery()) {
                            const auto dragged = *static_cast<const ge3d::EntityId*>(payload->Data);
                            reparent = {draggedIds(dragged), zone == 1 ? entity.id : entity.parent, zone == 1 ? 0 : entity.id, zone == 2, true};
                        }
                    }
                    if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ASSET")) {
                        droppedAsset = static_cast<const char*>(payload->Data);
                        droppedOn = entity.id;
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("menu")) {
                    if (!isSelected(entity.id)) select(entity.id);
                    if (ImGui::MenuItem("Copiar", "Ctrl+C")) action = Action::copy;
                    if (ImGui::MenuItem("Pegar", "Ctrl+V", false, !clipboard_.empty())) action = Action::paste;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Renombrar", "F2")) {
                        renaming_ = entity.id;
                        startRename_ = true;
                    }
                    if (ImGui::MenuItem("Duplicar", "Ctrl+D")) action = Action::duplicate;
                    if (ImGui::MenuItem("Eliminar", "Supr")) action = Action::remove;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Crear hijo vacio")) {
                        action = Action::emptyChild;
                        actionTarget = entity.id;
                    }
                    if (ImGui::MenuItem("Sacar del padre", nullptr, false, entity.parent != 0)) action = Action::unparent;
                    if (entity.active ? ImGui::MenuItem("Desactivar") : ImGui::MenuItem("Activar")) {
                        action = entity.active ? Action::deactivate : Action::activate;
                    }
                    if (entity.prefab) {
                        ImGui::Separator();
                        const auto instanceRoot = ge3d::prefabInstanceRoot(scene_, entity.id);
                        if (ImGui::MenuItem("Abrir prefab")) openPrefab(contentRoot_ / ge3d::pathFromUtf8(entity.prefab->asset));
                        if (ImGui::MenuItem("Aplicar cambios al prefab", nullptr, false, !isPlaying())) applyPrefab(instanceRoot);
                        if (ImGui::MenuItem("Revertir al prefab")) revertPrefab(instanceRoot);
                        if (ImGui::MenuItem("Desempaquetar")) {
                            action = Action::unpack;
                            actionTarget = instanceRoot;
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Enfocar", "F")) focusSelection();
                    ImGui::EndPopup();
                }
                if (open && !children.empty()) {
                    for (const auto child : children) drawNode(child);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };
            if (rootOpen) {
                // Raíces (sin padre o con un padre que ya no existe); con el buscador, lista plana de coincidencias.
                std::vector<ge3d::EntityId> top;
                for (const auto& entity : scene_.entities()) {
                    if (filtering ? contains(entity.name, hierarchyFilter_) : (!entity.parent || !scene_.find(entity.parent))) top.push_back(entity.id);
                }
                for (const auto id : top) drawNode(id);
                ImGui::TreePop();
            }
            // Espacio libre bajo la lista: soltar ahí saca el objeto a la raíz, al final.
            ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, ImGui::GetFrameHeight())});
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ENTITY")) {
                    reparent = {draggedIds(*static_cast<const ge3d::EntityId*>(payload->Data)), 0, 0, false, true};
                }
                if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ASSET")) {
                    droppedAsset = static_cast<const char*>(payload->Data);
                    droppedOn = 0;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl) clearSelection();
            if (ImGui::BeginPopupContextWindow("crear", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Pegar", "Ctrl+V", false, !clipboard_.empty())) action = Action::paste;
                ImGui::Separator();
                createMenu();
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    // Se aplica después de recorrer la lista para no modificarla mientras se itera.
    if (reparent.pending && !editInProgress()) {
        if (auto command = ge3d::makeReparentCommand(scene_, reparent.ids, reparent.parent, reparent.anchor, reparent.after)) {
            execute(std::move(*command));
        }
    }
    if (!droppedAsset.empty()) {
        // Un prefab soltado en la Jerarquía se instancia (como hijo si cae sobre un objeto).
        if (droppedAsset.ends_with(".prefab.json")) instantiatePrefab(droppedAsset, std::nullopt, droppedOn);
        else if (droppedOn) applyAsset(droppedAsset, droppedOn, std::nullopt);
    }
    if (action == Action::duplicate) duplicateSelection();
    if (action == Action::remove) deleteSelection();
    if (action == Action::copy) copySelection();
    if (action == Action::paste) pasteClipboard();
    if (action == Action::activate || action == Action::deactivate) setActive(action == Action::activate);
    if (action == Action::emptyChild) createEmptyChild(actionTarget);
    if (action == Action::unpack) unpackPrefab(actionTarget);
    if (action == Action::unparent) {
        if (auto command = ge3d::makeReparentCommand(scene_, selection_, 0, 0, false)) execute(std::move(*command));
    }
}// --- Inspector ---------------------------------------------------------------------------------

void EditorView::applyComponentAction(ge3d::Entity& entity, const ge3d::Entity& frameStart, int kind, int action) {
    // kind sigue el orden de Removal en drawInspector: 1 transformación, 2 forma, 3 vóxeles, 4 malla,
    // 5 script, 6 cuerpo, 7 audio, 8 jugador.
    if (action == 2) {
        componentClipboardKind_ = kind;
        componentClipboard_ = entity;
        log("Valores del componente copiados");
        return;
    }
    std::optional<ge3d::Entity> before = frameStart;
    const auto& source = componentClipboard_;
    const bool paste = action == 3;
    if (paste && componentClipboardKind_ != kind) return;
    switch (kind) {
    case 1: entity.transform = paste ? source.transform : ge3d::Transform{}; break;
    case 2:
        if (paste && source.shape) entity.shape = source.shape;
        else if (entity.shape) entity.shape = ge3d::makeShape(entity.shape->primitive);
        break;
    case 3:
        if (paste && source.terrain) {
            entity.terrain = source.terrain;
            if (entity.terrain->voxels) entity.terrain->voxels = std::make_shared<ge3d::VoxelGrid>(*entity.terrain->voxels);
        } else if (!paste) {
            entity.terrain = ge3d::makeTerrain({});
        }
        break;
    case 4:
        if (paste && source.heightmap) {
            entity.heightmap = source.heightmap;
            if (entity.heightmap->data) entity.heightmap->data = std::make_shared<ge3d::Heightmap>(*entity.heightmap->data);
        } else if (!paste) {
            entity.heightmap = ge3d::makeHeightmap({});
        }
        break;
    case 5:
        if (paste && source.script) entity.script = source.script;
        else if (entity.script) entity.script->parameters.clear();
        break;
    case 6: entity.body = paste ? source.body : std::optional<ge3d::BodyComponent>(ge3d::BodyComponent{}); break;
    case 7:
        if (paste && source.audio) entity.audio = source.audio;
        else if (entity.audio) entity.audio = ge3d::AudioSourceComponent{entity.audio->clip};
        break;
    case 8: entity.player = paste ? source.player : std::optional<ge3d::PlayerComponent>(ge3d::PlayerComponent{}); break;
    default: return;
    }
    commitModification(before, paste ? "Pegar valores" : "Restablecer componente");
}

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
        // Cabecera: casilla de activo, icono y nombre (como en Unity).
        bool active = entity->active;
        if (ImGui::Checkbox("##activo", &active)) {
            std::optional<ge3d::Entity> before = frameStart;
            entity->active = active;
            commitModification(before, active ? "Activar" : "Desactivar");
        }
        ImGui::SetItemTooltip("Activo: si se desactiva, no se dibuja, no choca, no suena y su script no corre");
        ImGui::SameLine();
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

        drawPrefabBanner(*entity);
        if (selection_.size() > 1) {
            ImGui::TextDisabled("%zu objetos seleccionados: se muestra el activo", selection_.size());
            ImGui::TextDisabled("El gizmo mueve, gira y escala todos a la vez.");
        }

        // Cada componente tiene un menú (boton ... o clic derecho en su cabecera): Restablecer, Copiar valores,
        // Pegar valores y Quitar.
        enum class Removal { none, transform, shape, terrain, heightmap, script, body, audio, player };
        Removal removal = Removal::none;
        Removal menuKind = Removal::none;
        int menuAction = 0;   // 1 restablecer, 2 copiar, 3 pegar
        std::string revertComponent;
        static constexpr const char* componentNames[] = {"", "transform", "shape", "terrain", "heightmap", "script", "body", "audio", "player"};
        const auto removeMenu = [&](const char* id, Removal kind) {
            // En una instancia de prefab, una barra azul marca los componentes cambiados en esta copia.
            const char* component = componentNames[static_cast<int>(kind)];
            const bool changed = entity->prefab && std::find(entity->prefab->overrides.begin(), entity->prefab->overrides.end(), component) !=
                                                       entity->prefab->overrides.end();
            if (changed) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled({min.x - 4, min.y}, {min.x - 1, max.y}, IM_COL32(90, 170, 255, 255));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cambiado en esta copia del prefab");
            }
            ImGui::OpenPopupOnItemClick(id, ImGuiPopupFlags_MouseButtonRight);
            const float button = ImGui::GetFrameHeight();
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - button);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
            if (ImGui::Button((std::string("...##") + id).c_str(), {button, 0})) ImGui::OpenPopup(id);
            ImGui::PopStyleColor();
            if (ImGui::BeginPopup(id)) {
                if (ImGui::MenuItem("Restablecer")) {
                    menuKind = kind;
                    menuAction = 1;
                }
                if (ImGui::MenuItem("Copiar valores")) {
                    menuKind = kind;
                    menuAction = 2;
                }
                if (ImGui::MenuItem("Pegar valores", nullptr, false, componentClipboardKind_ == static_cast<int>(kind))) {
                    menuKind = kind;
                    menuAction = 3;
                }
                if (changed) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Revertir al prefab")) revertComponent = component;
                }
                if (kind != Removal::transform) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Quitar componente")) removal = kind;
                }
                ImGui::EndPopup();
            }
        };

        // Componente Transformación (siempre presente).
        const bool transformOpen = componentHeader("Transformacion", boldFont_);
        removeMenu("transform_menu", Removal::transform);
        if (transformOpen) {
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

        if (entity->body) {
            const bool open = componentHeader("Cuerpo fisico", boldFont_);
            removeMenu("cuerpo_menu", Removal::body);
            if (open) drawBodyComponent(*entity, frameStart);
            ImGui::Spacing();
        }
        if (entity->audio) {
            const bool open = componentHeader("Fuente de audio", boldFont_);
            removeMenu("audio_menu", Removal::audio);
            if (open) drawAudioComponent(*entity, frameStart);
            ImGui::Spacing();
        }
        if (entity->player) {
            const bool open = componentHeader("Jugador", boldFont_);
            removeMenu("jugador_menu", Removal::player);
            if (open) drawPlayerComponent(*entity, frameStart);
            ImGui::Spacing();
        }

        // Componente Script: comportamiento en Luau con sus parámetros.
        if (entity->script) {
            const bool open = componentHeader("Script", boldFont_);
            removeMenu("script_menu", Removal::script);
            if (open) drawScriptComponent(*entity, frameStart);
            ImGui::Spacing();
        }

        if (menuAction) applyComponentAction(*entity, frameStart, static_cast<int>(menuKind), menuAction);
        if (!revertComponent.empty() && entity->prefab) {
            if (const auto* content = prefabContent(entity->prefab->asset)) {
                if (auto command = ge3d::makeRevertCommand(scene_, entity->id, *content, revertComponent)) execute(std::move(*command));
            }
        }
        if (removal != Removal::none) {
            std::optional<ge3d::Entity> before = frameStart;
            if (removal == Removal::shape) entity->shape.reset();
            if (removal == Removal::terrain) entity->terrain.reset();
            if (removal == Removal::heightmap) entity->heightmap.reset();
            if (removal == Removal::script) entity->script.reset();
            if (removal == Removal::body) entity->body.reset();
            if (removal == Removal::audio) entity->audio.reset();
            if (removal == Removal::player) entity->player.reset();
            commitModification(before, "Quitar componente");
        }

        ImGui::Separator();
        ImGui::Spacing();
        const float buttonWidth = std::min(ImGui::GetContentRegionAvail().x, 220 * scale_);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);
        if (ImGui::Button("Agregar componente", {buttonWidth, 0})) {
            addComponentFilter_[0] = '\0';
            ImGui::OpenPopup("agregar_componente");
        }
        if (ImGui::BeginPopup("agregar_componente")) {
            std::optional<ge3d::Entity> before = frameStart;
            bool added = false;
            // Buscador como el de Unity: escribe y pulsa Intro para añadir el primero que coincida.
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(240 * scale_);
            const bool enter = ImGui::InputTextWithHint("##buscar_componente", "Buscar componente", addComponentFilter_,
                                                        sizeof addComponentFilter_, ImGuiInputTextFlags_EnterReturnsTrue);
            if (addComponentFilter_[0]) {
                struct Option {
                    const char* name;
                    bool available;
                    std::function<void()> add;
                };
                const std::vector<Option> options = {
                    {"Forma: caja", !entity->shape, [&] { entity->shape = ge3d::makeShape(ge3d::Primitive::box, paintMaterial_); }},
                    {"Forma: rampa", !entity->shape, [&] { entity->shape = ge3d::makeShape(ge3d::Primitive::ramp, paintMaterial_); }},
                    {"Terreno", !entity->heightmap, [&] { entity->heightmap = ge3d::makeHeightmap(newHeightmap_); }},
                    {"Terreno de voxeles", !entity->terrain, [&] { entity->terrain = ge3d::makeTerrain(newTerrain_); }},
                    {"Cuerpo fisico", !entity->body, [&] { entity->body = ge3d::BodyComponent{}; }},
                    {"Zona (trigger)", !entity->body, [&] { entity->body = ge3d::BodyComponent{ge3d::BodyType::trigger}; }},
                    {"Fuente de audio", !entity->audio, [&] {
                         entity->audio = ge3d::AudioSourceComponent{soundList_.empty() ? std::string() : soundList_.front()};
                     }},
                    {"Jugador", !entity->player, [&] { entity->player = ge3d::PlayerComponent{}; }},
                };
                bool any = false;
                for (const auto& option : options) {
                    if (!contains(option.name, addComponentFilter_)) continue;
                    any = true;
                    if ((ImGui::MenuItem(option.name, nullptr, false, option.available) || (enter && option.available && !added))) {
                        option.add();
                        added = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                for (const auto& path : scriptList_) {
                    if (entity->script || !contains(path, addComponentFilter_)) continue;
                    any = true;
                    if (ImGui::MenuItem(("Script: " + path).c_str()) || (enter && !added)) {
                        entity->script = ge3d::ScriptComponent{path, true, {}};
                        added = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!any) ImGui::TextDisabled("Nada coincide");
                if (added) commitModification(before, "Agregar componente");
                ImGui::EndPopup();
            } else {
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
            if (ImGui::BeginMenu("Script", !entity->script)) {
                for (const auto& path : scriptList_) {
                    if (ImGui::MenuItem(path.c_str())) {
                        entity->script = ge3d::ScriptComponent{path, true, {}};
                        added = true;
                    }
                }
                if (scriptList_.empty()) ImGui::TextDisabled("No hay scripts en Content/scripts");
                ImGui::Separator();
                if (ImGui::MenuItem("Nuevo script...")) {
                    showCode_ = true;
                    ImGui::SetWindowFocus("Codigo");
                    log("Crea el script con \"+ Nuevo script\" en la pestana Codigo y luego elige el en el Inspector");
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cuerpo fisico", nullptr, false, !entity->body)) {
                entity->body = ge3d::BodyComponent{};
                added = true;
            }
            if (ImGui::MenuItem("Zona (trigger)", nullptr, false, !entity->body)) {
                entity->body = ge3d::BodyComponent{ge3d::BodyType::trigger};
                added = true;
            }
            if (ImGui::MenuItem("Fuente de audio", nullptr, false, !entity->audio)) {
                entity->audio = ge3d::AudioSourceComponent{soundList_.empty() ? std::string() : soundList_.front()};
                added = true;
            }
            if (ImGui::MenuItem("Jugador", nullptr, false, !entity->player)) {
                entity->player = ge3d::PlayerComponent{};
                added = true;
            }
            if (added) commitModification(before, "Agregar componente");
            ImGui::EndPopup();
            }
        }
        ImGui::Spacing();
        ImGui::TextDisabled("UUID");
        ImGui::TextDisabled("%s", entity->uuid.toString().c_str());
        ImGui::PopID();
        // Soltar un script, sonido o material del Proyecto en el Inspector lo asigna al objeto activo.
        if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##soltar_inspector"))) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ASSET")) {
                applyAsset(std::string(static_cast<const char*>(payload->Data)), selected_, std::nullopt);
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::End();
}

// --- Vista de escena ---------------------------------------------------------------------------

void EditorView::flyCamera() {
    const auto& io = ImGui::GetIO();
    const glm::vec3 eye = cameraPosition();
    yaw_ -= io.MouseDelta.x * 0.005f;
    pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.005f, -1.55f, 1.55f);
    const glm::vec3 orbit{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)};
    const glm::vec3 forward = -orbit;
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0, 1, 0}));
    glm::vec3 move{0.0f};
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
    if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
    if (io.MouseWheel != 0.0f) {
        flySpeed_ = std::clamp(flySpeed_ * std::pow(1.25f, io.MouseWheel), 0.5f, 300.0f);
        flySpeedShown_ = ImGui::GetTime();
    }
    const float speed = flySpeed_ * (io.KeyShift ? 3.0f : 1.0f);
    // La cámara gira sobre sí misma: el punto que se mira se recoloca delante del ojo.
    pivot_ = eye + move * speed * std::min(io.DeltaTime, 0.1f) + forward * distance_;
}

void EditorView::drawSelectionOutline(ImDrawList* draw, ImVec2 origin, ImVec2 size) {
    if (firstPerson_) return;
    // Contorno naranja como en Unity: más intenso en el objeto activo.
    for (const auto id : selection_) {
        const auto* entity = scene_.find(id);
        if (!entity || !entity->shape) continue;
        const ImU32 color = id == selected_ ? IM_COL32(255, 150, 40, 255) : IM_COL32(255, 180, 90, 190);
        drawPieceOutline(draw, entity->shape->primitive, worldMatrix(*entity), origin, size, color, IM_COL32(0, 0, 0, 0));
    }
}

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

            const bool viewHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);
            if (firstPerson_ && !mouseCaptured_ && viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                mouseCaptured_ = true;
                ImGui::SetWindowFocus();
            }
            // En primera persona el ratón es del jugador: nada de órbita, gizmos ni selección en la vista.
            const bool hovered = viewHovered && !firstPerson_;
            const bool altOrbit = io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            // Como en Unity: clic derecho mira y vuela con WASD/QE (rueda: velocidad); Alt + clic izquierdo orbita.
            flying_ = (flying_ || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) && ImGui::IsMouseDown(ImGuiMouseButton_Right);
            orbiting_ = (orbiting_ || (hovered && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) && altOrbit;
            if (flying_) flyCamera();
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
            if (hovered && !flying_) distance_ = std::clamp(distance_ * std::pow(0.87f, io.MouseWheel), 0.5f, maxDistance);

            draw->PushClipRect(origin, end, true);
            ImGuizmo::SetDrawlist(draw);
            ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
            auto viewMatrix = view();
            const auto projectionMatrix = projection();
            bool overGizmo = false;
            auto* entity = scene_.find(selected_);
            const bool gizmoTool = (tool_ == Tool::move || tool_ == Tool::rotate || tool_ == Tool::scale) && !firstPerson_;
            if (entity && gizmoTool && !orbiting_) {
                const ImGuizmo::OPERATION operation = tool_ == Tool::move ? ImGuizmo::TRANSLATE
                                                    : tool_ == Tool::rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
                const float step = tool_ == Tool::rotate ? 15.0f : 0.25f;
                const float snapValues[] = {step, step, step};
                const ge3d::Entity beforeManipulate = *entity;
                std::vector<ge3d::Entity> beforeGroup;
                for (const auto id : selection_) {
                    if (const auto* selectedEntity = scene_.find(id)) beforeGroup.push_back(*selectedEntity);
                }
                const glm::mat4 oldModel = worldMatrix(*entity);
                auto model = oldModel;
                // Lo que se escribe en la entidad es relativo a su padre.
                const auto setWorld = [&](ge3d::Entity& target, const float* t, const float* r, const float* k, bool rotation, bool scale) {
                    ge3d::Transform world = ge3d::worldTransform(scene_, target);
                    world.position = {t[0], t[1], t[2]};
                    if (rotation) world.rotationDegrees = {r[0], r[1], r[2]};
                    if (scale) world.scale = {std::max(0.01f, k[0]), std::max(0.01f, k[1]), std::max(0.01f, k[2])};
                    target.transform = ge3d::localTransform(scene_, target.parent, world);
                };
                if (ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projectionMatrix),
                        operation,
                        localAxes_ || tool_ == Tool::scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                        glm::value_ptr(model), nullptr, snap_ || io.KeyCtrl ? snapValues : nullptr)) {
                    float translation[3], rotation[3], scaling[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scaling);
                    setWorld(*entity, translation, rotation, scaling, true, true);
                    // El resto de la selección sigue al objeto activo: mismo desplazamiento y giro (alrededor
                    // del activo) o la misma proporción de escala.
                    const glm::mat4 delta = worldMatrix(*entity) * glm::inverse(oldModel);
                    const auto& oldScale = beforeManipulate.transform.scale;
                    const auto& newScale = entity->transform.scale;
                    for (const auto id : selection_) {
                        auto* other = scene_.find(id);
                        if (!other || other == entity) continue;
                        // Los hijos de algo seleccionado ya se mueven con su padre.
                        if (std::any_of(selection_.begin(), selection_.end(), [&](ge3d::EntityId s) { return ge3d::isAncestor(scene_, s, id); })) continue;
                        if (tool_ == Tool::scale) {
                            other->transform.scale = {std::max(0.01f, other->transform.scale.x * newScale.x / oldScale.x),
                                                      std::max(0.01f, other->transform.scale.y * newScale.y / oldScale.y),
                                                      std::max(0.01f, other->transform.scale.z * newScale.z / oldScale.z)};
                            continue;
                        }
                        glm::mat4 moved = delta * worldMatrix(*other);
                        float t[3], r[3], k[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(moved), t, r, k);
                        setWorld(*other, t, r, k, tool_ == Tool::rotate, false);
                    }
                }
                // El arrastre se previsualiza en vivo y se guarda como una sola operación al soltar.
                if (ImGuizmo::IsUsing() && gizmoEdit_.empty()) gizmoEdit_ = beforeGroup;
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
            bool overCube = false;
            if (!firstPerson_) {
                ImGuizmo::ViewManipulate(glm::value_ptr(viewMatrix), distance_, {end.x - cube, origin.y}, {cube, cube}, 0x00000000);
                if (viewMatrix != previousView) setViewFromMatrix(viewMatrix);
                overCube = ImGuizmo::IsViewManipulateHovered();
            }

            const bool canUseMouse = hovered && !overGizmo && !overCube && !io.KeyAlt && !orbiting_ && !panning_ && !flying_;
            if (tool_ == Tool::paint) {
                // Pintar: vista previa de la cara bajo el cursor, trazo con arrastre, cuentagotas con Ctrl.
                const auto hit = canUseMouse ? raycast(io.MousePos, origin, size) : std::nullopt;
                if (hit && !io.KeyCtrl && !hit->terrain) {
                    if (const auto* target = scene_.find(hit->entity); target && target->shape) {
                        drawPieceOutline(draw, target->shape->primitive, worldMatrix(*target), origin, size,
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
            drawSelectionOutline(draw, origin, size);
            drawPlayOverlay(draw, origin, size);
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
            bool interacted = false;
            if (canUseMouse && isPlaying() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // En Play, el clic sobre un objeto con script llama a su onInteract.
                if (const auto hit = raycast(io.MousePos, origin, size)) interacted = scripts_->interact(scene_, hit->entity);
            }
            if (!interacted && canUseMouse && selectTool && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                pick(io.MousePos, origin, size);
            }
            if (marquee_) {
                const ImVec2 a{std::min(marqueeStart_.x, io.MousePos.x), std::min(marqueeStart_.y, io.MousePos.y)};
                const ImVec2 b{std::max(marqueeStart_.x, io.MousePos.x), std::max(marqueeStart_.y, io.MousePos.y)};
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    draw->AddRectFilled(a, b, IM_COL32(80, 140, 220, 50));
                    draw->AddRect(a, b, IM_COL32(110, 170, 255, 220));
                } else {
                    marquee_ = false;
                    const bool additive = io.KeyCtrl || io.KeyShift;
                    if (b.x - a.x < 4 && b.y - a.y < 4) {
                        if (!additive) clearSelection();
                    } else {
                        // Lo que tenga el centro dentro del rectángulo.
                        if (!additive) clearSelection();
                        const glm::mat4 clip = projection() * view();
                        for (const auto& candidate : scene_.entities()) {
                            const glm::vec4 c = clip * glm::vec4(vector(ge3d::worldTransform(scene_, candidate).position), 1.0f);
                            if (c.w <= 0.01f) continue;
                            const ImVec2 p{origin.x + (c.x / c.w * 0.5f + 0.5f) * size.x, origin.y + (0.5f - c.y / c.w * 0.5f) * size.y};
                            if (p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y) select(candidate.id, SelectMode::add);
                        }
                    }
                }
            }
            if (flying_ || ImGui::GetTime() - flySpeedShown_ < 1.2) {
                char speed[64];
                std::snprintf(speed, sizeof speed, "Vuelo: %.1f m/s  (rueda: velocidad, Shift: x3)", flySpeed_);
                draw->AddText({origin.x + 8 * scale_, end.y - ImGui::GetTextLineHeight() - 6 * scale_}, IM_COL32(255, 255, 255, 190), speed);
            }
            ImGui::Dummy(available);
            // Soltar un recurso del Proyecto sobre un objeto de la vista (Shift: material solo en esa cara).
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                         ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    const std::string asset(static_cast<const char*>(payload->Data));
                    const auto hit = raycast(io.MousePos, origin, size);
                    auto* overlay = ImGui::GetWindowDrawList();
                    if (hit) {
                        if (const auto* target = scene_.find(hit->entity); target && target->shape) {
                            drawPieceOutline(overlay, target->shape->primitive, worldMatrix(*target), origin, size,
                                             IM_COL32(120, 200, 255, 255), IM_COL32(120, 200, 255, 50),
                                             io.KeyShift && asset.starts_with("material:") ? static_cast<int>(hit->face) : -1);
                        }
                    }
                    if (payload->IsDelivery()) {
                        if (asset.ends_with(".prefab.json")) {
                            std::optional<glm::vec3> point;
                            if (hit) point = hit->point;
                            else {
                                const auto ray = mouseRay(io.MousePos, origin, size);
                                if (std::abs(ray.direction.y) > 1e-4f) {
                                    const float t = -ray.start.y / ray.direction.y;
                                    if (t > 0) point = ray.start + ray.direction * t;
                                }
                            }
                            instantiatePrefab(asset, point, 0);
                        } else if (asset.ends_with(".scene.json")) {
                            requestSceneChange(SceneChange::open, contentRoot_ / ge3d::pathFromUtf8(asset));
                        } else if (hit) {
                            const bool face = io.KeyShift && !hit->terrain;
                            applyAsset(asset, hit->entity, face ? std::optional<std::uint32_t>(hit->face) : std::nullopt);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
    ImGui::End();
    if (painting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishPaintStroke();
    if (building_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishBuildStroke();
    if (erasing_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishEraseStroke();
    if (sculpting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) finishSculpt();
    if (!gizmoEdit_.empty() && (!ImGuizmo::IsUsing() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        const char* label = tool_ == Tool::rotate ? "Rotar" : tool_ == Tool::scale ? "Escalar" : "Mover";
        if (auto command = ge3d::makeModifyCommand(scene_, gizmoEdit_, label)) activeHistory().record(std::move(*command));
        gizmoEdit_.clear();
    }
}

// --- Proyecto ----------------------------------------------------------------------------------

void EditorView::drawProjectNode(const ProjectNode& node, bool root) {
    // Columna izquierda: solo carpetas.
    if (!node.directory) return;
    const bool hasFolders = std::any_of(node.children.begin(), node.children.end(), [](const ProjectNode& c) { return c.directory; });
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (root) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasFolders) flags |= ImGuiTreeNodeFlags_Leaf;
    if (!projectMaterials_ && node.path == projectFolder_) flags |= ImGuiTreeNodeFlags_Selected;
    const bool open = treeItem(&node, node.name.c_str(), Icon::folder, ImGui::GetColorU32(ImVec4{0.85f, 0.72f, 0.38f, 1}), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        projectFolder_ = node.path;
        projectMaterials_ = false;
    }
    if (open) {
        for (const auto& child : node.children) drawProjectNode(child, false);
        ImGui::TreePop();
    }
}

const EditorView::ProjectNode* EditorView::findProjectNode(const ProjectNode& node, const std::filesystem::path& path) const {
    if (node.path == path) return &node;
    for (const auto& child : node.children) {
        if (!child.directory) continue;
        if (const auto* found = findProjectNode(child, path)) return found;
    }
    return nullptr;
}

std::string EditorView::assetPath(const std::filesystem::path& path) const {
    std::error_code error;
    auto relative = ge3d::pathToUtf8(std::filesystem::relative(path, contentRoot_, error));
    std::replace(relative.begin(), relative.end(), '\\', '/');
    return relative;
}

void EditorView::applyAsset(const std::string& asset, ge3d::EntityId id, std::optional<std::uint32_t> face) {
    auto* entity = scene_.find(id);
    if (!entity || editInProgress()) return;
    std::optional<ge3d::Entity> before = *entity;
    const char* label = nullptr;
    if (asset.starts_with("material:")) {
        const auto material = asset.substr(9);
        if (!entity->shape) {
            log("Solo las piezas con Forma tienen materiales (los terrenos usan capas)", true);
            return;
        }
        if (face && *face < ge3d::maxFaces) entity->shape->materials[*face] = material;
        else entity->shape->materials.fill(material);
        paintMaterial_ = material;
        label = "Asignar material";
    } else if (asset.ends_with(".luau") && ge3d::isValidScriptPath(asset)) {
        if (!entity->script || entity->script->path != asset) entity->script = ge3d::ScriptComponent{asset, true, {}};
        label = "Asignar script";
    } else if (asset.ends_with(".wav") && ge3d::isValidSoundPath(asset)) {
        if (entity->audio) entity->audio->clip = asset;
        else entity->audio = ge3d::AudioSourceComponent{asset};
        label = "Asignar sonido";
    } else {
        log("Ese recurso no se puede asignar a un objeto: " + asset, true);
        return;
    }
    commitModification(before, label);
    select(id);
}
void EditorView::drawProject() {
    if (!showProject_) return;
    namespace fs = std::filesystem;
    if (!ImGui::Begin("Proyecto", &showProject_)) {
        ImGui::End();
        return;
    }
    if (!projectFound_) {
        ImGui::TextDisabled("No existe la carpeta de contenido.");
        if (ImGui::Button("Crear carpeta")) {
            std::error_code error;
            fs::create_directories(contentRoot_ / "scenes", error);
            if (error) log("No se pudo crear la carpeta: " + error.message(), true);
            refreshProject();
        }
        ImGui::End();
        return;
    }
    if (!findProjectNode(project_, projectFolder_)) projectFolder_ = contentRoot_;

    // Barra: actualizar, ruta, buscador y tamaño de los iconos.
    if (ImGui::Button("Actualizar")) refreshProject();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const std::string here = projectMaterials_ ? "Content / Materiales"
                           : "Content" + (projectFolder_ == contentRoot_ ? std::string() : " / " + assetPath(projectFolder_));
    ImGui::TextDisabled("%s", here.c_str());
    const float tools = 300 * scale_;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - tools));
    ImGui::SetNextItemWidth(170 * scale_);
    ImGui::InputTextWithHint("##buscar_recurso", "Buscar en el proyecto", projectFilter_, sizeof projectFilter_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##tamano", &projectTile_, 40.0f, 120.0f, "");
    ImGui::SetItemTooltip("Tamano de los iconos");
    ImGui::Separator();

    // Columna de carpetas.
    if (ImGui::BeginChild("##carpetas", {180 * scale_, 0}, ImGuiChildFlags_ResizeX)) {
        drawProjectNode(project_, true);
        if (treeItem("##materiales", "Materiales", Icon::paint, ImGui::GetColorU32(accentHover),
                     ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                         (projectMaterials_ ? ImGuiTreeNodeFlags_Selected : 0))) {
        }
        if (ImGui::IsItemClicked()) projectMaterials_ = true;
        ImGui::SetItemTooltip("Catalogo de materiales (materials.json): arrastralos sobre las piezas");
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Rejilla de recursos.
    struct Item {
        std::string name;
        fs::path path;
        std::string asset;      // ruta relativa (o "material:<id>")
        bool directory{};
        Icon icon{Icon::file};
        ImU32 color{};
        ImTextureID texture{};
    };
    std::vector<Item> items;
    const auto kind = [&](const fs::path& path, bool directory, Item& item) {
        const auto name = ge3d::pathToUtf8(path.filename());
        item.icon = directory ? Icon::folder : name.ends_with(".prefab.json") ? Icon::cube : name.ends_with(".scene.json") ? Icon::scene
                  : name.ends_with(".luau") ? Icon::script
                  : name.ends_with(".wav") ? Icon::sound : Icon::file;
        item.color = ImGui::GetColorU32(directory ? ImVec4{0.85f, 0.72f, 0.38f, 1} : item.icon == Icon::cube ? ImVec4{0.50f, 0.74f, 1.0f, 1}
                                       : item.icon == Icon::scene ? accentHover
                                       : item.icon == Icon::script ? ImVec4{0.55f, 0.85f, 0.55f, 1}
                                       : item.icon == Icon::sound ? ImVec4{0.95f, 0.65f, 0.40f, 1} : gray(0.7f));
    };
    const bool searching = projectFilter_[0] != '\0';
    if (projectMaterials_ || searching) {
        const auto& materials = library_.materials();
        for (std::size_t i = 0; i < materials.size(); ++i) {
            if (!contains(materials[i].name, projectFilter_) && !contains(materials[i].id, projectFilter_)) continue;
            Item item{materials[i].name, {}, "material:" + materials[i].id, false, Icon::paint, ImGui::GetColorU32(accentHover)};
            if (i < thumbnails_.size()) item.texture = thumbnails_[i];
            items.push_back(std::move(item));
        }
    }
    if (searching) {
        // Búsqueda en todo el proyecto, como en Unity.
        const auto collect = [&](auto& self, const ProjectNode& node) -> void {
            for (const auto& child : node.children) {
                if (contains(child.name, projectFilter_)) {
                    Item item{child.name, child.path, assetPath(child.path), child.directory};
                    kind(child.path, child.directory, item);
                    items.push_back(std::move(item));
                }
                if (child.directory) self(self, child);
            }
        };
        collect(collect, project_);
    } else if (!projectMaterials_) {
        if (const auto* folder = findProjectNode(project_, projectFolder_)) {
            for (const auto& child : folder->children) {
                Item item{child.name, child.path, assetPath(child.path), child.directory};
                kind(child.path, child.directory, item);
                items.push_back(std::move(item));
            }
        }
    }

    bool openCreate = false;
    if (ImGui::BeginChild("##recursos")) {
        const float tile = projectTile_ * scale_;
        const float cell = tile + 14 * scale_;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
        auto* draw = ImGui::GetWindowDrawList();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (i % static_cast<std::size_t>(columns) != 0) ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();
            const ImVec2 corner = ImGui::GetCursorScreenPos();
            const float labelHeight = ImGui::GetTextLineHeight() * 2;
            ImGui::InvisibleButton("##recurso", {cell, tile + labelHeight + 4});
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = projectSelected_ == item.asset;
            if (selected || hovered) {
                draw->AddRectFilled(corner, {corner.x + cell, corner.y + tile + labelHeight + 4},
                                    selected ? ImGui::GetColorU32(accent) : ImGui::GetColorU32(gray(0.3f)), 4.0f);
            }
            const ImVec2 iconMin{corner.x + (cell - tile) * 0.5f, corner.y + 2};
            if (item.texture) {
                draw->AddImage(item.texture, {iconMin.x + tile * 0.1f, iconMin.y + tile * 0.1f}, {iconMin.x + tile * 0.9f, iconMin.y + tile * 0.9f});
            } else {
                drawIcon(draw, item.icon, {iconMin.x + tile * 0.5f, iconMin.y + tile * 0.5f}, tile * 0.32f, item.color);
            }
            // Nombre (dos líneas como máximo).
            std::string label = item.name;
            const float maxWidth = cell - 4;
            while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > maxWidth * 2) label.pop_back();
            if (label.size() < item.name.size()) label += "...";
            draw->PushClipRect({corner.x, corner.y}, {corner.x + cell, corner.y + tile + labelHeight + 4}, true);
            draw->AddText(nullptr, 0.0f, {corner.x + 2, corner.y + tile + 2}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str(), nullptr, maxWidth);
            draw->PopClipRect();

            // Lo que sigue usa el último elemento: el botón invisible de la casilla (tiene identificador).
            if (ImGui::IsItemClicked()) projectSelected_ = item.asset;
            if (hovered) ImGui::SetTooltip("%s", item.asset.starts_with("material:") ? item.name.c_str() : item.asset.c_str());
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                // Doble clic: entrar en la carpeta, abrir la escena o el script, escuchar el sonido.
                if (item.directory) {
                    projectFolder_ = item.path;
                    projectFilter_[0] = '\0';
                } else if (item.icon == Icon::cube) {
                    openPrefab(item.path);
                } else if (item.icon == Icon::scene) {
                    requestSceneChange(SceneChange::open, item.path);
                } else if (item.icon == Icon::script) {
                    openScript(item.asset);
                } else if (item.icon == Icon::sound) {
                    if (!audio_) {
                        audio_ = std::make_unique<ge3d::AudioSystem>(contentRoot_, [](bool error, const std::string& message) {
                            editorLog().add(error ? LogLevel::warning : LogLevel::info, message);
                        });
                    }
                    audio_->playOneShot(item.asset, 1.0f, std::nullopt);
                    audio_->flush();
                } else if (item.asset.starts_with("material:")) {
                    paintMaterial_ = item.asset.substr(9);
                    tool_ = Tool::paint;
                }
            }
            // Arrastrar a la Escena, la Jerarquía o el Inspector.
            if (!item.directory && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("GE3D_ASSET", item.asset.c_str(), item.asset.size() + 1);
                ImGui::TextUnformatted(item.name.c_str());
                ImGui::TextDisabled(item.icon == Icon::scene ? "Soltar en la Escena para abrirla"
                                    : item.icon == Icon::cube ? "Soltar en la Escena o la Jerarquia para crear una copia"
                                                              : "Soltar sobre un objeto");
                ImGui::EndDragDropSource();
            }
            if (!item.asset.starts_with("material:") && ImGui::BeginPopupContextItem("menu_recurso")) {
                projectSelected_ = item.asset;
                if (ImGui::MenuItem("Abrir")) {
                    if (item.directory) projectFolder_ = item.path;
                    else if (item.icon == Icon::cube) openPrefab(item.path);
                    else if (item.icon == Icon::scene) requestSceneChange(SceneChange::open, item.path);
                    else if (item.icon == Icon::script) openScript(item.asset);
                }
                if (ImGui::MenuItem("Renombrar")) {
                    projectRename_ = item.path;
                    copyText(projectRenameBuffer_, sizeof projectRenameBuffer_, item.name);
                }
                if (ImGui::MenuItem("Eliminar")) projectDelete_ = item.path;
                ImGui::Separator();
                if (ImGui::MenuItem("Mostrar en el Explorador")) revealInExplorer(item.path);
                ImGui::EndPopup();
            }
            ImGui::EndGroup();
            ImGui::PopID();
        }
        if (items.empty()) ImGui::TextDisabled(searching ? "Nada coincide" : "Carpeta vacia");
        // Soltar un objeto de la Jerarquía aquí lo convierte en prefab (en la carpeta abierta).
        if (!projectMaterials_ && ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##soltar_prefab"))) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("GE3D_ENTITY")) {
                createPrefab(*static_cast<const ge3d::EntityId*>(payload->Data), projectFolder_);
            }
            ImGui::EndDragDropTarget();
        }
        // Clic derecho en el fondo: crear.
        if (!projectMaterials_ && ImGui::BeginPopupContextWindow("crear_recurso", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("Crear")) {
                if (ImGui::MenuItem("Carpeta")) {
                    auto path = projectFolder_ / "Nueva carpeta";
                    std::error_code error;
                    for (int n = 2; fs::exists(path, error); ++n) path = projectFolder_ / ("Nueva carpeta " + std::to_string(n));
                    fs::create_directory(path, error);
                    refreshProject();
                }
                if (ImGui::MenuItem("Script Luau")) openCreate = true;
                if (ImGui::MenuItem("Escena")) {
                    auto path = projectFolder_ / "nueva.scene.json";
                    std::error_code error;
                    for (int n = 2; fs::exists(path, error); ++n) path = projectFolder_ / ("nueva_" + std::to_string(n) + ".scene.json");
                    ge3d::Scene empty;
                    empty.create("Suelo", ge3d::Primitive::box, {{0, -0.25f, 0}, {}, {8, 0.5f, 8}});
                    try {
                        ge3d::saveSceneFile(empty, path);
                        log("Escena creada: " + assetPath(path));
                    } catch (const std::exception& failure) {
                        log(std::string("No se pudo crear la escena: ") + failure.what(), true);
                    }
                    refreshProject();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Actualizar")) refreshProject();
            if (ImGui::MenuItem("Mostrar en el Explorador")) revealInExplorer(projectFolder_);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    if (openCreate) {
        newScriptName_[0] = '\0';
        ImGui::OpenPopup("nuevo_script_proyecto");
    }
    if (ImGui::BeginPopup("nuevo_script_proyecto")) {
        ImGui::TextUnformatted("Nombre del script (se crea en Content/scripts)");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##nombre_script", newScriptName_, sizeof newScriptName_, ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::Button("Crear")) {
            createScript(newScriptName_);
            refreshProject();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Renombrar: las referencias de la escena abierta (scripts y sonidos) se actualizan.
    if (!projectRename_.empty()) ImGui::OpenPopup("renombrar_recurso");
    if (ImGui::BeginPopupModal("renombrar_recurso", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Nuevo nombre para %s", ge3d::pathToUtf8(projectRename_.filename()).c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##nuevo_nombre", projectRenameBuffer_, sizeof projectRenameBuffer_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("Renombrar")) {
            const auto target = projectRename_.parent_path() / ge3d::pathFromUtf8(projectRenameBuffer_);
            std::error_code error;
            if (projectRenameBuffer_[0] == '\0' || fs::exists(target, error)) {
                log("Ya existe un archivo con ese nombre (o el nombre esta vacio)", true);
            } else {
                const auto oldAsset = assetPath(projectRename_);
                fs::rename(projectRename_, target, error);
                if (error) {
                    log("No se pudo renombrar: " + error.message(), true);
                } else {
                    const auto newAsset = assetPath(target);
                    std::vector<ge3d::Entity> before;
                    for (const auto& entity : scene_.entities()) {
                        const bool script = entity.script && entity.script->path == oldAsset;
                        const bool sound = entity.audio && entity.audio->clip == oldAsset;
                        if (!script && !sound) continue;
                        before.push_back(entity);
                        auto* changed = scene_.find(entity.id);
                        if (script) changed->script->path = newAsset;
                        if (sound) changed->audio->clip = newAsset;
                    }
                    if (auto command = ge3d::makeModifyCommand(scene_, before, "Actualizar referencias")) activeHistory().record(std::move(*command));
                    for (auto& document : documents_) {
                        if (document.path == oldAsset) document.path = newAsset;
                    }
                    log("Renombrado " + oldAsset + " -> " + newAsset + (before.empty() ? "" : " (referencias de la escena actualizadas)"));
                    refreshProject();
                    watchScripts(true);
                }
            }
            projectRename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) {
            projectRename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // Eliminar: a la papelera de reciclaje (se puede recuperar).
    if (!projectDelete_.empty()) ImGui::OpenPopup("eliminar_recurso");
    if (ImGui::BeginPopupModal("eliminar_recurso", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enviar %s a la papelera de reciclaje?", ge3d::pathToUtf8(projectDelete_.filename()).c_str());
        ImGui::TextDisabled("Las escenas que lo usen dejaran de encontrarlo.");
        if (ImGui::Button("Eliminar")) {
            if (moveToRecycleBin(projectDelete_)) log("Enviado a la papelera: " + assetPath(projectDelete_));
            else log("No se pudo eliminar " + assetPath(projectDelete_), true);
            projectDelete_.clear();
            refreshProject();
            watchScripts(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) {
            projectDelete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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
        if (toggleButton("Limpiar al jugar", clearOnPlay_, "Vaciar la Consola al pulsar Play")) clearOnPlay_ = !clearOnPlay_;
        ImGui::SameLine();
        if (toggleButton("Pausa en error", pauseOnError_, "Pausar el juego en cuanto aparece un error")) pauseOnError_ = !pauseOnError_;
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
                const std::string text = entry.repeats > 1 ? entry.message + "  (x" + std::to_string(entry.repeats) + ")" : entry.message;
                // Un mensaje que cita "scripts/archivo.luau:línea" se abre en la pestaña Código con un clic.
                const auto at = entry.message.find(".luau:");
                const auto start = at == std::string::npos ? std::string::npos : entry.message.rfind("scripts/", at);
                if (start != std::string::npos) {
                    const auto path = entry.message.substr(start, at + 5 - start);
                    int line = 0;
                    for (auto i = at + 6; i < entry.message.size() && std::isdigit(static_cast<unsigned char>(entry.message[i])); ++i) {
                        line = line * 10 + (entry.message[i] - '0');
                    }
                    ImGui::PushID(&entry);
                    if (ImGui::Selectable(text.c_str()) && ge3d::isValidScriptPath(path)) openScript(path, line);
                    ImGui::SetItemTooltip("Abrir %s en la linea %d", path.c_str(), line);
                    ImGui::PopID();
                } else {
                    ImGui::TextUnformatted(text.c_str());
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
    ImGui::DockBuilderDockWindow("Codigo", center);
    ImGui::DockBuilderFinish(dock);
    focusSceneFrames_ = 2;
}

void EditorView::draw(ImTextureID sceneTexture) {
    validateSelection();
    // Pausa en error (como Error Pause de Unity): el juego se detiene en el fotograma del primer error.
    const int errors = editorLog().counts()[2];
    if (isPlaying() && !paused_ && pauseOnError_ && errors > lastErrorCount_) {
        paused_ = true;
        log("Pausado por un error (Consola > Pausa en error)");
    }
    lastErrorCount_ = errors;
    if (!isPlaying() && history_.state() != overridesState_) {
        overridesState_ = history_.state();
        refreshOverrides();
    }
    if (isPlaying() && !paused_) {
        ++playFrames_;
        updatePlay(std::min(ImGui::GetIO().DeltaTime, 1.0f / 20.0f));
    }
    watchScripts();

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
    drawCode();
    if (focusSceneFrames_ > 0) {
        // Escena y Código comparten el centro: al montar la distribución se muestra la Escena.
        --focusSceneFrames_;
        ImGui::SetWindowFocus("Escena");
    }

    drawPathPopup();
    drawUnsavedPopup();
    drawShortcutsPopup();

    if (tinted) ImGui::PopStyleColor(tinted);
}
