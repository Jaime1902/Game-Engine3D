#pragma once

#include "ge3d/Scene.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ge3d {

// Sonido de una escena en Play (miniaudio). Las Fuentes de audio de la escena son emisores: con
// "spatial" suenan desde su entidad y bajan de volumen con la distancia al oyente (la cámara o el
// jugador). Los efectos sueltos (pasos, golpes, scripts) tienen un límite de voces: si se llena, se
// descarta el más lejano (guidelines 6.1). Sin dispositivo de audio todo sigue funcionando en silencio.
class AudioSystem {
public:
    using LogSink = std::function<void(bool error, const std::string& message)>;
    static constexpr int maxOneShots = 24;

    struct Listener {
        Vec3 position{};
        Vec3 forward{0, 0, -1};
        Vec3 up{0, 1, 0};
    };

    AudioSystem(std::filesystem::path contentRoot, LogSink log);
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Decodifica de antemano los sonidos indicados (al pulsar Play): reproducirlos después es inmediato.
    void preload(const std::vector<std::string>& clips);
    // Prepara los emisores de la escena y empieza los que tienen "al empezar".
    void start(const Scene& scene);
    // Coloca emisores y oyente; crea o retira emisores si la escena cambió.
    void update(const Scene& scene, const Listener& listener);
    void stop();

    // Efecto suelto. Con posición suena en 3D; sin ella, igual en los dos oídos. Solo se encola (es seguro
    // llamarlo desde un script con presupuesto de tiempo): suena en el siguiente update.
    void playOneShot(const std::string& clip, float volume, std::optional<Vec3> position);
    // Reproduce ya los efectos encolados (fuera de Play, p. ej. escuchar un sonido desde el Inspector).
    void flush();
    // Control de la Fuente de audio de una entidad (scripts).
    void playEmitter(EntityId entity);
    void stopEmitter(EntityId entity);

    struct Stats {
        bool device{};
        int emitters{};
        int playing{};
        int oneShots{};
    };
    [[nodiscard]] Stats stats() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}
