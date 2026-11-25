#include "ge3d/History.hpp"
#include "ge3d/Primitives.hpp"
#include "ge3d/SceneIO.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
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

template <class Function>
bool throwsFormatError(Function&& function) {
    try {
        function();
    } catch (const ge3d::SceneFormatError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

ge3d::EntityId idOf(const ge3d::Scene& scene, const std::string& name) {
    for (const auto& entity : scene.entities()) {
        if (entity.name == name) return entity.id;
    }
    return 0;
}

void testStarterGeometry() {
    const auto scene = ge3d::makeStarterScene();
    expect(scene.entities().size() == 3, "la escena inicial tiene 3 entidades");
    expect(scene.entities()[0].id != scene.entities()[1].id, "ids locales distintos");
    expect(scene.entities()[0].uuid != scene.entities()[1].uuid, "UUIDs distintos en la escena inicial");

    const auto box = ge3d::makePrimitive(ge3d::Primitive::box);
    const auto ramp = ge3d::makePrimitive(ge3d::Primitive::ramp);
    expect(box.triangles.size() == 12, "la caja tiene 12 triangulos");
    expect(ramp.triangles.size() == 8, "la rampa tiene 8 triangulos");
    for (const auto* mesh : {&box, &ramp}) {
        for (const auto& triangle : mesh->triangles) {
            for (const auto index : triangle.indices) {
                expect(index < mesh->positions.size(), "indices de malla dentro de rango");
            }
        }
    }
}

void testUuid() {
    const auto a = ge3d::Uuid::generate();
    const auto b = ge3d::Uuid::generate();
    expect(a != b, "dos UUID generados son distintos");
    const auto text = a.toString();
    expect(text.size() == 36 && text[8] == '-' && text[13] == '-' && text[18] == '-' && text[23] == '-',
           "UUID en formato canonico");
    expect(text[14] == '4', "UUID version 4");
    expect(std::string("89ab").find(text[19]) != std::string::npos, "UUID variante RFC 4122");
    expect(ge3d::Uuid::parse(text) == a, "UUID ida y vuelta");
    auto upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    expect(ge3d::Uuid::parse(upper) == a, "UUID acepta mayusculas");
    expect(!ge3d::Uuid::parse("no-es-un-uuid"), "UUID rechaza texto invalido");
    expect(!ge3d::Uuid::parse("0123456789abcdef0123456789abcdef0123"), "UUID rechaza texto sin guiones");
}

void testHistory() {
    auto scene = ge3d::makeStarterScene();
    const auto initial = scene.entities();
    const auto crate = idOf(scene, "Crate");
    const auto ramp = idOf(scene, "Ramp");
    ge3d::History history;
    expect(!history.canUndo() && history.state() == 0, "historial vacio al empezar");

    auto box = scene.makeEntity("Caja", ge3d::Primitive::box, {{3, 0.5f, 0}, {}, {1, 1, 1}});
    const auto boxId = box.id;
    history.execute(scene, ge3d::makeCreateCommand(scene, std::move(box)));
    expect(scene.entities().size() == 4 && scene.find(boxId), "crear anade la entidad");

    auto erase = ge3d::makeDeleteCommand(scene, crate);
    expect(erase.has_value(), "se puede borrar una entidad existente");
    history.execute(scene, std::move(*erase));
    expect(scene.entities().size() == 3 && !scene.find(crate), "borrar quita la entidad");

    const auto before = *scene.find(ramp);
    scene.find(ramp)->transform.position.x = 5;
    auto move = ge3d::makeModifyCommand(scene, before, "Mover");
    expect(move.has_value(), "un cambio de transformacion genera comando");
    history.record(std::move(*move));
    expect(!ge3d::makeModifyCommand(scene, *scene.find(ramp), "Nada"), "sin cambios no hay comando");

    auto duplicate = ge3d::makeDuplicateCommand(scene, ramp, {1, 0, 0});
    expect(duplicate.has_value(), "se puede duplicar");
    history.execute(scene, std::move(*duplicate));
    expect(scene.entities().size() == 4, "duplicar anade una entidad");
    const auto rampIndex = *scene.indexOf(ramp);
    const auto& copy = scene.entities()[rampIndex + 1];
    expect(copy.uuid != scene.find(ramp)->uuid && copy.id != ramp, "el duplicado tiene identidad nueva");
    expect(copy.primitive == ge3d::Primitive::ramp && copy.transform.position.x == 6, "el duplicado conserva forma y se desplaza");

    const auto finalState = history.state();
    while (history.undo(scene)) {}
    expect(scene.entities() == initial, "deshacer todo recupera la escena original");
    expect(history.state() == 0, "estado inicial tras deshacer todo");
    while (history.redo(scene)) {}
    expect(scene.entities().size() == 4 && scene.find(ramp)->transform.position.x == 5 && !scene.find(crate),
           "rehacer todo vuelve a aplicar los cambios");
    expect(history.state() == finalState, "el estado coincide tras rehacer");

    history.undo(scene);
    expect(history.canRedo(), "hay algo para rehacer");
    auto other = scene.makeEntity("Otra", ge3d::Primitive::box);
    history.execute(scene, ge3d::makeCreateCommand(scene, std::move(other)));
    expect(!history.canRedo(), "una edicion nueva descarta lo que habia para rehacer");
    expect(history.state() != finalState, "una edicion nueva cambia el estado");

    ge3d::History limited(2);
    auto small = ge3d::makeStarterScene();
    for (int i = 0; i < 5; ++i) {
        auto entity = small.makeEntity("E", ge3d::Primitive::box);
        limited.execute(small, ge3d::makeCreateCommand(small, std::move(entity)));
    }
    int undone = 0;
    while (limited.undo(small)) ++undone;
    expect(undone == 2 && small.entities().size() == 6, "el historial respeta su limite");
}

void testSerialization() {
    auto scene = ge3d::makeStarterScene();
    scene.find(idOf(scene, "Crate"))->transform.position.x = 0.3f;
    scene.find(idOf(scene, "Ramp"))->transform.rotationDegrees = {0, 45, 0};
    scene.create("Puerta \xC3\xB1", ge3d::Primitive::box, {{1, 2, 3}, {10, 20, 30}, {0.5f, 2, 0.1f}});

    const auto text = ge3d::serializeScene(scene);
    expect(text.find("0.3,") != std::string::npos || text.find("0.3\n") != std::string::npos, "floats escritos de forma legible");
    expect(text.find("0.30000001") == std::string::npos, "sin ruido decimal en los floats");

    const auto loaded = ge3d::parseScene(text);
    expect(loaded.entities().size() == scene.entities().size(), "la escena cargada tiene las mismas entidades");
    for (std::size_t i = 0; i < std::min(loaded.entities().size(), scene.entities().size()); ++i) {
        const auto& a = scene.entities()[i];
        const auto& b = loaded.entities()[i];
        expect(a.uuid == b.uuid && a.name == b.name && a.primitive == b.primitive && a.transform == b.transform,
               "cada entidad sobrevive a guardar y cargar");
    }
    expect(ge3d::serializeScene(loaded) == text, "guardar dos veces produce el mismo texto");

    const std::string uuid = "6f1c2a3b-4d5e-4f60-8a7b-9c0d1e2f3a4b";
    const auto entity = [&](const std::string& id, const std::string& primitive, const std::string& scale) {
        return R"({"uuid":")" + id + R"(","name":"A","primitive":")" + primitive +
               R"(","position":[0,0,0],"rotationDegrees":[0,0,0],"scale":)" + scale + "}";
    };
    const auto document = [](const std::string& entities, int version = 1) {
        return R"({"format":"ge3d.scene","version":)" + std::to_string(version) + R"(,"entities":[)" + entities + "]}";
    };
    expect(ge3d::parseScene(document(entity(uuid, "box", "[1,1,1]"))).entities().size() == 1, "escena minima valida");

    const std::vector<std::string> invalid = {
        "",
        "{",
        "[]",
        R"({"format":"otro","version":1,"entities":[]})",
        R"({"format":"ge3d.scene","entities":[]})",
        R"({"format":"ge3d.scene","version":1})",
        document("", 99),
        document("", 0),
        document(entity(uuid, "box", "[1,1,1]") + "," + entity(uuid, "box", "[1,1,1]")),
        document(entity(uuid, "esfera", "[1,1,1]")),
        document(entity(uuid, "box", "[1,0,1]")),
        document(entity(uuid, "box", "[1,1]")),
        document(entity("00000000-0000-0000-0000-000000000000", "box", "[1,1,1]")),
        document(entity("no-uuid", "box", "[1,1,1]")),
    };
    for (const auto& input : invalid) {
        expect(throwsFormatError([&] { (void)ge3d::parseScene(input); }), "una escena invalida se rechaza");
    }
}

void testFiles() {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() / ("ge3d_tests_" + ge3d::Uuid::generate().toString());
    const auto path = folder / "escenas" / "prueba.scene.json";
    auto scene = ge3d::makeStarterScene();
    try {
        ge3d::saveSceneFile(scene, path);
        expect(fs::exists(path), "guardar crea el archivo y sus carpetas");
        scene.create("Extra", ge3d::Primitive::ramp);
        ge3d::saveSceneFile(scene, path);
        auto temporary = path;
        temporary += ".tmp";
        expect(!fs::exists(temporary), "no queda archivo temporal");
        const auto loaded = ge3d::loadSceneFile(path);
        expect(loaded.entities().size() == 4, "volver a guardar reemplaza el archivo");
    } catch (const std::exception& error) {
        std::cerr << "excepcion: " << error.what() << '\n';
        expect(false, "guardar y cargar archivos");
    }
    bool missing = false;
    try {
        (void)ge3d::loadSceneFile(folder / "no-existe.scene.json");
    } catch (const std::runtime_error&) {
        missing = true;
    }
    expect(missing, "cargar un archivo inexistente falla con error");
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

}

int main() {
    testStarterGeometry();
    testUuid();
    testHistory();
    testSerialization();
    testFiles();
    if (failures > 0) {
        std::cerr << failures << " comprobaciones fallidas\n";
        return 1;
    }
    std::cout << "runtime_tests: todo correcto\n";
    return 0;
}
