#pragma once

#include "ge3d/Scene.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ge3d {

// Error o aviso de un script, con la línea si se conoce (0 = sin línea).
struct ScriptDiagnostic {
    std::string path;
    int line{0};
    std::string message;
};

// Parámetro declarado por el script en su tabla params, con su valor por defecto.
struct ScriptParameter {
    std::string name;
    ScriptValue value;
};

// Lo que el editor necesita saber de un script sin ejecutarlo en Play.
struct ScriptInfo {
    bool ok{false};
    std::vector<ScriptParameter> parameters;   // ordenados por nombre
    std::vector<std::string> callbacks;        // onStart, onUpdate, onInteract, onMessage, onStop presentes
    std::vector<ScriptDiagnostic> diagnostics;
};

// Compila el script y ejecuta solo su cuerpo (en una VM temporal y aislada) para leer sus parámetros.
[[nodiscard]] ScriptInfo describeScript(const std::string& source, const std::string& path);

// Teclas de este fotograma para input.isDown / input.wasPressed ("E", "Space", "Left"...).
struct ScriptInput {
    std::vector<std::string> down;
    std::vector<std::string> pressed;
};

// Servicios del motor que los scripts pueden usar (física y audio). Los pone quien ejecuta Play;
// sin ellos, esas funciones no hacen nada.
struct ScriptServices {
    std::function<std::optional<Vec3>(EntityId)> velocity;
    std::function<void(EntityId, Vec3)> setVelocity;
    std::function<void(EntityId, Vec3)> applyImpulse;
    std::function<void(const std::string& clip, float volume, std::optional<Vec3> position)> playSound;
    std::function<void(EntityId, bool play)> emitter;
    std::function<EntityId()> player;
};

// Ejecuta los ScriptComponent de una escena en modo Play.
//
// Cada Play crea una VM de Luau nueva y Stop la destruye entera: ningún estado de script sobrevive.
// Los scripts no tienen acceso al sistema de archivos ni al sistema operativo, solo a la API del motor
// (entidades, escena, entrada, registro). Cada llamada tiene un presupuesto de 1 ms: un bucle infinito
// se interrumpe, se informa con su traza y esa instancia queda desactivada. Todos los scripts juntos
// tienen 3,5 ms por fotograma; lo que no cabe se ejecuta en el siguiente.
class ScriptHost {
public:
    using SourceLoader = std::function<std::optional<std::string>(const std::string& path)>;
    using LogSink = std::function<void(bool error, const std::string& message)>;

    static constexpr double callBudgetMs = 1.0;
    // onStart (y el cuerpo del script) preparan el estado una vez: tienen más margen.
    static constexpr double startBudgetMs = 20.0;
    static constexpr double frameBudgetMs = 3.5;
    static constexpr std::size_t memoryLimit = 64u * 1024u * 1024u;

    ScriptHost(SourceLoader loader, LogSink log);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Crea la VM, carga los scripts de las entidades y llama a onStart.
    void start(Scene& scene);
    // onUpdate(self, dt) de cada instancia y después los mensajes enviados con send.
    void update(Scene& scene, float dt, const ScriptInput& input);
    // onInteract(self) de la entidad (clic sobre ella en Play). Devuelve true si tenía ese callback.
    bool interact(Scene& scene, EntityId entity);
    // onStop y destrucción de la VM.
    void stop(Scene& scene);
    // Recompila un script cambiado. Si falla, las instancias siguen con la versión anterior; si va bien,
    // se recrean (estado privado nuevo, mismos parámetros) y se llama a su onStart.
    bool reload(Scene& scene, const std::string& path, std::vector<ScriptDiagnostic>* diagnostics = nullptr);

    void setServices(ScriptServices services);
    // Llama a un callback con otra entidad y un número, p. ej. onTriggerEnter(self, otro) u
    // onCollision(self, otro, velocidad). Si la entidad no tiene script o ese callback, no hace nada.
    void notify(Scene& scene, EntityId self, const char* callback, EntityId other, double value);

    // Cambia un parámetro de la instancia en marcha (el Inspector durante Play).
    void setParameter(EntityId entity, const std::string& name, const ScriptValue& value);

    [[nodiscard]] bool running() const noexcept;
    struct Stats {
        int instances{};
        int disabled{};
        double frameMs{};
        std::size_t memory{};
    };
    [[nodiscard]] Stats stats() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}
