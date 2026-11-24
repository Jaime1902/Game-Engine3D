#include "EditorView.hpp"

#include "ge3d/Primitives.hpp"
#include <ImGuizmo.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float aspect = 320.0f / 180.0f;
glm::vec3 vector(ge3d::Vec3 v) { return {v.x, v.y, v.z}; }

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

void editVector(const char* label, ge3d::Vec3& value, float speed, float minimum = 0, float maximum = 0) {
    float values[] = {value.x, value.y, value.z};
    if (ImGui::DragFloat3(label, values, speed, minimum, maximum, "%.2f",
                          ImGuiSliderFlags_AlwaysClamp)) {
        value = {values[0], values[1], values[2]};
    }
}
}

EditorView::EditorView() {
    auto& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowPadding = {12, 10};
    style.FramePadding = {8, 5};
    style.ItemSpacing = {8, 8};
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = {0.071f, 0.086f, 0.11f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.11f, 0.15f, 0.20f, 1};
    style.Colors[ImGuiCol_ButtonActive] = {0.15f, 0.42f, 0.45f, 1};
    style.Colors[ImGuiCol_Header] = {0.15f, 0.33f, 0.36f, 1};
}

glm::mat4 EditorView::view() const {
    const glm::vec3 orbit{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
                          std::cos(yaw_) * std::cos(pitch_)};
    return glm::lookAtRH(pivot_ + orbit * distance_, pivot_, {0, 1, 0});
}

glm::mat4 EditorView::projection() const {
    return glm::perspectiveRH_NO(glm::radians(48.0f), aspect, 0.1f, 150.0f);
}

glm::mat4 EditorView::renderCamera() const {
    glm::mat4 clip{1.0f};
    clip[1][1] = -1;
    clip[2][2] = 0.5f;
    clip[3][2] = 0.5f;
    return clip * projection() * view();
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
        distance_ = std::clamp(std::max({scale.x, scale.y, scale.z}) * 3.2f, 2.0f, 60.0f);
    }
}

void EditorView::draw(ImTextureID sceneTexture) {
    const auto dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (!layoutCreated_) {
        layoutCreated_ = true;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
        ImGuiID center = dock;
        const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
        const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.23f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Jerarquia", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Escena", center);
        ImGui::DockBuilderFinish(dock);
    }

    if (ImGui::Begin("Jerarquia")) {
        for (const auto& entity : scene_.entities()) {
            ImGui::PushID(static_cast<int>(entity.id));
            if (ImGui::Selectable(entity.name.c_str(), selected_ == entity.id)) selected_ = entity.id;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focusSelection();
            ImGui::PopID();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Inspector")) {
        if (auto* entity = scene_.find(selected_)) {
            ImGui::TextUnformatted(entity->name.c_str());
            ImGui::SeparatorText("Transformacion");
            ImGui::PushID(static_cast<int>(entity->id));
            editVector("Posicion", entity->transform.position, 0.05f);
            editVector("Rotacion", entity->transform.rotationDegrees, 0.5f);
            editVector("Escala", entity->transform.scale, 0.03f, 0.01f, 250.0f);
            ImGui::PopID();
        } else {
            ImGui::TextDisabled("Selecciona un objeto");
        }
    }
    ImGui::End();

    if (ImGui::Begin("Escena", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const char* tools[] = {"Mover", "Rotar", "Escalar"};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            if (i == tool_) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            const bool pressed = ImGui::Button(tools[i]);
            if (i == tool_) ImGui::PopStyleColor();
            if (pressed) tool_ = i;
        }
        ImGui::SameLine();
        if (ImGui::Button(localAxes_ ? "Local" : "Global")) localAxes_ = !localAxes_;
        ImGui::SameLine();
        ImGui::Checkbox("Ajuste", &snap_);
        ImGui::SameLine();
        if (ImGui::Button("Enfocar")) focusSelection();

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
            draw->AddRectFilled(position, {position.x + available.x, position.y + available.y}, IM_COL32(8, 11, 17, 255));
            draw->AddImage(sceneTexture, origin, end);
            const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);
            orbiting_ = (orbiting_ || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) &&
                         ImGui::IsMouseDown(ImGuiMouseButton_Right);
            panning_ = (panning_ || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) &&
                        ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            if (orbiting_) {
                yaw_ -= io.MouseDelta.x * 0.008f;
                pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.008f, -1.48f, 1.48f);
            }
            if (panning_) {
                const auto inverseView = glm::inverse(view());
                pivot_ += (-glm::vec3(inverseView[0]) * io.MouseDelta.x +
                            glm::vec3(inverseView[1]) * io.MouseDelta.y) * distance_ * 0.0018f;
            }
            if (hovered) distance_ = std::clamp(distance_ * std::pow(0.87f, io.MouseWheel), 0.5f, 90.0f);
            if (ImGui::IsWindowFocused() && !io.WantTextInput && !orbiting_) {
                if (ImGui::IsKeyPressed(ImGuiKey_W)) tool_ = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) tool_ = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) tool_ = 2;
                if (ImGui::IsKeyPressed(ImGuiKey_F)) focusSelection();
            }

            bool overGizmo = false;
            if (auto* entity = scene_.find(selected_)) {
                draw->PushClipRect(origin, end, true);
                ImGuizmo::SetDrawlist(draw);
                ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
                const ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
                const float step = tool_ == 1 ? 15.0f : 0.25f;
                const float snapValues[] = {step, step, step};
                auto model = modelMatrix(entity->transform);
                if (ImGuizmo::Manipulate(glm::value_ptr(view()), glm::value_ptr(projection()), operations[tool_],
                        localAxes_ || tool_ == 2 ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                        glm::value_ptr(model), nullptr, snap_ ? snapValues : nullptr)) {
                    float translation[3], rotation[3], scaling[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scaling);
                    entity->transform = {
                        {translation[0], translation[1], translation[2]},
                        {rotation[0], rotation[1], rotation[2]},
                        {std::max(0.01f, scaling[0]), std::max(0.01f, scaling[1]), std::max(0.01f, scaling[2])}
                    };
                }
                overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
                draw->PopClipRect();
            }
            if (hovered && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                pick(io.MousePos, origin, size);
            }
            ImGui::Dummy(available);
        }
    }
    ImGui::End();
}
