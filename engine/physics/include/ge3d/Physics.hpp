#pragma once

#include "ge3d/Model.hpp"
#include "ge3d/Scene.hpp"
#include <functional>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ge3d {

// Suceso físico para los scripts. En los de zona, self es la zona y other quien entra o sale.
struct PhysicsEvent {
    enum class Kind { triggerEnter, triggerExit, collision };
    Kind kind{};
    EntityId self{};
    EntityId other{};
    float speed{};      // choques: velocidad relativa (m/s)
};

// Lo que el jugador quiere hacer este fotograma (dirección en el mundo, longitud <= 1).
struct CharacterInput {
    Vec3 move{};
    bool jump{};
    bool run{};
};

struct CharacterState {
    Vec3 feet{};        // posición de los pies
    Vec3 eye{};         // posición de los ojos (cámara)
    Vec3 velocity{};
    bool grounded{};
    bool inWater{};
};

// Mundo físico de una escena en Play (Jolt Physics, paso fijo de 60 Hz en el hilo principal).
//
//  - Piezas y terrenos sin Cuerpo físico: colisionadores estáticos. Si un script los mueve, se recolocan.
//  - Cuerpo dinámico: cae, choca, se empuja y flota en el agua de los terrenos de malla.
//  - Cinemático: lo mueve un script (o el Inspector) y empuja a los dinámicos.
//  - Zona (trigger): no choca; genera triggerEnter / triggerExit.
//  - Jugador: personaje de cápsula que sube rampas y escalones, salta, nada y empuja cajas.
// La escena manda: cada paso lee las transformaciones cambiadas por scripts o el editor y después escribe
// las de los cuerpos dinámicos.
class PhysicsWorld {
public:
    static constexpr float fixedStep = 1.0f / 60.0f;

    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // De dónde salen los modelos (para sus colisionadores). Sin esto, los modelos no chocan.
    using ModelSource = std::function<std::shared_ptr<const ModelData>(const std::string& path)>;
    void setModelSource(ModelSource source);

    void build(const Scene& scene);
    // Avanza el tiempo de un fotograma en pasos fijos y sincroniza la escena. input solo afecta al jugador.
    void step(Scene& scene, float frameSeconds, const CharacterInput& input);
    [[nodiscard]] std::vector<PhysicsEvent> takeEvents();

    [[nodiscard]] bool hasCharacter() const noexcept;
    [[nodiscard]] EntityId characterEntity() const noexcept;
    [[nodiscard]] CharacterState character() const;

    // Operaciones para los scripts (sobre cuerpos dinámicos).
    [[nodiscard]] std::optional<Vec3> velocity(EntityId entity) const;
    void setVelocity(EntityId entity, Vec3 velocity);
    void applyImpulse(EntityId entity, Vec3 impulse);
    // Primer cuerpo que corta el rayo (sin contar zonas ni el jugador): entidad y distancia.
    [[nodiscard]] std::optional<std::pair<EntityId, float>> raycast(Vec3 from, Vec3 direction, float maxDistance) const;

    struct Stats {
        int bodies{};
        int dynamic{};
        int active{};
        double stepMs{};
    };
    [[nodiscard]] Stats stats() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Rotación en grados con la convención del editor (primero X, luego Y, luego Z: R = Rz·Ry·Rx)
// a cuaternión (x, y, z, w) y vuelta.
[[nodiscard]] std::array<float, 4> eulerToQuaternion(Vec3 degrees);
[[nodiscard]] Vec3 quaternionToEuler(const std::array<float, 4>& quaternion);

}
