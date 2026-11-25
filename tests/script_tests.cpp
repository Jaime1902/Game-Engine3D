// Pruebas del host de scripts Luau: parámetros, callbacks, presupuesto, sandbox, mensajes y recarga.
#include "ge3d/SceneIO.hpp"
#include "ge3d/ScriptHost.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FALLO: " << what << '\n';
        ++failures;
    }
}

// Fuentes en memoria por ruta; los errores y registros se acumulan para comprobarlos.
struct Harness {
    std::map<std::string, std::string> sources;
    std::vector<std::string> errors;
    std::vector<std::string> messages;
    ge3d::ScriptHost host{[this](const std::string& path) -> std::optional<std::string> {
                              const auto it = sources.find(path);
                              return it == sources.end() ? std::nullopt : std::optional<std::string>(it->second);
                          },
                          [this](bool error, const std::string& text) { (error ? errors : messages).push_back(text); }};
    [[nodiscard]] bool logged(const std::string& text) const {
        for (const auto& message : messages) {
            if (message.find(text) != std::string::npos) return true;
        }
        return false;
    }
    [[nodiscard]] bool failed(const std::string& text) const {
        for (const auto& error : errors) {
            if (error.find(text) != std::string::npos) return true;
        }
        return false;
    }
};

ge3d::EntityId addScripted(ge3d::Scene& scene, const std::string& name, const std::string& path,
                           std::map<std::string, ge3d::ScriptValue> parameters = {}) {
    auto entity = scene.makeEntity(name, ge3d::Primitive::box);
    entity.script = ge3d::ScriptComponent{path, true, std::move(parameters)};
    return scene.add(std::move(entity));
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void testDescribe() {
    const auto info = ge3d::describeScript(R"(
        local S = {}
        S.params = { velocidad = 2, activo = true, nombre = "hola" }
        function S.onUpdate(self, dt) end
        return S
    )", "scripts/s.luau");
    expect(info.ok && info.parameters.size() == 3, "describe lee los parametros");
    expect(info.parameters[0].name == "activo" && std::get<bool>(info.parameters[0].value), "parametro booleano");
    expect(info.parameters[2].name == "velocidad" && std::get<double>(info.parameters[2].value) == 2.0, "parametro numerico");
    expect(info.callbacks.size() == 1 && info.callbacks[0] == "onUpdate", "describe detecta los callbacks");

    const auto broken = ge3d::describeScript("local S = {}\nfunction S.onStart(self)\n  if x then\nend\nreturn S\n", "scripts/roto.luau");
    expect(!broken.ok && !broken.diagnostics.empty() && broken.diagnostics[0].line > 0, "un error de sintaxis indica la linea");
    const auto noTable = ge3d::describeScript("local x = 1", "scripts/x.luau");
    expect(!noTable.ok, "un script que no devuelve tabla es un error");
}

void testDoor() {
    Harness h;
    h.sources["scripts/puerta.luau"] = readFile(std::string(GE3D_CONTENT_DIR) + "/scripts/puerta.luau");
    expect(!h.sources["scripts/puerta.luau"].empty(), "el script de ejemplo de la puerta existe");
    const auto info = ge3d::describeScript(h.sources["scripts/puerta.luau"], "scripts/puerta.luau");
    expect(info.ok && info.parameters.size() == 3, "la puerta declara angulo, abierta y velocidad");

    ge3d::Scene scene;
    const auto door = addScripted(scene, "Puerta", "scripts/puerta.luau", {{"velocidad", 180.0}});
    scene.find(door)->transform.scale = {2, 3, 0.2f};
    const auto start = scene.find(door)->transform;
    h.host.start(scene);
    expect(h.errors.empty(), "la puerta arranca sin errores");
    expect(scene.find(door)->transform.position == start.position, "cerrada, la puerta no se mueve al empezar");
    expect(h.host.interact(scene, door), "clic en la puerta llama a onInteract");
    for (int i = 0; i < 30; ++i) h.host.update(scene, 1.0f / 60.0f, {});
    // 0,5 s a 180 grados por segundo (valor del Inspector, no el del script): 90 grados.
    expect(std::abs(scene.find(door)->transform.rotationDegrees.y - 90.0f) < 0.01f, "la velocidad del Inspector abre la puerta");
    // La bisagra (borde izquierdo) no se mueve: el centro queda a medio ancho hacia -Z.
    const auto& p = scene.find(door)->transform.position;
    expect(std::abs(p.x - (start.position.x - 1.0f)) < 0.01f && std::abs(p.z - (start.position.z - 1.0f)) < 0.01f,
           "la puerta gira alrededor de su bisagra");
    expect(h.logged("[Puerta] se abre"), "log identifica a la entidad");
    h.host.update(scene, 0.1f, {});
    scene.find(door)->transform.rotationDegrees.y += 0.0f;
    h.host.stop(scene);
    expect(!h.host.running(), "Stop destruye la maquina virtual");
}

void testBudgetAndSandbox() {
    Harness h;
    h.sources["scripts/bucle.luau"] = R"(
        local S = {}
        function S.onUpdate(self, dt) while true do end end
        return S
    )";
    h.sources["scripts/contador.luau"] = R"(
        local S = {}
        S.params = { pasos = 0 }
        function S.onUpdate(self, dt)
            self.pasos += 1
            self.entity.name = "pasos " .. self.pasos
        end
        return S
    )";
    h.sources["scripts/sandbox.luau"] = R"(
        local S = {}
        function S.onStart(self)
            log(io == nil, debug == nil, loadstring == nil, os.execute == nil, os.getenv == nil)
            local ok = pcall(function() string.len = nil end)
            log("biblioteca modificable", ok)
        end
        return S
    )";
    ge3d::Scene scene;
    const auto loop = addScripted(scene, "Bucle", "scripts/bucle.luau");
    const auto counter = addScripted(scene, "Contador", "scripts/contador.luau");
    addScripted(scene, "Sandbox", "scripts/sandbox.luau");
    h.host.start(scene);
    h.host.update(scene, 0.016f, {});
    h.host.update(scene, 0.016f, {});
    expect(h.failed("presupuesto"), "un bucle sin fin se interrumpe con un error");
    expect(!scene.find(loop)->script->enabled, "el script que agota el presupuesto queda desactivado");
    expect(scene.find(counter)->name == "pasos 2", "los demas scripts siguen funcionando");
    expect(h.host.stats().disabled == 1, "estadisticas: una instancia desactivada");
    expect(h.logged("true true true true true"), "sin io, debug, loadstring ni funciones del sistema");
    expect(h.logged("biblioteca modificable false"), "las bibliotecas son de solo lectura");
    h.host.stop(scene);
}

void testMessagesAndErrors() {
    Harness h;
    h.sources["scripts/emisor.luau"] = R"(
        local S = {}
        function S.onStart(self) scene.send("Receptor", "hola", 5) end
        return S
    )";
    h.sources["scripts/receptor.luau"] = R"(
        local S = {}
        function S.onMessage(self, evento, valor, origen)
            self.entity.name = evento .. " " .. valor .. " de " .. origen.name
        end
        return S
    )";
    h.sources["scripts/error.luau"] = "local S = {}\nfunction S.onStart(self)\n  local x = nil\n  x.campo = 1\nend\nreturn S\n";
    ge3d::Scene scene;
    addScripted(scene, "Emisor", "scripts/emisor.luau");
    const auto receiver = addScripted(scene, "Receptor", "scripts/receptor.luau");
    addScripted(scene, "Error", "scripts/error.luau");
    addScripted(scene, "Falta", "scripts/no_existe.luau");
    h.host.start(scene);
    h.host.update(scene, 0.016f, {});
    expect(scene.find(receiver)->name == "hola 5 de Emisor", "scene.send entrega el mensaje con su origen");
    expect(h.failed("error.luau:4"), "un error en tiempo de ejecucion indica archivo y linea");
    expect(h.failed("no se encuentra el script"), "un script inexistente se informa");
    h.host.stop(scene);
}

void testReload() {
    Harness h;
    h.sources["scripts/r.luau"] = R"(
        local S = {}
        S.params = { valor = 1 }
        function S.onStart(self) self.entity.name = "v1 " .. self.valor end
        return S
    )";
    ge3d::Scene scene;
    const auto id = addScripted(scene, "R", "scripts/r.luau", {{"valor", 7.0}});
    h.host.start(scene);
    expect(scene.find(id)->name == "v1 7", "onStart con el parametro del Inspector");
    h.sources["scripts/r.luau"] = "local S = {\nfunction S.onStart(self) end\nreturn S";
    std::vector<ge3d::ScriptDiagnostic> diagnostics;
    expect(!h.host.reload(scene, "scripts/r.luau", &diagnostics) && !diagnostics.empty(), "una recarga con errores falla");
    h.sources["scripts/r.luau"] = R"(
        local S = {}
        S.params = { valor = 1 }
        function S.onStart(self) self.entity.name = "v2 " .. self.valor end
        return S
    )";
    expect(h.host.reload(scene, "scripts/r.luau"), "una recarga valida se aplica");
    expect(scene.find(id)->name == "v2 7", "la recarga reinicia el estado y conserva los parametros");
    h.host.stop(scene);
}

void testSerialization() {
    ge3d::Scene scene;
    addScripted(scene, "Puerta", "scripts/puerta.luau", {{"velocidad", 45.0}, {"abierta", true}, {"nombre", std::string("norte")}});
    const auto loaded = ge3d::parseScene(ge3d::serializeScene(scene));
    const auto& script = loaded.entities().front().script;
    expect(script && script->path == "scripts/puerta.luau" && script->enabled && script->parameters.size() == 3 &&
               std::get<double>(script->parameters.at("velocidad")) == 45.0 && std::get<bool>(script->parameters.at("abierta")),
           "el componente Script se guarda con sus parametros");
    expect(!ge3d::isValidScriptPath("../fuera.luau") && !ge3d::isValidScriptPath("scripts/a.lua") &&
               ge3d::isValidScriptPath("scripts/a.luau"),
           "rutas de script validas");
}

}

int main() {
    testDescribe();
    testDoor();
    testBudgetAndSandbox();
    testMessagesAndErrors();
    testReload();
    testSerialization();
    if (failures > 0) {
        std::cerr << failures << " comprobaciones fallidas\n";
        return 1;
    }
    std::cout << "script_tests: todo correcto\n";
    return 0;
}
