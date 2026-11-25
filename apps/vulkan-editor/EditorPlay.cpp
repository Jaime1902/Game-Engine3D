// Modo Play: física (Jolt), audio (miniaudio), jugador en tercera o primera persona y sus componentes en el
// Inspector (también el componente Modelo).
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

ge3d::EntityId EditorView::hiddenEntity() const {
    return firstPerson_ && !thirdPerson_ && physics_ && physics_->hasCharacter() ? physics_->characterEntity() : ge3d::EntityId{};
}

// Tercera persona: la cámara mira a la cabeza del jugador desde detrás (según la mirada del ratón). Si hay
// una pared en medio se acerca al instante (nunca la atraviesa) y vuelve a alejarse poco a poco.
void EditorView::updateThirdPersonCamera(float dt) {
    const auto state = physics_->character();
    const auto* player = scene_.find(physics_->characterEntity());
    const float height = player && player->player ? player->player->height : 1.75f;
    tpHead_ = toGlm(state.feet) + glm::vec3{0.0f, height * 1.05f, 0.0f};   // algo por encima: el personaje queda bajo el centro
    const glm::vec3 back = -fpForward();
    float wanted = tpDistance_;
    if (const auto hit = physics_->raycast(toVec(tpHead_), toVec(back), tpDistance_ + 0.3f)) {
        wanted = std::max(0.35f, hit->second - 0.3f);
    }
    tpCurrent_ = wanted < tpCurrent_ ? wanted : tpCurrent_ + (wanted - tpCurrent_) * std::min(1.0f, dt * 3.0f);
    fpEye_ = tpHead_ + back * tpCurrent_;
}

// --- Inicio y fin de Play -------------------------------------------------------------------------

void EditorView::startPlaySystems() {
    physics_ = std::make_unique<ge3d::PhysicsWorld>();
    physics_->setModelSource([this](const std::string& path) { return models_.cached(path); });
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
        thirdPerson_ = player && player->player && player->player->camera == ge3d::PlayerCamera::thirdPerson;
        fpPitch_ = thirdPerson_ ? -0.3f : 0.0f;
        fpEye_ = toGlm(physics_->character().eye);
        if (thirdPerson_) {
            tpDistance_ = tpCurrent_ = player->player->cameraDistance;
            updateThirdPersonCamera(0.0f);
        }
        mouseCaptured_ = true;
        log(std::string("Jugador: WASD para andar, raton para mirar") + (thirdPerson_ ? " (rueda: distancia)" : "") +
            ", Espacio salta, Shift corre, E o clic interactua, Esc suelta el raton");
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
    thirdPerson_ = false;
    mouseCaptured_ = false;
    scriptCursor_ = false;
}

// --- Un fotograma de juego ------------------------------------------------------------------------

void EditorView::updatePlay(float dt) {
    ge3d::CharacterInput input;
    glm::vec3 move{0.0f};
    if (firstPerson_ && mouseCaptured_) {
        // Mirar con el ratón (capturado por la ventana) y andar con WASD respecto a la mirada.
        const float sensitivity = glm::radians(scene_.find(physics_->characterEntity()) &&
                                                       scene_.find(physics_->characterEntity())->player
                                                   ? scene_.find(physics_->characterEntity())->player->mouseSensitivity
                                                   : 0.12f);
        fpYaw_ -= capturedDelta_.x * sensitivity;
        // Tercera persona: sin mirar tan arriba que la cámara se meta en el suelo.
        fpPitch_ = std::clamp(fpPitch_ - capturedDelta_.y * sensitivity, -1.5f, thirdPerson_ ? 0.6f : 1.5f);
        if (thirdPerson_ && ImGui::GetIO().MouseWheel != 0.0f) {
            tpDistance_ = std::clamp(tpDistance_ * std::pow(0.88f, ImGui::GetIO().MouseWheel), 1.0f, 12.0f);
        }
        const glm::vec3 forward{-std::sin(fpYaw_), 0.0f, -std::cos(fpYaw_)};
        const glm::vec3 right{std::cos(fpYaw_), 0.0f, -std::sin(fpYaw_)};
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
        if (thirdPerson_) {
            // El personaje gira (suave) hacia donde anda; la cámara no gira con él.
            if (auto* player = scene_.find(physics_->characterEntity()); player && glm::length(move) > 0.0f) {
                auto world = ge3d::worldTransform(scene_, *player);
                const float target = glm::degrees(std::atan2(-move.x, -move.z));
                const float difference = std::fmod(target - world.rotationDegrees.y + 540.0f, 360.0f) - 180.0f;
                world.rotationDegrees.y += difference * std::min(1.0f, dt * 12.0f);
                player->transform = ge3d::localTransform(scene_, player->parent, world);
            }
            updateThirdPersonCamera(dt);
        } else {
            fpEye_ = toGlm(state.eye);
        }
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
    // ui.cursor(true) suelta el ratón para la interfaz (menús, diálogos); ui.cursor(false) lo devuelve.
    if (const bool cursor = scripts_->wantsCursor(); cursor != scriptCursor_) {
        scriptCursor_ = cursor;
        if (firstPerson_) mouseCaptured_ = !cursor;
    }
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
    // Lo que hay delante del jugador, a menos de 3 m. En tercera persona, delante del personaje (hacia donde
    // mira) y, si no hay nada, lo que está en el centro de la cámara cerca de él.
    std::optional<std::pair<ge3d::EntityId, float>> hit;
    if (thirdPerson_) {
        if (const auto* player = scene_.find(physics_->characterEntity())) {
            const float yaw = glm::radians(ge3d::worldTransform(scene_, *player).rotationDegrees.y);
            const glm::vec3 facing{-std::sin(yaw), 0.0f, -std::cos(yaw)};
            hit = physics_->raycast(toVec(tpHead_ - glm::vec3{0.0f, 0.55f, 0.0f}), toVec(facing), 2.0f);
        }
        if (!hit) {
            hit = physics_->raycast(toVec(fpEye_), toVec(fpForward()), tpCurrent_ + 2.5f);
            if (hit && hit->second < tpCurrent_ - 0.2f) hit.reset();   // entre la cámara y el jugador
        }
    } else {
        hit = physics_->raycast(toVec(fpEye_), toVec(fpForward()), 3.0f);
    }
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
            // Punto de mira (en tercera persona no hace falta: se ve al personaje).
            const float r = 5.0f * scale_;
            if (!thirdPerson_) {
                draw->AddLine({center.x - r, center.y}, {center.x + r, center.y}, IM_COL32(255, 255, 255, 200), 2.0f);
                draw->AddLine({center.x, center.y - r}, {center.x, center.y + r}, IM_COL32(255, 255, 255, 200), 2.0f);
            }
            draw->AddText({origin.x + 8 * scale_, origin.y + size.y - ImGui::GetTextLineHeight() - 6 * scale_},
                          IM_COL32(255, 255, 255, 170),
                          thirdPerson_ ? "Esc: soltar el raton  |  Rueda: distancia  |  Ctrl+P: detener"
                                       : "Esc: soltar el raton  |  Ctrl+P: detener");
        } else if (!scriptCursor_) {   // con el ratón suelto por la interfaz del juego no hay aviso
            const char* text = "Clic en la vista para jugar";
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            draw->AddRectFilled({center.x - textSize.x * 0.5f - 10, center.y - textSize.y * 0.5f - 6},
                                {center.x + textSize.x * 0.5f + 10, center.y + textSize.y * 0.5f + 6}, IM_COL32(0, 0, 0, 160), 4.0f);
            draw->AddText({center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f}, IM_COL32(255, 255, 255, 255), text);
        }
        if (!thirdPerson_) return;
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
        // En tercera persona solo se dibuja la cápsula de un jugador que no tiene nada visible.
        if (firstPerson_ && !(entity.player && !entity.model && !entity.shape && ge3d::childrenOf(scene_, entity.id).empty())) continue;
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
        // Con el aldeano de ejemplo como cuerpo, si está en el proyecto (se cambia en su componente Modelo).
        if (std::find(modelList_.begin(), modelList_.end(), "models/aldeano.gltf") != modelList_.end()) {
            entity.model = ge3d::ModelComponent{"models/aldeano.gltf", ge3d::ColliderShape::none};
        }
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
    labelRow("Camara", width);
    int camera = static_cast<int>(player.camera);
    if (ImGui::Combo("##camara", &camera, "Tercera persona\0Primera persona\0")) {
        std::optional<ge3d::Entity> before = frameStart;
        player.camera = static_cast<ge3d::PlayerCamera>(camera);
        commitModification(before, "Camara del jugador");
    }
    if (player.camera == ge3d::PlayerCamera::thirdPerson) {
        editFloat("Distancia", player.cameraDistance, 0.05f, 1.0f, 12.0f, "%.1f m", frameStart, width);
    }
    ImGui::TextDisabled("En Play: WASD, raton, Espacio, Shift, E o clic.");
    ImGui::TextDisabled("La flecha verde indica hacia donde mira (rotacion Y).");
}

void EditorView::drawModelComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& model = *entity.model;
    const float width = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    labelRow("Archivo", width);
    const auto slash = model.path.find_last_of('/');
    const std::string shown = model.path.empty() ? "(ninguno)" : model.path.substr(slash == std::string::npos ? 0 : slash + 1);
    if (ImGui::BeginCombo("##archivo", shown.c_str())) {
        for (const auto& path : modelList_) {
            if (ImGui::Selectable(path.c_str(), path == model.path) && path != model.path) {
                std::optional<ge3d::Entity> before = frameStart;
                model.path = path;
                commitModification(before, "Cambiar modelo");
            }
        }
        if (modelList_.empty()) ImGui::TextDisabled("No hay glTF en Content/models");
        ImGui::EndCombo();
    }
    labelRow("Colisionador", width);
    int collider = static_cast<int>(model.collider);
    if (ImGui::Combo("##colisionador", &collider, "Ninguno\0Caja\0Malla exacta\0")) {
        std::optional<ge3d::Entity> before = frameStart;
        model.collider = static_cast<ge3d::ColliderShape>(collider);
        commitModification(before, "Colisionador del modelo");
    }
    static constexpr const char* hints[] = {"Decoracion: no choca.", "La caja que envuelve el modelo.",
                                            "Choca con su forma exacta (con Cuerpo dinamico: su envolvente convexa)."};
    ImGui::TextDisabled("%s", hints[std::clamp(collider, 0, 2)]);
    if (model.path.empty()) return;
    if (const auto data = models_.cached(model.path)) {
        const glm::vec3 size = toGlm(data->boundsMax) - toGlm(data->boundsMin);
        ImGui::TextDisabled("%zu triangulos, %zu material%s, %zu textura%s", data->triangleCount(), data->materials.size(),
                            data->materials.size() == 1 ? "" : "es", data->textures.size(), data->textures.size() == 1 ? "" : "s");
        ImGui::TextDisabled("Tamano: %.2f x %.2f x %.2f m", size.x, size.y, size.z);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.45f, 0.4f, 1.0f});
        ImGui::TextWrapped("%s", models_.lastError(model.path).c_str());
        ImGui::PopStyleColor();
    }
}

void EditorView::createModelEntity(const std::string& asset, std::optional<glm::vec3> position, ge3d::EntityId parent) {
    if (editInProgress()) return;
    // Nombre a partir del archivo: "models/casita.gltf" -> "Casita".
    auto name = asset.substr(asset.find_last_of('/') == std::string::npos ? 0 : asset.find_last_of('/') + 1);
    name = name.substr(0, name.find_last_of('.'));
    if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') name[0] = static_cast<char>(name[0] - 'a' + 'A');
    ge3d::Transform transform;
    if (parent && scene_.find(parent)) {
        if (position) {
            ge3d::Transform world;
            world.position = toVec(*position);
            transform = ge3d::localTransform(scene_, parent, world);
        }
    } else {
        parent = 0;
        const glm::vec3 at = position.value_or(glm::vec3{pivot_.x, std::max(0.0f, pivot_.y), pivot_.z});
        transform.position = toVec(at);
    }
    auto entity = scene_.makeEntity(name.empty() ? "Modelo" : name, transform);
    entity.parent = parent;
    entity.model = ge3d::ModelComponent{asset};
    const auto id = entity.id;
    execute(ge3d::makeCreateCommand(scene_, std::move(entity)));
    select(id);
    if (!models_.cached(asset)) log("Modelo: " + models_.lastError(asset), true);
}

// --- Interfaz de los scripts ----------------------------------------------------------------------

void EditorView::drawScriptUi(ImDrawList* draw, ImVec2 origin, ImVec2 size) {
    if (!scripts_ || !scripts_->running() || sceneSize_.x == 0) return;
    const auto commands = scripts_->uiCommands();
    if (commands.empty()) return;
    // Un píxel de la interfaz = un píxel de la escena: todo cae en la rejilla de la imagen.
    const float ratio = size.x / static_cast<float>(sceneSize_.x);
    const auto at = [&](float x, float y) { return ImVec2{origin.x + std::floor(x) * ratio, origin.y + std::floor(y) * ratio}; };
    const auto box = [&](float x, float y, float w, float h, ImU32 color) {
        if (w > 0 && h > 0) draw->AddRectFilled(at(x, y), at(x + w, y + h), color);
    };
    // Recuadro pixel art: relleno y borde de 1 píxel sin las esquinas.
    const auto panel = [&](const ge3d::UiCommand& c, ImU32 fill, ImU32 border) {
        box(c.x + 1, c.y + 1, c.w - 2, c.h - 2, fill);
        box(c.x + 1, c.y, c.w - 2, 1, border);
        box(c.x + 1, c.y + c.h - 1, c.w - 2, 1, border);
        box(c.x, c.y + 1, 1, c.h - 2, border);
        box(c.x + c.w - 1, c.y + 1, 1, c.h - 2, border);
    };
    const auto text = [&](const std::string& value, float x, float y, int scale, ImU32 color) {
        const float pixels = ge3d::uiLineHeight * static_cast<float>(scale) * ratio;
        const int k = static_cast<int>(std::lround(pixels / 13.0f));
        ImFont* font = uiFonts_[static_cast<std::size_t>(std::clamp(k, 1, static_cast<int>(uiFonts_.size())) - 1)];
        // Sombra de 1 píxel para que se lea sobre cualquier fondo.
        const ImU32 alpha = (color >> IM_COL32_A_SHIFT) & 0xFFu;
        const ImVec2 shadow = at(x + static_cast<float>(scale), y + static_cast<float>(scale));
        draw->AddText(font, pixels, shadow, IM_COL32(0, 0, 0, alpha * 3 / 5), value.c_str());
        draw->AddText(font, pixels, at(x, y), color, value.c_str());
    };
    for (const auto& c : commands) {
        switch (c.kind) {
        case ge3d::UiCommand::Kind::rect: box(c.x, c.y, c.w, c.h, c.color); break;
        case ge3d::UiCommand::Kind::panel: panel(c, c.color, c.background); break;
        case ge3d::UiCommand::Kind::text: text(c.text, c.x, c.y, c.scale, c.color); break;
        case ge3d::UiCommand::Kind::bar:
            box(c.x, c.y, c.w, c.h, c.background);
            box(c.x, c.y, std::round(c.w * c.value), c.h, c.color);
            break;
        case ge3d::UiCommand::Kind::button: {
            const ImU32 fill = c.state == 2 ? IM_COL32(29, 32, 53, 240) : c.state == 1 ? IM_COL32(64, 72, 120, 240) : IM_COL32(42, 47, 74, 235);
            panel(c, fill, c.state ? IM_COL32(255, 214, 110, 255) : IM_COL32(244, 244, 244, 255));
            const float width = ge3d::uiTextWidth(c.text);
            const float pressed = c.state == 2 ? 1.0f : 0.0f;
            text(c.text, c.x + std::floor((c.w - width) * 0.5f), c.y + std::floor((c.h - ge3d::uiLineHeight) * 0.5f) + pressed, 1, c.color);
            break;
        }
        }
    }
}
