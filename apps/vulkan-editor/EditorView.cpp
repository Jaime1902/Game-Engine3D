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

constexpr float aspect = 320.0f / 180.0f;
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
enum class Icon { hand, move, rotate, scale, play, pause, step, cube, ramp, folder, file, scene };

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

glm::mat4 EditorView::projection() const {
    return glm::perspectiveRH_NO(glm::radians(48.0f), aspect, nearPlane, farPlane);
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

glm::mat4 EditorView::renderCamera() const {
    glm::mat4 clip{1.0f};
    clip[1][1] = -1;
    clip[2][2] = 0.5f;
    clip[3][2] = 0.5f;
    return clip * projection() * view();
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

void EditorView::pick(ImVec2 mouse, ImVec2 origin, ImVec2 size) {
    const auto inverse = glm::inverse(projection() * view());
    const float x = (mouse.x - origin.x) / size.x * 2 - 1;
    const float y = 1 - (mouse.y - origin.y) / size.y * 2;
    const auto near = inverse * glm::vec4(x, y, -1, 1);
    const auto far = inverse * glm::vec4(x, y, 1, 1);
    const auto start = glm::vec3(near) / near.w;
    const auto direction = glm::normalize(glm::vec3(far) / far.w - start);
    static const auto box = ge3d::makePrimitive(ge3d::Primitive::box);
    static const auto ramp = ge3d::makePrimitive(ge3d::Primitive::ramp);
    float closest = std::numeric_limits<float>::infinity();
    selected_ = 0;
    for (const auto& entity : scene_.entities()) {
        const auto model = modelMatrix(entity.transform);
        const auto& mesh = entity.primitive == ge3d::Primitive::box ? box : ramp;
        for (const auto& triangle : mesh.triangles) {
            const auto point = [&](std::uint32_t index) {
                return glm::vec3(model * glm::vec4(vector(mesh.positions[index]), 1));
            };
            const auto& indices = triangle.indices;
            const auto distance = hitTriangle(start, direction, point(indices[0]), point(indices[1]), point(indices[2]));
            if (distance >= 0 && distance < closest) {
                closest = distance;
                selected_ = entity.id;
            }
        }
    }
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
                                     [&](const ge3d::Entity& entity) { return entity.primitive == primitive; });
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
        playSnapshot_ = scene_;
        playHistory_.clear();
        paused_ = false;
        playFrames_ = 0;
        log("Play: los cambios que hagas ahora se descartan al detener");
    } else {
        scene_ = std::move(*playSnapshot_);
        playSnapshot_.reset();
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
    if (isPlaying() && paused_) ++playFrames_;
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
        {"Q / W / E / R", "Mano, mover, rotar, escalar"},
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
                    const bool box = entity.primitive == ge3d::Primitive::box;
                    treeItem(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(entity.id)), displayName(entity),
                             box ? Icon::cube : Icon::ramp, ImGui::GetColorU32(gray(0.75f)), flags);
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
        const bool box = entity->primitive == ge3d::Primitive::box;

        // Cabecera: icono y nombre.
        const float iconSize = ImGui::GetFrameHeight();
        ImGui::Dummy({iconSize, iconSize});
        drawIcon(ImGui::GetWindowDrawList(), box ? Icon::cube : Icon::ramp,
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

        // Componente Transformación.
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

        // Componente Forma.
        if (componentHeader("Forma", boldFont_)) {
            const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.30f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Primitiva");
            ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(-1);
            int primitive = box ? 0 : 1;
            if (ImGui::Combo("##primitiva", &primitive, "Caja\0Rampa\0")) {
                std::optional<ge3d::Entity> before = frameStart;
                entity->primitive = primitive == 0 ? ge3d::Primitive::box : ge3d::Primitive::ramp;
                commitModification(before, "Cambiar forma");
            }
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();
        const float buttonWidth = std::min(ImGui::GetContentRegionAvail().x, 220 * scale_);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f);
        ImGui::BeginDisabled();
        ImGui::Button("Agregar componente", {buttonWidth, 0});
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Llega con la etapa 3: scripts Luau, y la 4: fisica y audio");
        ImGui::Spacing();
        ImGui::TextDisabled("UUID");
        ImGui::TextDisabled("%s", entity->uuid.toString().c_str());
        ImGui::PopID();
    }
    ImGui::End();
}

// --- Vista de escena ---------------------------------------------------------------------------

void EditorView::drawViewport(ImTextureID sceneTexture) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool visible = ImGui::Begin("Escena", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (visible) {
        // Barra propia de la vista.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, gray(0.19f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6 * scale_, 3 * scale_});
        if (ImGui::BeginChild("##barra_escena", {0, ImGui::GetFrameHeight() + 6 * scale_},
                              ImGuiChildFlags_AlwaysUseWindowPadding)) {
            if (toggleButton("Cuadricula", showGrid_, "Mostrar u ocultar la cuadricula del suelo")) showGrid_ = !showGrid_;
            ImGui::SameLine();
            if (ImGui::Button("Enfocar")) focusSelection();
            ImGui::SetItemTooltip("Encuadrar la seleccion (F)");
            const char* info = "320 x 180  |  Perspectiva";
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - ImGui::CalcTextSize(info).x - 10 * scale_));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", info);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        const auto available = ImGui::GetContentRegionAvail();
        const auto position = ImGui::GetCursorScreenPos();
        const auto& io = ImGui::GetIO();
        if (available.x > 1 && available.y > 1) {
            const auto dpi = io.DisplayFramebufferScale;
            const auto zoom = std::min(available.x * dpi.x / 320, available.y * dpi.y / 180);
            const auto scale = zoom >= 1 ? std::floor(zoom) : zoom;
            const ImVec2 size{320 * scale / dpi.x, 180 * scale / dpi.y};
            const ImVec2 origin{
                std::floor((position.x + (available.x - size.x) * 0.5f) * dpi.x) / dpi.x,
                std::floor((position.y + (available.y - size.y) * 0.5f) * dpi.y) / dpi.y
            };
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end{origin.x + size.x, origin.y + size.y};
            draw->AddRectFilled(position, {position.x + available.x, position.y + available.y}, IM_COL32(28, 28, 28, 255));
            draw->AddImage(sceneTexture, origin, end);
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
            if (entity && tool_ != Tool::hand && !orbiting_) {
                const ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
                const float step = tool_ == Tool::rotate ? 15.0f : 0.25f;
                const float snapValues[] = {step, step, step};
                const ge3d::Entity beforeManipulate = *entity;
                auto model = modelMatrix(entity->transform);
                if (ImGuizmo::Manipulate(glm::value_ptr(viewMatrix), glm::value_ptr(projectionMatrix),
                        operations[static_cast<int>(tool_)],
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
            draw->PopClipRect();

            if (hovered && !overGizmo && !overCube && !io.KeyAlt && tool_ != Tool::hand &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                pick(io.MousePos, origin, size);
            }
            ImGui::Dummy(available);
        }
    }
    ImGui::End();
    if (gizmoEdit_ && (!ImGuizmo::IsUsing() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        const char* labels[] = {"Mover", "Mover", "Rotar", "Escalar"};
        commitModification(gizmoEdit_, labels[static_cast<int>(tool_)]);
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
    if (isPlaying() && !paused_) ++playFrames_;

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
