#pragma once

#include "ge3d/Scene.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

class EditorView {
public:
    EditorView();
    void draw(ImTextureID sceneTexture);
    [[nodiscard]] glm::mat4 renderCamera() const;
    [[nodiscard]] const ge3d::Scene& scene() const { return scene_; }
    static glm::mat4 modelMatrix(const ge3d::Transform& transform);

private:
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection() const;
    void pick(ImVec2 mouse, ImVec2 origin, ImVec2 size);
    void focusSelection();

    ge3d::Scene scene_ = ge3d::makeStarterScene();
    ge3d::EntityId selected_{};
    glm::vec3 pivot_{0.0f, 0.35f, 0.0f};
    float yaw_ = 0.75f;
    float pitch_ = 0.48f;
    float distance_ = 7.0f;
    int tool_ = 0;
    bool localAxes_ = false;
    bool snap_ = false;
    bool orbiting_ = false;
    bool panning_ = false;
    bool layoutCreated_ = false;
};
