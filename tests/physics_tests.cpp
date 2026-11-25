// Pruebas de la etapa 4: física (Jolt) del mundo y del jugador, zonas, agua y audio (miniaudio).
#include "ge3d/Audio.hpp"
#include "ge3d/Physics.hpp"
#include "ge3d/SceneIO.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FALLO: " << what << '\n';
        ++failures;
    }
}

void simulate(ge3d::PhysicsWorld& world, ge3d::Scene& scene, float seconds, ge3d::CharacterInput input = {}) {
    for (float t = 0; t < seconds; t += 1.0f / 60.0f) world.step(scene, 1.0f / 60.0f, input);
}

ge3d::EntityId addPiece(ge3d::Scene& scene, const char* name, ge3d::Primitive primitive, ge3d::Vec3 position, ge3d::Vec3 scale,
                        std::optional<ge3d::BodyComponent> body = std::nullopt, ge3d::Vec3 rotation = {}) {
    auto entity = scene.makeEntity(name, primitive, {position, rotation, scale});
    entity.body = body;
    return scene.add(std::move(entity));
}

ge3d::EntityId addPlayer(ge3d::Scene& scene, ge3d::Vec3 position) {
    auto entity = scene.makeEntity("Jugador", {position, {}, {1, 1, 1}});
    entity.player = ge3d::PlayerComponent{};
    return scene.add(std::move(entity));
}

void testRotations() {
    const auto q = ge3d::eulerToQuaternion({0, 90, 0});
    // Giro de 90 grados en Y (convención del editor): +X pasa a -Z. Componente del eje rotado.
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float rotatedXz = 2 * (x * z - w * y);
    expect(std::abs(rotatedXz + 1.0f) < 1e-4f, "90 grados en Y llevan +X a -Z");
    const ge3d::Vec3 angles{20, -35, 60};
    const auto back = ge3d::quaternionToEuler(ge3d::eulerToQuaternion(angles));
    expect(std::abs(back.x - angles.x) < 0.01f && std::abs(back.y - angles.y) < 0.01f && std::abs(back.z - angles.z) < 0.01f,
           "grados -> cuaternion -> grados");
}

void testFallingBox() {
    ge3d::Scene scene;
    addPiece(scene, "Suelo", ge3d::Primitive::box, {0, -0.5f, 0}, {20, 1, 20});
    const auto box = addPiece(scene, "Caja", ge3d::Primitive::box, {0, 5, 0}, {1, 1, 1}, ge3d::BodyComponent{});
    ge3d::PhysicsWorld world;
    world.build(scene);
    simulate(world, scene, 3.0f);
    const auto& t = scene.find(box)->transform;
    expect(std::abs(t.position.y - 0.5f) < 0.05f, "una caja dinamica cae y se apoya en el suelo");
    expect(world.stats().dynamic == 1, "estadisticas: un cuerpo dinamico");
}

void testCharacter() {
    ge3d::Scene scene;
    addPiece(scene, "Suelo", ge3d::Primitive::box, {0, -0.5f, 0}, {40, 1, 40});
    // Rampa de 4 m de largo que sube 2 m hacia -Z (su cara alta está en -Z).
    addPiece(scene, "Rampa", ge3d::Primitive::ramp, {0, 1, -6}, {3, 2, 4});
    const auto playerId = addPlayer(scene, {0, 0.05f, 0});
    ge3d::PhysicsWorld world;
    world.build(scene);
    expect(world.hasCharacter() && world.characterEntity() == playerId, "el componente Jugador crea el personaje");
    simulate(world, scene, 0.5f);
    expect(world.character().grounded, "el jugador queda de pie en el suelo");
    float highest = 0.0f;
    for (int i = 0; i < 120; ++i) {
        world.step(scene, 1.0f / 60.0f, {{0, 0, -1}});
        highest = std::max(highest, world.character().feet.y);
    }
    expect(highest > 1.4f, "el jugador sube la rampa");
}

void testWallPushAndZone() {
    ge3d::Scene scene;
    addPiece(scene, "Suelo", ge3d::Primitive::box, {0, -0.5f, 0}, {40, 1, 40});
    addPiece(scene, "Pared", ge3d::Primitive::box, {4, 1.5f, 0}, {1, 3, 6});
    const auto crate = addPiece(scene, "Caja", ge3d::Primitive::box, {-2, 0.5f, 0}, {1, 1, 1}, ge3d::BodyComponent{});
    auto zone = scene.makeEntity("Zona", {{0, 1, 5}, {}, {3, 2, 3}});
    zone.body = ge3d::BodyComponent{ge3d::BodyType::trigger};
    const auto zoneId = scene.add(std::move(zone));
    const auto playerId = addPlayer(scene, {0, 0.05f, 0});
    ge3d::PhysicsWorld world;
    world.build(scene);
    simulate(world, scene, 0.3f);

    simulate(world, scene, 2.0f, {{1, 0, 0}});
    expect(world.character().feet.x < 3.6f, "el jugador no atraviesa la pared");

    const float crateStart = scene.find(crate)->transform.position.x;
    simulate(world, scene, 2.5f, {{-1, 0, 0}});
    expect(scene.find(crate)->transform.position.x < crateStart - 0.5f, "el jugador empuja la caja");

    // Zona: atravesarla genera la entrada y la salida, con el jugador como "otro".
    (void)world.takeEvents();
    const auto here = world.character().feet;
    // Hacia el centro de la zona (0, 5) y más allá.
    ge3d::Vec3 direction{0.0f - here.x, 0, 5.0f - here.z};
    const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    direction = {direction.x / length, 0, direction.z / length};
    bool entered = false, left = false;
    for (int i = 0; i < 300; ++i) {
        world.step(scene, 1.0f / 60.0f, {direction});
        for (const auto& event : world.takeEvents()) {
            if (event.self != zoneId || event.other != playerId) continue;
            entered = entered || event.kind == ge3d::PhysicsEvent::Kind::triggerEnter;
            left = left || event.kind == ge3d::PhysicsEvent::Kind::triggerExit;
        }
    }
    expect(entered, "entrar en una zona genera triggerEnter");
    expect(left, "salir de una zona genera triggerExit");
    const auto feet = world.character().feet;
    const auto& position = scene.find(playerId)->transform.position;
    expect(std::abs(position.x - feet.x) < 1e-4f && std::abs(position.z - feet.z) < 1e-4f, "la entidad del jugador sigue al personaje");
}

void testWater() {
    ge3d::Scene scene;
    ge3d::HeightmapSettings settings;
    settings.chunksX = 1;
    settings.chunksZ = 1;
    settings.generator.water = false;
    auto terrain = scene.makeEntity("Lago", {{-32, 0, -32}, {}, {1, 1, 1}});
    terrain.heightmap = ge3d::HeightmapComponent{settings, ge3d::makeFlatHeightmap(settings, -4.0f)};
    for (int z = 0; z <= 64; ++z) {
        for (int x = 0; x <= 64; ++x) terrain.heightmap->data->setWater(x, z, 3.0f);   // superficie en y = -1
    }
    scene.add(std::move(terrain));
    const auto box = addPiece(scene, "Caja", ge3d::Primitive::box, {0, 2, 0}, {1, 1, 1}, ge3d::BodyComponent{});
    ge3d::PhysicsWorld world;
    world.build(scene);
    simulate(world, scene, 6.0f);
    const float y = scene.find(box)->transform.position.y;
    expect(y > -2.0f && y < 0.0f, "una caja de madera flota en el agua del terreno");
}

void testVoxelGround() {
    ge3d::Scene scene;
    ge3d::TerrainSettings settings;
    settings.chunksX = 1;
    settings.chunksY = 1;
    settings.chunksZ = 1;
    auto terrain = scene.makeEntity("Voxeles", {{-16, 0, -16}, {}, {1, 1, 1}});
    terrain.terrain = ge3d::TerrainComponent{settings, ge3d::makeFlatTerrain(settings, 4, "piedra")};
    scene.add(std::move(terrain));
    const auto box = addPiece(scene, "Caja", ge3d::Primitive::box, {0, 8, 0}, {1, 1, 1}, ge3d::BodyComponent{});
    ge3d::PhysicsWorld world;
    world.build(scene);
    simulate(world, scene, 3.0f);
    expect(std::abs(scene.find(box)->transform.position.y - 4.5f) < 0.05f, "una caja se apoya sobre el terreno de voxeles");
}

void testKinematicAndScripts() {
    ge3d::Scene scene;
    addPiece(scene, "Suelo", ge3d::Primitive::box, {0, -0.5f, 0}, {40, 1, 40});
    const auto lift = addPiece(scene, "Plataforma", ge3d::Primitive::box, {0, 0.25f, 0}, {2, 0.5f, 2},
                               ge3d::BodyComponent{ge3d::BodyType::kinematic});
    const auto box = addPiece(scene, "Caja", ge3d::Primitive::box, {0, 1.0f, 0}, {0.5f, 0.5f, 0.5f}, ge3d::BodyComponent{});
    ge3d::PhysicsWorld world;
    world.build(scene);
    simulate(world, scene, 1.0f);
    // Un script sube la plataforma cambiando su posición: la caja sube con ella.
    for (int i = 0; i < 120; ++i) {
        scene.find(lift)->transform.position.y += 0.02f;
        world.step(scene, 1.0f / 60.0f, {});
    }
    expect(scene.find(box)->transform.position.y > 2.5f, "una plataforma cinematica levanta la caja");
    world.applyImpulse(box, {0, 0, 20});
    simulate(world, scene, 0.2f);
    expect(world.velocity(box).has_value(), "velocidad de un cuerpo dinamico disponible para scripts");
    const auto hit = world.raycast({0, 10, 0}, {0, -1, 0}, 20.0f);
    expect(hit.has_value(), "un rayo hacia abajo encuentra algo");
}

void testAudio() {
    ge3d::Scene scene;
    auto emitter = scene.makeEntity("Fuente", ge3d::Primitive::box);
    emitter.audio = ge3d::AudioSourceComponent{"sounds/fuente.wav"};
    scene.add(std::move(emitter));
    std::string lastError;
    ge3d::AudioSystem audio(GE3D_CONTENT_DIR, [&](bool error, const std::string& message) {
        if (error) lastError = message;
    });
    audio.start(scene);
    audio.update(scene, {});
    auto stats = audio.stats();
    if (stats.device) {
        expect(stats.emitters == 1 && stats.playing == 1, "la fuente de audio suena al empezar");
        audio.preload({"sounds/paso.wav"});
        for (int i = 0; i < 40; ++i) audio.playOneShot("sounds/paso.wav", 0.1f, ge3d::Vec3{static_cast<float>(i), 0, 0});
        audio.update(scene, {});
        expect(audio.stats().oneShots > 0 && audio.stats().oneShots <= ge3d::AudioSystem::maxOneShots, "limite de voces de efectos");
        audio.playOneShot("sounds/no_existe.wav", 1.0f, std::nullopt);
        audio.update(scene, {});
        expect(!lastError.empty(), "un sonido que no existe se informa");
    } else {
        std::cout << "(sin dispositivo de audio: se omiten las comprobaciones de sonido)\n";
    }
    audio.stop();
    expect(audio.stats().emitters == 0, "stop retira los emisores");

    ge3d::Scene saved;
    auto entity = saved.makeEntity("Todo", ge3d::Primitive::box);
    entity.body = ge3d::BodyComponent{ge3d::BodyType::kinematic, 3, 0.2f, 0.5f};
    entity.audio = ge3d::AudioSourceComponent{"sounds/campana.wav", 0.5f, 1, 10, false, false, true};
    entity.player = ge3d::PlayerComponent{};
    saved.add(std::move(entity));
    const auto loaded = ge3d::parseScene(ge3d::serializeScene(saved));
    const auto& e = loaded.entities().front();
    expect(e.body && e.body->type == ge3d::BodyType::kinematic && e.body->mass == 3 && e.audio && e.audio->clip == "sounds/campana.wav" &&
               !e.audio->loop && e.player,
           "cuerpo, audio y jugador se guardan en la escena");
}

}

int main() {
    testRotations();
    testFallingBox();
    testCharacter();
    testWallPushAndZone();
    testWater();
    testVoxelGround();
    testKinematicAndScripts();
    testAudio();
    if (failures > 0) {
        std::cerr << failures << " comprobaciones fallidas\n";
        return 1;
    }
    std::cout << "physics_tests: todo correcto\n";
    return 0;
}
