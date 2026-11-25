#include "ge3d/Audio.hpp"

#define MA_NO_ENCODING
#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <vector>

namespace ge3d {

namespace {

float distanceSquared(Vec3 a, Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}

}

struct AudioSystem::Impl {
    std::filesystem::path root;
    LogSink log;
    ma_engine engine{};
    bool initialized{};
    bool failed{};
    Listener listener;
    std::set<std::string> reported;   // archivos que ya fallaron (un aviso por archivo)

    struct Emitter {
        std::unique_ptr<ma_sound> sound;
        AudioSourceComponent settings;
        bool seen{};
    };
    std::unordered_map<EntityId, Emitter> emitters;
    struct OneShot {
        std::unique_ptr<ma_sound> sound;
        std::optional<Vec3> position;
    };
    std::vector<OneShot> oneShots;
    struct Request {
        std::string clip;
        float volume{};
        std::optional<Vec3> position;
    };
    std::vector<Request> pending;                                   // cola de efectos (se descarta si se llena)
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> cache; // sonidos precargados (no suenan)
    void playNow(const Request& request);

    void report(bool error, const std::string& message) const {
        if (log) log(error, message);
    }

    bool ensureEngine() {
        if (initialized) return true;
        if (failed) return false;
        if (ma_engine_init(nullptr, &engine) != MA_SUCCESS) {
            failed = true;
            report(true, "Audio: no hay dispositivo de sonido disponible; Play sigue sin sonido");
            return false;
        }
        initialized = true;
        ma_engine_listener_set_world_up(&engine, 0, 0, 1, 0);
        return true;
    }

    // Carga un WAV de Content. Los datos decodificados se comparten entre sonidos del mismo archivo.
    std::unique_ptr<ma_sound> load(const std::string& clip, bool spatial) {
        if (!isValidSoundPath(clip)) {
            if (reported.insert(clip).second) report(true, "Audio: ruta de sonido invalida \"" + clip + "\"");
            return nullptr;
        }
        auto sound = std::make_unique<ma_sound>();
        const auto path = root / std::filesystem::path(std::u8string(clip.begin(), clip.end()));
        ma_uint32 flags = MA_SOUND_FLAG_DECODE;
        if (!spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
        const ma_result result = ma_sound_init_from_file_w(&engine, path.wstring().c_str(), flags, nullptr, nullptr, sound.get());
#else
        const ma_result result = ma_sound_init_from_file(&engine, path.string().c_str(), flags, nullptr, nullptr, sound.get());
#endif
        if (result != MA_SUCCESS) {
            if (reported.insert(clip).second) report(true, "Audio: no se puede cargar " + clip + " (se esperaba un WAV)");
            return nullptr;
        }
        // Atenuación lineal entre la distancia mínima y la máxima: el cambio al acercarse se nota.
        ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
        return sound;
    }

    void configure(Emitter& emitter, const AudioSourceComponent& settings) {
        auto* sound = emitter.sound.get();
        ma_sound_set_volume(sound, settings.volume);
        ma_sound_set_looping(sound, settings.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_min_distance(sound, settings.minDistance);
        ma_sound_set_max_distance(sound, std::max(settings.maxDistance, settings.minDistance + 0.01f));
        emitter.settings = settings;
    }

    void clearOneShots(bool all) {
        for (auto it = oneShots.begin(); it != oneShots.end();) {
            if (all || ma_sound_at_end(it->sound.get())) {
                ma_sound_uninit(it->sound.get());
                it = oneShots.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clearCache() {
        for (auto& [clip, sound] : cache) {
            if (sound) ma_sound_uninit(sound.get());
        }
        cache.clear();
    }

    void clearEmitters() {
        for (auto& [id, emitter] : emitters) {
            if (emitter.sound) ma_sound_uninit(emitter.sound.get());
        }
        emitters.clear();
    }
};

AudioSystem::AudioSystem(std::filesystem::path contentRoot, LogSink log) : impl_(std::make_unique<Impl>()) {
    impl_->root = std::move(contentRoot);
    impl_->log = std::move(log);
}

AudioSystem::~AudioSystem() {
    stop();
    impl_->clearCache();
    if (impl_->initialized) ma_engine_uninit(&impl_->engine);
}

void AudioSystem::preload(const std::vector<std::string>& clips) {
    auto& impl = *impl_;
    if (!impl.ensureEngine()) return;
    for (const auto& clip : clips) {
        if (impl.cache.contains(clip)) continue;
        // Mantener un sonido cargado deja los datos decodificados en la caché de miniaudio.
        impl.cache[clip] = impl.load(clip, false);
    }
}

void AudioSystem::start(const Scene& scene) {
    stop();
    if (!impl_->ensureEngine()) return;
    update(scene, impl_->listener);
    for (auto& [id, emitter] : impl_->emitters) {
        if (emitter.settings.playOnStart && emitter.sound) ma_sound_start(emitter.sound.get());
    }
}

void AudioSystem::update(const Scene& scene, const Listener& listener) {
    auto& impl = *impl_;
    impl.listener = listener;
    if (!impl.initialized) return;
    ma_engine_listener_set_position(&impl.engine, 0, listener.position.x, listener.position.y, listener.position.z);
    ma_engine_listener_set_direction(&impl.engine, 0, listener.forward.x, listener.forward.y, listener.forward.z);
    ma_engine_listener_set_world_up(&impl.engine, 0, listener.up.x, listener.up.y, listener.up.z);

    for (auto& [id, emitter] : impl.emitters) emitter.seen = false;
    for (const auto& entity : scene.entities()) {
        if (!entity.audio || entity.audio->clip.empty() || !entity.active) continue;
        auto [it, created] = impl.emitters.try_emplace(entity.id);
        auto& emitter = it->second;
        emitter.seen = true;
        const auto& settings = *entity.audio;
        if (!created && emitter.sound && (settings.clip != emitter.settings.clip || settings.spatial != emitter.settings.spatial)) {
            ma_sound_uninit(emitter.sound.get());
            emitter.sound.reset();
            created = true;
        }
        if (created) {
            emitter.settings = settings;
            emitter.sound = impl.load(settings.clip, settings.spatial);
            if (emitter.sound) impl.configure(emitter, settings);
        } else if (emitter.sound && !(settings == emitter.settings)) {
            impl.configure(emitter, settings);   // volumen o distancias cambiados en el Inspector
        }
        if (emitter.sound && settings.spatial) {
            const auto& p = entity.transform.position;
            ma_sound_set_position(emitter.sound.get(), p.x, p.y, p.z);
        }
    }
    for (auto it = impl.emitters.begin(); it != impl.emitters.end();) {
        if (!it->second.seen) {
            if (it->second.sound) ma_sound_uninit(it->second.sound.get());
            it = impl.emitters.erase(it);
        } else {
            ++it;
        }
    }
    impl.clearOneShots(false);
    auto requests = std::move(impl.pending);
    impl.pending.clear();
    for (const auto& request : requests) impl.playNow(request);
}

void AudioSystem::stop() {
    impl_->pending.clear();
    impl_->clearOneShots(true);
    impl_->clearEmitters();
}

void AudioSystem::playOneShot(const std::string& clip, float volume, std::optional<Vec3> position) {
    // Cola acotada: si un script pide cientos de sonidos en un fotograma, los que sobran se descartan.
    if (impl_->pending.size() < 64) impl_->pending.push_back({clip, volume, position});
}

void AudioSystem::Impl::playNow(const Request& request) {
    auto& impl = *this;
    const auto& clip = request.clip;
    const float volume = request.volume;
    const auto& position = request.position;
    if (!impl.ensureEngine()) return;
    impl.clearOneShots(false);
    if (static_cast<int>(impl.oneShots.size()) >= maxOneShots) {
        // Robo de voz: se descarta el más lejano al oyente (los que no tienen posición cuentan como cercanos).
        const auto far = std::max_element(impl.oneShots.begin(), impl.oneShots.end(), [&](const auto& a, const auto& b) {
            const float da = a.position ? distanceSquared(*a.position, impl.listener.position) : 0.0f;
            const float db = b.position ? distanceSquared(*b.position, impl.listener.position) : 0.0f;
            return da < db;
        });
        ma_sound_uninit(far->sound.get());
        impl.oneShots.erase(far);
    }
    auto sound = impl.load(clip, position.has_value());
    if (!sound) return;
    ma_sound_set_volume(sound.get(), volume);
    if (position) {
        ma_sound_set_position(sound.get(), position->x, position->y, position->z);
        ma_sound_set_min_distance(sound.get(), 1.5f);
        ma_sound_set_max_distance(sound.get(), 40.0f);
    }
    ma_sound_start(sound.get());
    impl.oneShots.push_back({std::move(sound), position});
}

void AudioSystem::flush() {
    auto requests = std::move(impl_->pending);
    impl_->pending.clear();
    for (const auto& request : requests) impl_->playNow(request);
}

void AudioSystem::playEmitter(EntityId entity) {
    const auto it = impl_->emitters.find(entity);
    if (it == impl_->emitters.end() || !it->second.sound) return;
    if (!it->second.settings.loop) ma_sound_seek_to_pcm_frame(it->second.sound.get(), 0);
    ma_sound_start(it->second.sound.get());
}

void AudioSystem::stopEmitter(EntityId entity) {
    const auto it = impl_->emitters.find(entity);
    if (it != impl_->emitters.end() && it->second.sound) ma_sound_stop(it->second.sound.get());
}

AudioSystem::Stats AudioSystem::stats() const {
    Stats stats;
    stats.device = impl_->initialized;
    stats.emitters = static_cast<int>(impl_->emitters.size());
    for (const auto& [id, emitter] : impl_->emitters) {
        if (emitter.sound && ma_sound_is_playing(emitter.sound.get())) ++stats.playing;
    }
    stats.oneShots = static_cast<int>(impl_->oneShots.size());
    return stats;
}

}
