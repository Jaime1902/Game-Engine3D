// Modo Play: física (Jolt), audio (miniaudio), jugador en primera persona y sus componentes en el Inspector.
#include "EditorLog.hpp"
#include "EditorView.hpp"
#include "ge3d/SceneIO.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

glm::vec3 toGlm(ge3d::Vec3 v) {
    return {v.x, v.y, v.z};
}

ge3d::Vec3 toVec(glm::vec3 v) {
    return {v.x, v.y, v.z};
}

const char* callbackFor(ge3d::PhysicsEvent::Kind kind) {
    switch (kind) {
    case ge3d::PhysicsEvent::Kind::triggerEnter: return "onTriggerEnter";
    case ge3d::PhysicsEvent::Kind::triggerExit: return "onTriggerExit";
    case ge3d::PhysicsEvent::Kind::collision: return "onCollision";
    }
    return "onCollision";
}

}

// --- Cámara en primera persona --------------------------------------------------------------------

glm::vec3 EditorView::fpForward() const {
    return {-std::sin(fpYaw_) * std::cos(fpPitch_), std::sin(fpPitch_), -std::cos(fpYaw_) * std::cos(fpPitch_)};
}

// --- Inicio y fin de Play -------------------------------------------------------------------------

void EditorView::startPlaySystems() {
    physics_ = std::make_unique<ge3d::PhysicsWorld>();
    physics_->build(scene_);
    if (!audio_) {
        audio_ = std::make_unique<ge3d::AudioSystem>(contentRoot_, [](bool error, const std::string& message) {
            editorLog().add(error ? LogLevel::warning : LogLevel::info, message);
        });
    }
    // Los scripts usan la física y el audio de este Play.
    ge3d::ScriptServices services;
    services.velocity = [this](ge3d::EntityId id) { return physics_ ? physics_->velocity(id) : std::nullopt; };
    services.setVelocity = [this](ge3d::EntityId id, ge3d::Vec3 v) { if (physics_) physics_->setVelocity(id, v); };
    services.applyImpulse = [this](ge3d::EntityId id, ge3d::Vec3 v) { if (physics_) physics_->applyImpulse(id, v); };
    services.playSound = [this](const std::string& clip, float volume, std::optional<ge3d::Vec3> position) {
        if (audio_) audio_->playOneShot(clip, volume, position);
    };
    services.emitter = [this](ge3d::EntityId id, bool play) {
        if (!audio_) return;
        if (play) audio_->playEmitter(id);
        else audio_->stopEmitter(id);
    };
    services.player = [this] { return physics_ && physics_->hasCharacter() ? physics_->characterEntity() : ge3d::EntityId{}; };
    // scene.spawn / entity:destroy: cambian la copia de Play directamente (no son ediciones que deshacer).
    services.spawn = [this](const std::string& asset, ge3d::Vec3 position) {
        const auto* content = prefabContent(asset);
        if (!content) return ge3d::EntityId{};
        ge3d::Transform transform = ge3d::prefabRoot(*content)->transform;
        transform.position = position;
        ge3d::EntityId root{};
        ge3d::History temporary;
        temporary.execute(scene_, ge3d::makeInstantiateCommand(scene_, *content, asset, transform, 0, &root));
        return root;
    };
    services.destroy = [this](ge3d::EntityId id) {
        if (auto command = ge3d::makeDeleteCommand(scene_, id)) {
            ge3d::History temporary;
            temporary.execute(scene_, std::move(*command));
        }
    };
    scripts_->setServices(std::move(services));

    firstPerson_ = physics_->hasCharacter();
    if (firstPerson_) {
        const auto* player = scene_.find(physics_->characterEntity());
        fpYaw_ = player ? glm::radians(ge3d::worldTransform(scene_, *player).rotationDegrees.y) : 0.0f;
        fpPitch_ = 0.0f;
        fpEye_ = toGlm(physics_->character().eye);
        mouseCaptured_ = true;
        log("Jugador: WASD para andar, raton para mirar, Espacio salta, Shift corre, E o clic interactua, Esc suelta el raton");
    }
    // Todos los WAV del proyecto decodificados de antemano: un script que pide un sonido no espera al disco.
    audio_->preload(soundList_);
    audio_->start(scene_);
    const auto stats = physics_->stats();
    log("Fisica: " + std::to_string(stats.bodies) + " cuerpos (" + std::to_string(stats.dynamic) + " dinamicos)");
}

void EditorView::stopPlaySystems() {
    if (audio_) audio_->stop();
    scripts_->setServices({});
    physics_.reset();
    firstPerson_ = false;
    mouseCaptured_ = false;
}

// --- Un fotograma de juego ------------------------------------------------------------------------

void EditorView::updatePlay(float dt) {
    ge3d::CharacterInput input;
    if (firstPerson_ && mouseCaptured_) {
        // Mirar con el ratón (capturado por la ventana) y andar con WASD respecto a la mirada.
        const float sensitivity = glm::radians(scene_.find(physics_->characterEntity()) &&
                                                       scene_.find(physics_->characterEntity())->player
                                                   ? scene_.find(physics_->characterEntity())->player->mouseSensitivity
                                                   : 0.12f);
        fpYaw_ -= capturedDelta_.x * sensitivity;
        fpPitch_ = std::clamp(fpPitch_ - capturedDelta_.y * sensitivity, -1.5f, 1.5f);
        const glm::vec3 forward{-std::sin(fpYaw_), 0.0f, -std::cos(fpYaw_)};
        const glm::vec3 right{std::cos(fpYaw_), 0.0f, -std::sin(fpYaw_)};
        glm::vec3 move{0.0f};
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
        if (glm::length(move) > 0.0f) move = glm::normalize(move);
        input.move = toVec(move);
        input.jump = ImGui::IsKeyDown(ImGuiKey_Space);
        input.run = ImGui::IsKeyDown(ImGuiKey_LeftShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) mouseCaptured_ = false;
        if (capturedClick_ || ImGui::IsKeyPressed(ImGuiKey_E, false)) interactAhead();
    }
    capturedDelta_ = {0.0f, 0.0f};
    capturedClick_ = false;

    const bool wasGrounded = physics_->hasCharacter() && physics_->character().grounded;
    physics_->step(scene_, dt, input);

    // Sucesos de física -> scripts (y sonidos de golpes fuertes).
    for (const auto& event : physics_->takeEvents()) {
        scripts_->notify(scene_, event.self, callbackFor(event.kind), event.other, event.speed);
        if (event.kind == ge3d::PhysicsEvent::Kind::collision && event.speed > 2.5f && event.self < event.other) {
            if (const auto* entity = scene_.find(event.self)) {
                audio_->playOneShot("sounds/golpe.wav", std::min(1.0f, event.speed / 8.0f), ge3d::worldTransform(scene_, *entity).position);
            }
        }
    }

    if (physics_->hasCharacter()) {
        const auto state = physics_->character();
        fpEye_ = toGlm(state.eye);
        // Pasos y salto.
        const float horizontal = std::hypot(state.velocity.x, state.velocity.z);
        if (state.grounded && horizontal > 1.0f) {
            footstepTimer_ += dt;
            if (footstepTimer_ > (input.run ? 0.32f : 0.45f)) {
                footstepTimer_ = 0.0f;
                audio_->playOneShot("sounds/paso.wav", 0.35f, std::nullopt);
            }
        }
        if (wasGrounded && input.jump && state.velocity.y > 1.0f) audio_->playOneShot("sounds/salto.wav", 0.4f, std::nullopt);
    }

    scripts_->update(scene_, dt, gatherInput());
    simulateWater(dt);

    ge3d::AudioSystem::Listener listener;
    if (firstPerson_) {
        listener.position = toVec(fpEye_);
        listener.forward = toVec(fpForward());
    } else {
        const glm::vec3 eye = cameraPosition();
        listener.position = toVec(eye);
        listener.forward = toVec(glm::normalize(pivot_ - eye));
    }
    audio_->update(scene_, listener);
}

void EditorView::interactAhead() {
    // Lo que hay delante del jugador, a menos de 3 m.
    const auto hit = physics_->raycast(toVec(fpEye_), toVec(fpForward()), 3.0f);
    if (!hit) return;
    if (!scripts_->interact(scene_, hit->first)) {
        if (const auto* entity = scene_.find(hit->first)) log("Nada que hacer con " + entity->name);
    }
}

// --- Dibujo en la vista -----------------------------------------------------------------------------

void EditorView::drawPlayOverlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) {
    const ImVec2 center{origin.x + size.x * 0.5f, origin.y + size.y * 0.5f};
    if (firstPerson_) {
        if (mouseCaptured_) {
            // Punto de mira.
            const float r = 5.0f * scale_;
            draw->AddLine({center.x - r, center.y}, {center.x + r, center.y}, IM_COL32(255, 255, 255, 200), 2.0f);
            draw->AddLine({center.x, center.y - r}, {center.x, center.y + r}, IM_COL32(255, 255, 255, 200), 2.0f);
            draw->AddText({origin.x + 8 * scale_, origin.y + size.y - ImGui::GetTextLineHeight() - 6 * scale_},
                          IM_COL32(255, 255, 255, 170), "Esc: soltar el raton  |  Ctrl+P: detener");
        } else {
            const char* text = "Clic en la vista para jugar";
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            draw->AddRectFilled({center.x - textSize.x * 0.5f - 10, center.y - textSize.y * 0.5f - 6},
                                {center.x + textSize.x * 0.5f + 10, center.y + textSize.y * 0.5f + 6}, IM_COL32(0, 0, 0, 160), 4.0f);
            draw->AddText({center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f}, IM_COL32(255, 255, 255, 255), text);
        }
        return;
    }
    // Edición (o Play sin jugador): lo que no tiene malla visible se dibuja como guía.
    const glm::mat4 clip = projection() * view();
    const auto project = [&](glm::vec3 p, ImVec2& out) {
        const glm::vec4 c = clip * glm::vec4(p, 1.0f);
        if (c.w <= 0.01f) return false;
        out = {origin.x + (c.x / c.w * 0.5f + 0.5f) * size.x, origin.y + (0.5f - c.y / c.w * 0.5f) * size.y};
        return true;
    };
    const auto circle = [&](glm::vec3 c, float radius, ImU32 color) {
        ImVec2 points[25];
        int count = 0;
        for (int i = 0; i <= 24; ++i) {
            const float a = static_cast<float>(i) / 24.0f * 6.2831853f;
            if (project(c + glm::vec3{std::cos(a) * radius, 0, std::sin(a) * radius}, points[count])) ++count;
        }
        if (count > 2) draw->AddPolyline(points, count, color, 0, 1.5f);
    };
    for (const auto& entity : scene_.entities()) {
        const bool selected = entity.id == selected_;
        const auto world = ge3d::worldTransform(scene_, entity);
        const glm::vec3 p = toGlm(world.position);
        if (entity.player) {
            const float h = entity.player->height, r = entity.player->radius;
            const ImU32 color = selected ? IM_COL32(120, 255, 140, 255) : IM_COL32(90, 200, 110, 200);
            circle(p, r, color);
            circle(p + glm::vec3{0, h, 0}, r, color);
            for (int i = 0; i < 4; ++i) {
                const float a = static_cast<float>(i) * 1.5707963f;
                ImVec2 a0, a1;
                const glm::vec3 offset{std::cos(a) * r, 0, std::sin(a) * r};
                if (project(p + offset, a0) && project(p + offset + glm::vec3{0, h, 0}, a1)) draw->AddLine(a0, a1, color, 1.5f);
            }
            // Hacia dónde mira al empezar.
            const float yaw = glm::radians(world.rotationDegrees.y);
            ImVec2 e0, e1;
            if (project(p + glm::vec3{0, h * 0.92f, 0}, e0) &&
                project(p + glm::vec3{-std::sin(yaw) * 0.8f, h * 0.92f, -std::cos(yaw) * 0.8f}, e1)) {
                draw->AddLine(e0, e1, color, 2.0f);
            }
        }
        if (entity.body && entity.body->type == ge3d::BodyType::trigger) {
            drawPieceOutline(draw, ge3d::Primitive::box, worldMatrix(entity), origin, size,
                             selected ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 190, 60, 170), IM_COL32(230, 190, 60, selected ? 40 : 20));
        }
        if (entity.audio) {
            const ImU32 color = selected ? IM_COL32(120, 190, 255, 255) : IM_COL32(100, 160, 230, 170);
            ImVec2 c;
            if (project(p, c)) {
                draw->AddCircleFilled(c, 4.0f * scale_, color);
                // Ondas de un altavoz.
                draw->AddCircle(c, 8.0f * scale_, color, 12, 1.2f);
            }
            if (selected && entity.audio->spatial) {
                circle(p, entity.audio->minDistance, IM_COL32(120, 190, 255, 200));
                circle(p, entity.audio->maxDistance, IM_COL32(120, 190, 255, 110));
            }
        }
    }
}

// --- Crear objetos especiales ---------------------------------------------------------------------

void EditorView::createSpecial(Special kind) {
    const ge3d::Vec3 at{std::round(pivot_.x), std::max(0.0f, std::round(pivot_.y)), std::round(pivot_.z)};
    ge3d::Entity entity;
    if (kind == Special::player) {
        const bool exists = std::any_of(scene_.entities().begin(), scene_.entities().end(),
                                        [](const ge3d::Entity& e) { return e.player.has_value(); });
        if (exists) log("Ya hay un Jugador en la escena: en Play se usa el primero", true);
        entity = scene_.makeEntity("Jugador", {at, {}, {1, 1, 1}});
        entity.player = ge3d::PlayerComponent{};
    } else if (kind == Special::zone) {
        entity = scene_.makeEntity("Zona", {{at.x, at.y + 1.0f, at.z}, {}, {3, 2, 3}});
        entity.body = ge3d::BodyComponent{ge3d::BodyType::trigger};
    } else {
        entity = scene_.makeEntity("Sonido", {{at.x, at.y + 0.5f, at.z}, {}, {1, 1, 1}});
        entity.audio = ge3d::AudioSourceComponent{soundList_.empty() ? std::string() : soundList_.front()};
    }
    const auto id = entity.id;
    execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
    selected_ = id;
}

// --- Inspector ------------------------------------------------------------------------------------

namespace {

// Fila "etiqueta + control" con el ancho del Inspector.
void labelRow(const char* label, float width) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(width);
    ImGui::SetNextItemWidth(-1);
}

}

// Un control que se arrastra es una sola operación de deshacer; uno que se pulsa, también.
bool EditorView::editFloat(const char* label, float& value, float speed, float minimum, float maximum, const char* format,
                           const ge3d::Entity& frameStart, float labelWidth) {
    labelRow(label, labelWidth);
    ImGui::PushID(label);
    const bool changed = ImGui::DragFloat("##v", &value, speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemActivated() && !inspectorEdit_) inspectorEdit_ = frameStart;
    if (ImGui::IsItemDeactivatedAfterEdit()) commitModification(inspectorEdit_, label);
    ImGui::PopID();
    return changed;
}

bool EditorView::editBool(const char* label, bool& value, const ge3d::Entity& frameStart, float labelWidth) {
    labelRow(label, labelWidth);
    ImGui::PushID(label);
    const bool changed = ImGui::Checkbox("##v", &value);
    if (changed) {
        std::optional<ge3d::Entity> before = frameStart;
        commitModification(before, label);
    }
    ImGui::PopID();
    return changed;
}

void EditorView::drawBodyComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& body = *entity.body;
    const float width = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    labelRow("Tipo", width);
    int type = static_cast<int>(body.type);
    if (ImGui::Combo("##tipo", &type, "Dinamico\0Cinematico\0Zona (trigger)\0")) {
        std::optional<ge3d::Entity> before = frameStart;
        body.type = static_cast<ge3d::BodyType>(type);
        commitModification(before, "Tipo de cuerpo");
    }
    static constexpr const char* hints[] = {"Cae, choca, se puede empujar y flota en el agua.",
                                            "Lo mueve un script; empuja a los dinamicos.",
                                            "No choca: avisa con onTriggerEnter / onTriggerExit."};
    ImGui::TextDisabled("%s", hints[static_cast<int>(body.type)]);
    if (body.type == ge3d::BodyType::dynamic) {
        editFloat("Masa", body.mass, 0.5f, 0.0f, 100000.0f, body.mass > 0.0f ? "%.1f kg" : "automatica", frameStart, width);
        editFloat("Friccion", body.friction, 0.01f, 0.0f, 2.0f, "%.2f", frameStart, width);
        editFloat("Rebote", body.restitution, 0.01f, 0.0f, 1.0f, "%.2f", frameStart, width);
    }
    if (!entity.shape) ImGui::TextDisabled("Sin Forma: usa una caja del tamano de la escala.");
}

void EditorView::drawAudioComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& audio = *entity.audio;
    const float width = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    labelRow("Sonido", width);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    const auto slash = audio.clip.find_last_of('/');
    const std::string shown = audio.clip.empty() ? "(ninguno)" : audio.clip.substr(slash == std::string::npos ? 0 : slash + 1);
    if (ImGui::BeginCombo("##sonido", shown.c_str())) {
        for (const auto& clip : soundList_) {
            if (ImGui::Selectable(clip.c_str(), clip == audio.clip) && clip != audio.clip) {
                std::optional<ge3d::Entity> before = frameStart;
                audio.clip = clip;
                commitModification(before, "Cambiar sonido");
            }
        }
        if (soundList_.empty()) ImGui::TextDisabled("No hay WAV en Content/sounds");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(audio.clip.empty());
    if (ImGui::Button(">", {ImGui::GetFrameHeight(), 0})) {
        if (!audio_) {
            audio_ = std::make_unique<ge3d::AudioSystem>(contentRoot_, [](bool error, const std::string& message) {
                editorLog().add(error ? LogLevel::warning : LogLevel::info, message);
            });
        }
        audio_->playOneShot(audio.clip, audio.volume, std::nullopt);
        audio_->flush();
    }
    ImGui::SetItemTooltip("Escuchar");
    ImGui::EndDisabled();
    editFloat("Volumen", audio.volume, 0.01f, 0.0f, 4.0f, "%.2f", frameStart, width);
    editBool("En bucle", audio.loop, frameStart, width);
    editBool("Al empezar", audio.playOnStart, frameStart, width);
    editBool("3D", audio.spatial, frameStart, width);
    if (audio.spatial) {
        editFloat("Distancia min.", audio.minDistance, 0.1f, 0.01f, audio.maxDistance, "%.1f m", frameStart, width);
        editFloat("Distancia max.", audio.maxDistance, 0.1f, audio.minDistance, 10000.0f, "%.1f m", frameStart, width);
        ImGui::TextDisabled("A volumen completo hasta la min.; deja de oirse en la max.");
    }
}

void EditorView::drawPlayerComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& player = *entity.player;
    const float width = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    editFloat("Altura", player.height, 0.01f, std::max(0.5f, player.radius * 2.0f + 0.1f), 10.0f, "%.2f m", frameStart, width);
    editFloat("Radio", player.radius, 0.01f, 0.05f, player.height * 0.45f, "%.2f m", frameStart, width);
    editFloat("Andar", player.walkSpeed, 0.05f, 0.0f, 50.0f, "%.1f m/s", frameStart, width);
    editFloat("Correr", player.runSpeed, 0.05f, 0.0f, 50.0f, "%.1f m/s", frameStart, width);
    editFloat("Salto", player.jumpSpeed, 0.05f, 0.0f, 50.0f, "%.1f m/s", frameStart, width);
    editFloat("Sensibilidad", player.mouseSensitivity, 0.002f, 0.01f, 1.0f, "%.3f", frameStart, width);
    ImGui::TextDisabled("En Play: WASD, raton, Espacio, Shift, E o clic.");
    ImGui::TextDisabled("La flecha verde indica hacia donde mira (rotacion Y).");
}
