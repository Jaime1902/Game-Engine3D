// Scripts en el editor: pestaña Código, componente Script del Inspector y archivos de scripts.
#include "EditorLog.hpp"
#include "EditorView.hpp"
#include "ge3d/SceneIO.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

const ImVec4 okColor{0.45f, 0.80f, 0.45f, 1.0f};
const ImVec4 errorText{1.0f, 0.42f, 0.38f, 1.0f};

std::string fileName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Acceso del callback de ImGui al documento (el callback es una función libre).
struct CodeCallbackState {
    std::string* text;
    int* jumpLine;
    int* cursorLine;
};

int codeCallback(ImGuiInputTextCallbackData* data) {
    auto* state = static_cast<CodeCallbackState*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        state->text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = state->text->data();
        return 0;
    }
    if (*state->jumpLine > 0) {
        // Lleva el cursor al principio de la línea pedida (desde un error o el Inspector).
        int line = 1, position = 0;
        while (position < data->BufTextLen && line < *state->jumpLine) {
            if (data->Buf[position] == '\n') ++line;
            ++position;
        }
        data->CursorPos = data->SelectionStart = data->SelectionEnd = position;
        *state->jumpLine = 0;
    }
    int line = 0;
    for (int i = 0; i < data->CursorPos && i < data->BufTextLen; ++i) {
        if (data->Buf[i] == '\n') ++line;
    }
    *state->cursorLine = line;
    return 0;
}

}

// --- Archivos ------------------------------------------------------------------------------------

std::filesystem::path EditorView::scriptFile(const std::string& path) const {
    return contentRoot_ / ge3d::pathFromUtf8(path);
}

std::optional<std::string> EditorView::readScript(const std::string& path) const {
    if (!ge3d::isValidScriptPath(path)) return std::nullopt;
    // Si el script está abierto en Código con cambios sin guardar, Play usa lo guardado en disco.
    try {
        return ge3d::readTextFile(scriptFile(path));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const ge3d::ScriptInfo* EditorView::scriptInfo(const std::string& path) {
    if (!ge3d::isValidScriptPath(path)) return nullptr;
    std::error_code error;
    const auto time = std::filesystem::last_write_time(scriptFile(path), error);
    if (error) return nullptr;
    auto& entry = scriptCache_[path];
    if (entry.time != time) {
        const auto source = readScript(path);
        entry.time = time;
        entry.info = source ? ge3d::describeScript(*source, path) : ge3d::ScriptInfo{};
    }
    return &entry.info;
}

void EditorView::watchScripts(bool force) {
    const double now = ImGui::GetTime();
    if (!force && now - lastScriptWatch_ < 1.0) return;
    lastScriptWatch_ = now;
    scriptList_.clear();
    std::error_code error;
    const auto folder = contentRoot_ / "scripts";
    if (std::filesystem::is_directory(folder, error)) {
        for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (!it->is_regular_file(error) || it->path().extension() != ".luau") continue;
            auto relative = ge3d::pathToUtf8(std::filesystem::relative(it->path(), contentRoot_, error));
            std::replace(relative.begin(), relative.end(), '\\', '/');
            if (ge3d::isValidScriptPath(relative)) scriptList_.push_back(relative);
        }
    }
    std::sort(scriptList_.begin(), scriptList_.end());
    soundList_.clear();
    const auto sounds = contentRoot_ / "sounds";
    if (std::filesystem::is_directory(sounds, error)) {
        for (auto it = std::filesystem::directory_iterator(sounds, error); !error && it != std::filesystem::directory_iterator();
             it.increment(error)) {
            if (it->path().extension() != ".wav") continue;
            const auto clip = "sounds/" + ge3d::pathToUtf8(it->path().filename());
            if (ge3d::isValidSoundPath(clip)) soundList_.push_back(clip);
        }
    }
    std::sort(soundList_.begin(), soundList_.end());
    // Modelos glTF / GLB de todo Content; los ya importados se reimportan si cambió su archivo.
    modelList_.clear();
    for (auto it = std::filesystem::recursive_directory_iterator(contentRoot_, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        const auto extension = it->path().extension();
        if (!it->is_regular_file(error) || (extension != ".gltf" && extension != ".glb")) continue;
        auto relative = ge3d::pathToUtf8(std::filesystem::relative(it->path(), contentRoot_, error));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if (ge3d::isValidModelPath(relative)) modelList_.push_back(relative);
    }
    std::sort(modelList_.begin(), modelList_.end());
    models_.refresh();
    // Prefabs guardados (desde el modo prefab o por fuera): sus copias en esta escena se actualizan.
    syncPrefabs(false);
    // Cambios hechos fuera del editor (o con otro editor de texto): se recargan como al guardar.
    for (const auto& path : scriptList_) {
        const auto time = std::filesystem::last_write_time(scriptFile(path), error);
        if (error) continue;
        auto [known, inserted] = scriptTimes_.try_emplace(path, time);
        if (inserted || known->second == time) continue;
        known->second = time;
        for (auto& document : documents_) {
            if (document.path == path && !document.dirty()) {
                if (auto text = readScript(path)) {
                    document.text = document.saved = *text;
                    document.editedAt = ImGui::GetTime();
                }
            }
        }
        if (scripts_->running()) {
            std::vector<ge3d::ScriptDiagnostic> diagnostics;
            scripts_->reload(scene_, path, &diagnostics);
        }
        log("Script " + path + " cambiado en disco");
    }
}

void EditorView::openScript(const std::string& path, int line) {
    showCode_ = true;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i].path == path) {
            selectDocument_ = static_cast<int>(i);
            if (line > 0) documents_[i].jumpLine = line;
            ImGui::SetWindowFocus("Codigo");
            return;
        }
    }
    const auto text = readScript(path);
    if (!text) {
        log("No se puede abrir el script " + path, true);
        return;
    }
    CodeDocument document;
    document.path = path;
    document.text = document.saved = *text;
    document.editedAt = 0.0;
    document.jumpLine = line;
    documents_.push_back(std::move(document));
    selectDocument_ = static_cast<int>(documents_.size()) - 1;
    ImGui::SetWindowFocus("Codigo");
}

bool EditorView::saveScript(CodeDocument& document) {
    try {
        ge3d::writeTextFileAtomic(scriptFile(document.path), document.text);
    } catch (const std::exception& error) {
        log(std::string("No se pudo guardar el script: ") + error.what(), true);
        return false;
    }
    document.saved = document.text;
    std::error_code error;
    scriptTimes_[document.path] = std::filesystem::last_write_time(scriptFile(document.path), error);
    scriptCache_.erase(document.path);
    // En Play se recarga al momento; si no compila, sigue funcionando la versión anterior.
    document.diagnostics.clear();
    if (scripts_->running()) {
        scripts_->reload(scene_, document.path, &document.diagnostics);
    } else if (const auto* info = scriptInfo(document.path)) {
        document.diagnostics = info->diagnostics;
    }
    document.editedAt = -1.0;
    log("Script guardado: " + document.path + (document.diagnostics.empty() ? "" : " (con errores)"),
        !document.diagnostics.empty());
    return true;
}

void EditorView::createScript(const std::string& rawName) {
    std::string name;
    for (const char c : rawName) {
        if (std::isalnum(static_cast<unsigned char>(c))) name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (c == ' ' || c == '_' || c == '-') name += '_';
    }
    if (name.empty()) {
        log("Nombre de script vacio", true);
        return;
    }
    const std::string path = "scripts/" + name + ".luau";
    std::error_code error;
    if (std::filesystem::exists(scriptFile(path), error)) {
        openScript(path);
        return;
    }
    std::string type = name;
    type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(type[0])));
    std::replace(type.begin(), type.end(), '_', ' ');
    type.erase(std::remove(type.begin(), type.end(), ' '), type.end());
    const std::string text =
        "-- " + name + ".luau\n"
        "local " + type + " = {}\n\n"
        "-- Parametros editables en el Inspector (numero, booleano o texto).\n" +
        type + ".params = {\n\tvelocidad = 1,\n}\n\n"
        "-- Al pulsar Play. self.entity es la entidad; self.velocidad, el parametro.\n"
        "function " + type + ".onStart(self)\nend\n\n"
        "-- Cada fotograma; dt son los segundos desde el anterior.\n"
        "function " + type + ".onUpdate(self, dt)\nend\n\n"
        "-- Clic sobre la entidad en Play.\n"
        "function " + type + ".onInteract(self)\n\tlog(\"clic en\", self.entity.name)\nend\n\n"
        "return " + type + "\n";
    try {
        ge3d::writeTextFileAtomic(scriptFile(path), text);
    } catch (const std::exception& failure) {
        log(std::string("No se pudo crear el script: ") + failure.what(), true);
        return;
    }
    watchScripts(true);
    refreshProject();
    log("Script creado: " + path);
    openScript(path);
}

// --- Pestaña Código ------------------------------------------------------------------------------

void EditorView::drawScriptApi() {
    if (!ImGui::BeginPopup("api_scripts")) return;
    ImGui::SeparatorText("Estructura de un script");
    ImGui::TextUnformatted("local S = {}\nS.params = { velocidad = 1 }   -- Inspector\n"
                           "function S.onStart(self) end            -- al pulsar Play\n"
                           "function S.onUpdate(self, dt) end       -- cada fotograma\n"
                           "function S.onInteract(self) end         -- clic sobre la entidad\n"
                           "function S.onMessage(self, evento, valor, origen) end\n"
                           "function S.onStop(self) end             -- al detener\n"
                           "return S");
    ImGui::SeparatorText("Entidad (self.entity)");
    ImGui::TextUnformatted("entity.position, entity.rotation, entity.scale   -- vector (lectura y escritura)\n"
                           "entity.name, entity.id\n"
                           "entity:send(evento, valor)                       -- mensaje a su script");
    ImGui::SeparatorText("Fisica y sonido");
    ImGui::TextUnformatted("entity.velocity                                  -- vector (cuerpos dinamicos)\n"
                           "entity:applyImpulse(vector.create(0, 5, 0))\n"
                           "entity:playSound(\"sounds/puerta.wav\", volumen)   -- desde la entidad\n"
                           "entity:playAudio(), entity:stopAudio()             -- su Fuente de audio\n"
                           "audio.play(\"sounds/campana.wav\", volumen)        -- sin posicion\n"
                           "function S.onTriggerEnter(self, otro) end          -- zonas\n"
                           "function S.onTriggerExit(self, otro) end\n"
                           "function S.onCollision(self, otro, velocidad) end");
    ImGui::SeparatorText("Motor");
    ImGui::TextUnformatted("scene.find(nombre) -> entidad o nil\n"
                           "scene.player() -> el jugador o nil\n"
                           "scene.send(nombre, evento, valor)\n"
                           "scene.time() -> segundos desde Play\n"
                           "input.isDown(\"E\"), input.wasPressed(\"Space\")   -- A-Z, 0-9, Space, Enter,\n"
                           "                                                -- Left, Right, Up, Down, Shift, Ctrl,\n"
                           "                                                -- Escape, Tab\n"
                           "log(...)  (o print)                             -- Consola\n"
                           "vector.create(x, y, z), math, string, table");
    ImGui::SeparatorText("Interfaz (en onUpdate, pixeles de la escena)");
    ImGui::TextUnformatted("ui.width(), ui.height(), ui.mouse() -> x, y o nil\n"
                           "ui.rect(x, y, ancho, alto, \"#rrggbbaa\")\n"
                           "ui.panel(x, y, ancho, alto, [relleno], [borde])\n"
                           "ui.text(texto, x, y, [color], [escala], [\"left\"|\"center\"|\"right\"])\n"
                           "ui.bar(x, y, ancho, alto, valor0a1, [color], [fondo])\n"
                           "ui.button(texto, x, y, ancho, alto) -> true al pulsar\n"
                           "ui.wrap(texto, ancho), ui.textWidth(texto), ui.lineHeight()\n"
                           "ui.cursor(true)                     -- raton libre para menus");
    ImGui::SeparatorText("Limites");
    ImGui::TextUnformatted("1 ms por llamada y 3,5 ms por fotograma para todos los scripts.\n"
                           "Sin acceso a archivos ni al sistema. Un error desactiva el script hasta Stop.");
    ImGui::EndPopup();
}

void EditorView::drawCodeDocument(CodeDocument& document) {
    // Barra: guardar, estado de la compilación y ayuda.
    const bool save = ImGui::Button("Guardar") ||
                      (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                       ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S));
    ImGui::SetItemTooltip("Guardar el script (Ctrl+S). En Play se recarga al momento.");
    ImGui::SameLine();
    if (ImGui::Button("API")) ImGui::OpenPopup("api_scripts");
    ImGui::SetItemTooltip("Funciones y callbacks disponibles");
    drawScriptApi();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (document.editedAt >= 0.0) {
        ImGui::TextDisabled("Comprobando...");
    } else if (document.diagnostics.empty()) {
        ImGui::TextColored(okColor, "Sin errores");
    } else {
        ImGui::TextColored(errorText, "%zu error%s", document.diagnostics.size(), document.diagnostics.size() == 1 ? "" : "es");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|  linea %d", document.cursorLine + 1);
    if (save) saveScript(document);

    // Comprobación en vivo: se compila medio segundo después de dejar de escribir (sin guardar).
    if (document.editedAt >= 0.0 && ImGui::GetTime() - document.editedAt > 0.5) {
        document.diagnostics = ge3d::describeScript(document.text, document.path).diagnostics;
        document.editedAt = -1.0;
    }

    const float diagnosticsHeight = document.diagnostics.empty() ? 0.0f
        : std::min(5.0f, static_cast<float>(document.diagnostics.size())) * ImGui::GetTextLineHeightWithSpacing() +
          ImGui::GetStyle().WindowPadding.y * 2;
    if (codeFont_) ImGui::PushFont(codeFont_);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float charWidth = ImGui::CalcTextSize("M").x;
    int lines = 1;
    std::size_t longest = 0, current = 0;
    for (const char c : document.text) {
        if (c == '\n') {
            ++lines;
            longest = std::max(longest, current);
            current = 0;
        } else {
            current += c == '\t' ? 4 : 1;
        }
    }
    longest = std::max(longest, current);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.11f, 0.11f, 0.12f, 1.0f});
    if (ImGui::BeginChild("##texto", {0, -diagnosticsHeight}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        const float gutter = charWidth * 5.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float padY = ImGui::GetStyle().FramePadding.y;
        auto* draw = ImGui::GetWindowDrawList();
        // Líneas con error: fondo rojo y número en rojo.
        for (const auto& diagnostic : document.diagnostics) {
            if (diagnostic.line <= 0) continue;
            const float y = origin.y + padY + static_cast<float>(diagnostic.line - 1) * lineHeight;
            draw->AddRectFilled({origin.x, y}, {origin.x + std::max(ImGui::GetContentRegionAvail().x, gutter + charWidth * static_cast<float>(longest + 4)), y + lineHeight},
                                IM_COL32(150, 40, 40, 90));
        }
        const float scrollY = ImGui::GetScrollY();
        const float viewHeight = ImGui::GetWindowHeight();
        const int first = std::max(0, static_cast<int>((scrollY - padY) / lineHeight) - 1);
        const int last = std::min(lines, first + static_cast<int>(viewHeight / lineHeight) + 3);
        for (int line = first; line < last; ++line) {
            char number[16];
            std::snprintf(number, sizeof number, "%4d", line + 1);
            const bool error = std::any_of(document.diagnostics.begin(), document.diagnostics.end(),
                                           [&](const ge3d::ScriptDiagnostic& d) { return d.line == line + 1; });
            draw->AddText({origin.x, origin.y + padY + static_cast<float>(line) * lineHeight},
                          error ? IM_COL32(255, 110, 100, 255) : IM_COL32(110, 110, 120, 255), number);
        }
        ImGui::SetCursorScreenPos({origin.x + gutter, origin.y});
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
        const ImVec2 size{std::max(ImGui::GetContentRegionAvail().x, charWidth * static_cast<float>(longest + 4)),
                          std::max(ImGui::GetContentRegionAvail().y, static_cast<float>(lines + 1) * lineHeight + padY * 2)};
        CodeCallbackState state{&document.text, &document.jumpLine, &document.cursorLine};
        const bool jumping = document.jumpLine > 0;
        if (jumping) {
            ImGui::SetKeyboardFocusHere();
            ImGui::SetScrollY(std::max(0.0f, static_cast<float>(document.jumpLine - 1) * lineHeight - viewHeight * 0.35f));
        }
        const int previousLine = document.cursorLine;
        if (ImGui::InputTextMultiline("##fuente", document.text.data(), document.text.capacity() + 1, size,
                                      ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize |
                                          ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_NoHorizontalScroll,
                                      codeCallback, &state)) {
            document.editedAt = ImGui::GetTime();
        }
        ImGui::PopStyleColor();
        // El área de texto mide todo el archivo: el desplazamiento lo lleva esta ventana. Se sigue al cursor.
        if (ImGui::IsItemActive() && (document.cursorLine != previousLine || document.scrollToCursor)) {
            const float y = padY + static_cast<float>(document.cursorLine) * lineHeight;
            if (y < scrollY) ImGui::SetScrollY(y);
            else if (y + lineHeight * 2 > scrollY + viewHeight) ImGui::SetScrollY(y + lineHeight * 2 - viewHeight);
            document.scrollToCursor = false;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (codeFont_) ImGui::PopFont();

    // Lista de errores: clic para ir a la línea.
    if (!document.diagnostics.empty() && ImGui::BeginChild("##errores", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding)) {
        for (std::size_t i = 0; i < document.diagnostics.size(); ++i) {
            const auto& diagnostic = document.diagnostics[i];
            char label[600];
            std::snprintf(label, sizeof label, "%s  %s###diag%zu",
                          diagnostic.line > 0 ? ("linea " + std::to_string(diagnostic.line) + ":").c_str() : "",
                          diagnostic.message.c_str(), i);
            ImGui::PushStyleColor(ImGuiCol_Text, errorText);
            if (ImGui::Selectable(label) && diagnostic.line > 0) document.jumpLine = diagnostic.line;
            ImGui::PopStyleColor();
        }
    }
    if (!document.diagnostics.empty()) ImGui::EndChild();
}

void EditorView::drawCode() {
    if (!showCode_) return;
    // Sin robar el foco al aparecer: al abrir el editor se ve la Escena.
    codeFocused_ = false;
    if (!ImGui::Begin("Codigo", &showCode_, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    codeFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !documents_.empty();
    // Lista de scripts del proyecto.
    if (ImGui::BeginChild("##scripts", {170 * scale_, 0}, ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders)) {
        if (ImGui::Button("+ Nuevo script", {-1, 0})) {
            newScriptName_[0] = '\0';
            ImGui::OpenPopup("nuevo_script");
        }
        if (ImGui::BeginPopup("nuevo_script")) {
            ImGui::TextUnformatted("Nombre del script");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##nombre_script", newScriptName_, sizeof newScriptName_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Crear") || enter) {
                createScript(newScriptName_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
        if (scriptList_.empty()) ImGui::TextDisabled("Sin scripts en\nContent/scripts");
        for (const auto& path : scriptList_) {
            const bool open = std::any_of(documents_.begin(), documents_.end(), [&](const CodeDocument& d) { return d.path == path; });
            if (ImGui::Selectable(fileName(path).c_str(), open)) openScript(path);
            ImGui::SetItemTooltip("%s", path.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (documents_.empty()) {
        ImGui::TextDisabled("Abre un script de la lista o crea uno nuevo.");
        ImGui::TextDisabled("Para usarlo: selecciona un objeto > Agregar componente > Script.");
    } else if (ImGui::BeginTabBar("##documentos", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (std::size_t i = 0; i < documents_.size(); ++i) {
            auto& document = documents_[i];
            bool open = true;
            const std::string label = fileName(document.path) + (document.dirty() ? " *" : "") + "###" + document.path;
            ImGuiTabItemFlags flags = document.dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (selectDocument_ == static_cast<int>(i)) flags |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                drawCodeDocument(document);
                ImGui::EndTabItem();
            }
            if (!open) {
                if (document.dirty()) closingDocument_ = static_cast<int>(i);
                else {
                    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
        }
        selectDocument_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::EndGroup();

    // Cerrar con cambios sin guardar: preguntar.
    if (closingDocument_ >= 0) ImGui::OpenPopup("cerrar_script");
    if (ImGui::BeginPopupModal("cerrar_script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (closingDocument_ < 0 || closingDocument_ >= static_cast<int>(documents_.size())) {
            closingDocument_ = -1;
            ImGui::CloseCurrentPopup();
        } else {
            auto& document = documents_[static_cast<std::size_t>(closingDocument_)];
            ImGui::Text("%s tiene cambios sin guardar.", fileName(document.path).c_str());
            const auto close = [&] {
                documents_.erase(documents_.begin() + closingDocument_);
                closingDocument_ = -1;
                ImGui::CloseCurrentPopup();
            };
            if (ImGui::Button("Guardar")) {
                if (saveScript(document)) close();
            }
            ImGui::SameLine();
            if (ImGui::Button("Descartar")) close();
            ImGui::SameLine();
            if (ImGui::Button("Cancelar")) {
                closingDocument_ = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

// --- Componente Script ---------------------------------------------------------------------------

void EditorView::drawScriptComponent(ge3d::Entity& entity, const ge3d::Entity& frameStart) {
    auto& script = *entity.script;
    const float labelWidth = std::max(70.0f, ImGui::GetContentRegionAvail().x * 0.34f);
    const auto row = [&](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
    };

    row("Script");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Editar").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    const std::string shown = script.path.empty() ? "(ninguno)" : fileName(script.path);
    if (ImGui::BeginCombo("##script", shown.c_str())) {
        for (const auto& path : scriptList_) {
            if (ImGui::Selectable(fileName(path).c_str(), path == script.path) && path != script.path) {
                std::optional<ge3d::Entity> before = frameStart;
                script.path = path;
                script.parameters.clear();
                commitModification(before, "Cambiar script");
            }
            ImGui::SetItemTooltip("%s", path.c_str());
        }
        if (scriptList_.empty()) ImGui::TextDisabled("No hay scripts: crea uno en la pestana Codigo");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(script.path.empty());
    if (ImGui::Button("Editar")) openScript(script.path);
    ImGui::EndDisabled();

    row("Activo");
    bool enabled = script.enabled;
    if (ImGui::Checkbox("##activo", &enabled)) {
        std::optional<ge3d::Entity> before = frameStart;
        script.enabled = enabled;
        commitModification(before, enabled ? "Activar script" : "Desactivar script");
    }
    if (isPlaying() && !script.enabled) {
        ImGui::SameLine();
        ImGui::TextColored(errorText, "desactivado (ver Consola)");
    }

    if (script.path.empty()) return;
    const auto* info = scriptInfo(script.path);
    if (!info) {
        ImGui::TextColored(errorText, "No se encuentra %s", script.path.c_str());
        return;
    }
    if (!info->ok) {
        for (const auto& diagnostic : info->diagnostics) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorText);
            const std::string text = (diagnostic.line ? "linea " + std::to_string(diagnostic.line) + ": " : "") + diagnostic.message;
            if (ImGui::Selectable(text.c_str())) openScript(script.path, diagnostic.line);
            ImGui::PopStyleColor();
        }
        return;
    }

    if (!info->parameters.empty()) ImGui::SeparatorText("Parametros");
    for (const auto& parameter : info->parameters) {
        ImGui::PushID(parameter.name.c_str());
        // Valor elegido en el Inspector si es del mismo tipo que el del script; si no, el del script.
        auto chosen = script.parameters.find(parameter.name);
        const bool overridden = chosen != script.parameters.end() && chosen->second.index() == parameter.value.index();
        ge3d::ScriptValue value = overridden ? chosen->second : parameter.value;
        row(parameter.name.c_str());
        const float reset = overridden ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset);
        bool changed = false;
        bool finished = false;
        if (auto* number = std::get_if<double>(&value)) {
            changed = ImGui::DragScalar("##valor", ImGuiDataType_Double, number, 0.1f, nullptr, nullptr, "%.3g");
            if (ImGui::IsItemActivated() && !inspectorEdit_) inspectorEdit_ = frameStart;
            finished = ImGui::IsItemDeactivatedAfterEdit();
        } else if (auto* flag = std::get_if<bool>(&value)) {
            changed = ImGui::Checkbox("##valor", flag);
            if (changed && !inspectorEdit_) inspectorEdit_ = frameStart;
            finished = changed;
        } else {
            auto& text = std::get<std::string>(value);
            char buffer[256];
            std::snprintf(buffer, sizeof buffer, "%s", text.c_str());
            changed = ImGui::InputText("##valor", buffer, sizeof buffer);
            if (changed) text = buffer;
            if (ImGui::IsItemActivated() && !inspectorEdit_) inspectorEdit_ = frameStart;
            finished = ImGui::IsItemDeactivatedAfterEdit();
        }
        if (changed) {
            script.parameters[parameter.name] = value;
            // En Play el cambio llega al script en marcha (y se descarta al detener, como todo Play).
            if (scripts_->running()) scripts_->setParameter(entity.id, parameter.name, value);
        }
        if (finished) commitModification(inspectorEdit_, "Cambiar parametro");
        if (overridden) {
            ImGui::SameLine();
            if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0})) {
                std::optional<ge3d::Entity> before = frameStart;
                script.parameters.erase(parameter.name);
                if (scripts_->running()) scripts_->setParameter(entity.id, parameter.name, parameter.value);
                commitModification(before, "Restablecer parametro");
            }
            ImGui::SetItemTooltip("Volver al valor del script");
        }
        ImGui::PopID();
    }
    if (!info->callbacks.empty()) {
        std::string list;
        for (const auto& callback : info->callbacks) list += (list.empty() ? "" : ", ") + callback;
        ImGui::TextDisabled("Responde a: %s", list.c_str());
    }
    if (isPlaying()) {
        const auto stats = scripts_->stats();
        ImGui::TextDisabled("Play: %d scripts, %.2f ms, %.1f MB", stats.instances, stats.frameMs,
                            static_cast<double>(stats.memory) / (1024.0 * 1024.0));
    }
}

// --- Entrada para los scripts ----------------------------------------------------------------------

ge3d::ScriptInput EditorView::gatherInput() const {
    ge3d::ScriptInput input;
    if (ImGui::GetIO().WantTextInput) {
        input.uiWidth = static_cast<float>(sceneSize_.x);
        input.uiHeight = static_cast<float>(sceneSize_.y);
        return input;
    }
    const auto add = [&](ImGuiKey key, const std::string& name) {
        if (ImGui::IsKeyDown(key)) input.down.push_back(name);
        if (ImGui::IsKeyPressed(key, false)) input.pressed.push_back(name);
    };
    for (int i = 0; i < 26; ++i) add(static_cast<ImGuiKey>(ImGuiKey_A + i), std::string(1, static_cast<char>('A' + i)));
    for (int i = 0; i < 10; ++i) add(static_cast<ImGuiKey>(ImGuiKey_0 + i), std::string(1, static_cast<char>('0' + i)));
    add(ImGuiKey_Space, "Space");
    add(ImGuiKey_Enter, "Enter");
    add(ImGuiKey_LeftArrow, "Left");
    add(ImGuiKey_RightArrow, "Right");
    add(ImGuiKey_UpArrow, "Up");
    add(ImGuiKey_DownArrow, "Down");
    add(ImGuiKey_LeftShift, "Shift");
    add(ImGuiKey_LeftCtrl, "Ctrl");
    add(ImGuiKey_Escape, "Escape");
    add(ImGuiKey_Tab, "Tab");
    // Interfaz: pantalla y ratón en píxeles de la escena (sin ratón si lo tiene capturado la cámara).
    const float ratio = sceneSize_.x > 0 ? uiSize_.x / static_cast<float>(sceneSize_.x) : 1.0f;
    input.uiWidth = static_cast<float>(sceneSize_.x);
    input.uiHeight = static_cast<float>(sceneSize_.y);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!mouseCaptured_ && uiHovered_ && ratio > 0.0f && mouse.x >= uiOrigin_.x && mouse.y >= uiOrigin_.y &&
        mouse.x < uiOrigin_.x + uiSize_.x && mouse.y < uiOrigin_.y + uiSize_.y) {
        input.mouseX = std::floor((mouse.x - uiOrigin_.x) / ratio);
        input.mouseY = std::floor((mouse.y - uiOrigin_.y) / ratio);
        input.mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        input.mousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    }
    return input;
}
