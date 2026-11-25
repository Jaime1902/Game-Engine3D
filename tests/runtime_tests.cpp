#include "ge3d/History.hpp"
#include "ge3d/Materials.hpp"
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
    for (const auto primitive : {ge3d::Primitive::box, ge3d::Primitive::ramp}) {
        const auto mesh = ge3d::makePrimitive(primitive);
        for (const auto& triangle : mesh.triangles) {
            expect(triangle.face < ge3d::faceCount(primitive), "cada triangulo pertenece a una cara pintable");
        }
    }
    expect(ge3d::faceCount(ge3d::Primitive::ramp) <= ge3d::maxFaces, "la rampa cabe en el array de caras");
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
    expect(copy.shape && copy.shape->primitive == ge3d::Primitive::ramp && copy.transform.position.x == 6,
           "el duplicado conserva forma y se desplaza");

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
    scene.find(idOf(scene, "Crate"))->shape->materials = {"piedra", "cesped", "ladrillo", "teja", "agua", "hojas"};
    scene.find(idOf(scene, "Floor"))->shape.reset();  // entidad sin componentes: solo transformación
    scene.create("Puerta \xC3\xB1", ge3d::Primitive::box, {{1, 2, 3}, {10, 20, 30}, {0.5f, 2, 0.1f}});

    const auto text = ge3d::serializeScene(scene);
    expect(text.find("0.3,") != std::string::npos || text.find("0.3\n") != std::string::npos, "floats escritos de forma legible");
    expect(text.find("0.30000001") == std::string::npos, "sin ruido decimal en los floats");

    const auto loaded = ge3d::parseScene(text);
    expect(loaded.entities().size() == scene.entities().size(), "la escena cargada tiene las mismas entidades");
    for (std::size_t i = 0; i < std::min(loaded.entities().size(), scene.entities().size()); ++i) {
        const auto& a = scene.entities()[i];
        const auto& b = loaded.entities()[i];
        expect(a.uuid == b.uuid && a.name == b.name && a.transform == b.transform && a.shape == b.shape,
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

    // Migración: una escena v1 (sin colores) recibe el material equivalente al color por defecto.
    const auto migrated = ge3d::parseScene(document(entity(uuid, "ramp", "[1,1,1]")));
    expect(migrated.entities()[0].shape &&
               migrated.entities()[0].shape->materials[0] == ge3d::materialForPaletteColor(17),
           "una escena v1 se migra a materiales");
    const auto withFaces = [&](const std::string& faces) {
        return R"({"format":"ge3d.scene","version":2,"entities":[{"uuid":")" + uuid +
               R"(","name":"A","primitive":"box","position":[0,0,0],"rotationDegrees":[0,0,0],"scale":[1,1,1])" +
               faces + "}]}";
    };
    const auto painted = ge3d::parseScene(withFaces(R"(,"faces":[0,1,2,9,15,23])"));
    expect(painted.entities()[0].shape->materials ==
               ge3d::FaceMaterials{"piedra", "piedra", "piedra", "ladrillo", "cesped", "teja"},
           "una escena v2 convierte cada color en un material");
    for (const auto& faces : {std::string(""), std::string(R"(,"faces":[0,1,2,3,4])"),
                              std::string(R"(,"faces":[0,1,2,3,4,24])"), std::string(R"(,"faces":[0,1,2,3,4,-1])"),
                              std::string(R"(,"faces":[0,1,2,3,4,"rojo"])")}) {
        expect(throwsFormatError([&] { (void)ge3d::parseScene(withFaces(faces)); }), "caras invalidas en v2 se rechazan");
    }

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

    // v3: componentes. Material con mayúsculas y terreno con tamaño fuera de rango se rechazan.
    const auto v3 = [&](const std::string& components) {
        return R"({"format":"ge3d.scene","version":3,"entities":[{"uuid":")" + uuid +
               R"(","name":"A","transform":{"position":[0,0,0],"rotationDegrees":[0,0,0],"scale":[1,1,1]})" +
               components + "}]}";
    };
    expect(!ge3d::parseScene(v3("")).entities()[0].shape, "una entidad v3 puede no tener componentes");
    expect(throwsFormatError([&] { (void)ge3d::parseScene(v3(R"(,"shape":{"primitive":"box","materials":["Piedra","a","a","a","a","a"]})")); }),
           "un id de material invalido se rechaza");
    expect(throwsFormatError([&] { (void)ge3d::parseScene(v3(R"(,"terrain":{"chunks":[99,1,1],"voxelSize":1,"generator":{}})")); }),
           "un terreno fuera de rango se rechaza");
}

ge3d::TerrainSettings smallTerrain() {
    ge3d::TerrainSettings settings;
    settings.chunksX = 2;
    settings.chunksY = 1;
    settings.chunksZ = 1;
    return settings;
}

void testVoxels() {
    ge3d::VoxelGrid grid(2, 1, 1);
    const auto stone = grid.materialIndex("piedra");
    expect(stone == 1 && grid.materialIndex("piedra") == 1, "los materiales del terreno no se repiten");
    expect(grid.get(-1, 0, 0) == 0 && grid.get(64, 0, 0) == 0, "fuera de la rejilla es aire");
    const auto revision = grid.chunkRevision(1);
    expect(grid.set(31, 5, 5, stone), "colocar un voxel cambia la rejilla");
    expect(grid.chunkRevision(1) != revision, "editar el borde marca el chunk vecino para remallar");
    expect(!grid.set(31, 5, 5, stone), "colocar el mismo voxel no cambia nada");

    // Un vóxel aislado: 6 caras. Dos vóxeles contiguos en chunks distintos: la cara interior se oculta.
    ge3d::VoxelGrid single(1, 1, 1);
    single.set(4, 4, 4, single.materialIndex("piedra"));
    expect(ge3d::meshChunk(single, 0).quadCount() == 6, "un voxel aislado genera 6 caras");
    const auto before = ge3d::meshChunk(grid, 0).quadCount() + ge3d::meshChunk(grid, 1).quadCount();
    grid.set(32, 5, 5, stone);
    const auto after = ge3d::meshChunk(grid, 0).quadCount() + ge3d::meshChunk(grid, 1).quadCount();
    expect(before == 6 && after == 10, "las caras entre chunks vecinos se ocultan");

    // Greedy meshing: un bloque macizo de 8x4x8 del mismo material son 6 rectángulos (sin el suelo: 5 si toca y = 0).
    ge3d::VoxelGrid block(1, 1, 1);
    const auto sand = block.materialIndex("arena");
    for (int z = 2; z < 10; ++z) {
        for (int y = 2; y < 6; ++y) {
            for (int x = 2; x < 10; ++x) block.set(x, y, z, sand);
        }
    }
    expect(ge3d::meshChunk(block, 0).quadCount() == 6, "el greedy meshing fusiona caras iguales");
    block.set(5, 5, 5, block.materialIndex("cesped"));
    expect(ge3d::meshChunk(block, 0).quadCount() > 6, "materiales distintos no se fusionan");

    // Generador determinista y con capas.
    const auto settings = smallTerrain();
    const auto a = ge3d::generateTerrain(settings);
    const auto b = ge3d::generateTerrain(settings);
    expect(a->solidCount() > 0 && a->chunkData(0) == b->chunkData(0), "misma semilla, mismo terreno");
    auto other = settings;
    other.generator.seed = 99;
    expect(ge3d::generateTerrain(other)->chunkData(0) != a->chunkData(0), "otra semilla, otro terreno");

    // Guardado RLE: ida y vuelta exacta.
    const auto text = ge3d::serializeVoxels(*a);
    const auto loaded = ge3d::parseVoxels(text);
    expect(loaded->materials() == a->materials() && loaded->chunkData(0) == a->chunkData(0) &&
               loaded->chunkData(1) == a->chunkData(1),
           "los voxeles sobreviven a guardar y cargar");
    expect(text.size() < 64u * 32 * 32, "el formato de columnas comprime el terreno");
    for (const auto& broken : {std::string(R"({"format":"ge3d.terrain","version":1,"chunks":[1,1,1],"materials":[],"columns":"5:1"})"),
                               std::string(R"({"format":"ge3d.terrain","version":1,"chunks":[1,1,1],"materials":["a"],"columns":"40:1"})")}) {
        expect(throwsFormatError([&] { (void)ge3d::parseVoxels(broken); }), "datos de terreno invalidos se rechazan");
    }
}

void testTerrainHistory() {
    ge3d::Scene scene;
    auto entity = scene.makeEntity("Terreno");
    entity.terrain = ge3d::makeTerrain(smallTerrain());
    const auto id = scene.add(entity);
    auto& grid = *scene.find(id)->terrain->voxels;
    const auto original = grid.chunkData(0);

    ge3d::EditCommand command;
    command.label = "Excavar";
    auto changed = original;
    std::fill(changed.begin(), changed.begin() + 1000, ge3d::VoxelGrid::Voxel{0});
    command.terrainChanges.push_back({id, 0, original, changed});
    ge3d::History history;
    history.execute(scene, std::move(command));
    expect(grid.chunkData(0) == changed, "aplicar un cambio de terreno modifica el chunk");
    history.undo(scene);
    expect(grid.chunkData(0) == original, "deshacer recupera los voxeles");
    history.redo(scene);
    expect(grid.chunkData(0) == changed, "rehacer vuelve a aplicarlos");

    // Play trabaja con una copia profunda: editarla no toca la escena de edición.
    auto play = scene.deepCopy();
    play.find(id)->terrain->voxels->set(0, 31, 0, 1);
    expect(grid.get(0, 31, 0) == 0 && play.find(id)->terrain->voxels->get(0, 31, 0) == 1, "la copia de Play no comparte voxeles");

    // Duplicar un terreno le da voxeles propios.
    auto duplicate = ge3d::makeDuplicateCommand(scene, id, {0, 0, 0});
    history.execute(scene, std::move(*duplicate));
    const auto& copy = scene.entities().back();
    expect(copy.terrain && copy.terrain->voxels != scene.find(id)->terrain->voxels, "el terreno duplicado es independiente");
}

void testMaterialsAndImages() {
    const auto library = ge3d::parseMaterialLibrary(R"({"format":"ge3d.materials","version":1,"materials":[
        {"id":"cesped","name":"Cesped","texture":"textures/cesped_lado.png","top":"textures/cesped.png","tint":"#80ff40"},
        {"id":"piedra","texture":"textures/piedra.png"}]})");
    expect(library.materials().size() == 2 && library.indexOf("piedra") == 1, "el catalogo de materiales se lee");
    const auto* grass = library.find("cesped");
    expect(grass && grass->topTexture() == "textures/cesped.png" && grass->bottomTexture() == "textures/cesped_lado.png" &&
               grass->tint == 0x80ff40,
           "texturas por orientacion y tinte");
    expect(ge3d::parseMaterialLibrary(ge3d::serializeMaterialLibrary(library)).materials() == library.materials(),
           "el catalogo sobrevive a guardar y cargar");
    for (const auto& broken : {std::string(R"({"format":"ge3d.materials","version":1,"materials":[{"id":"A B","texture":"x.png"}]})"),
                               std::string(R"({"format":"ge3d.materials","version":1,"materials":[{"id":"a","texture":"../x.png"}]})"),
                               std::string(R"({"format":"ge3d.materials","version":1,"materials":[{"id":"a","texture":"x.png"},{"id":"a","texture":"y.png"}]})")}) {
        expect(throwsFormatError([&] { (void)ge3d::parseMaterialLibrary(broken); }), "un catalogo invalido se rechaza");
    }

    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() / ("ge3d_png_" + ge3d::Uuid::generate().toString()) / "prueba.png";
    ge3d::Image image{4, 2, {}};
    for (int i = 0; i < 8; ++i) image.rgba.insert(image.rgba.end(), {static_cast<std::uint8_t>(i * 30), 10, 200, 255});
    try {
        ge3d::savePng(path, image);
        const auto loaded = ge3d::loadPng(path);
        expect(loaded.width == 4 && loaded.height == 2 && loaded.rgba == image.rgba, "un PNG sobrevive a guardar y cargar");
    } catch (const std::exception& error) {
        std::cerr << "excepcion: " << error.what() << '\n';
        expect(false, "guardar y cargar PNG");
    }
    std::error_code ignored;
    fs::remove_all(path.parent_path(), ignored);
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
        // Un terreno editado a mano: sus vóxeles se guardan aparte y vuelven intactos.
        auto terrain = scene.makeEntity("Terreno");
        terrain.terrain = ge3d::makeTerrain(smallTerrain());
        terrain.terrain->voxels->set(3, 31, 3, terrain.terrain->voxels->materialIndex("nieve"));
        const auto terrainUuid = terrain.uuid;
        scene.add(terrain);
        ge3d::saveSceneFile(scene, path);
        auto temporary = path;
        temporary += ".tmp";
        expect(!fs::exists(temporary), "no queda archivo temporal");
        std::vector<std::string> warnings;
        const auto loaded = ge3d::loadSceneFile(path, &warnings);
        expect(loaded.entities().size() == 5 && warnings.empty(), "volver a guardar reemplaza el archivo");
        const auto& loadedTerrain = loaded.entities().back();
        expect(loadedTerrain.uuid == terrainUuid && loadedTerrain.terrain && loadedTerrain.terrain->voxels &&
                   loadedTerrain.terrain->voxels->get(3, 31, 3) == loadedTerrain.terrain->voxels->findMaterial("nieve") &&
                   loadedTerrain.terrain->voxels->get(3, 31, 3) != 0,
               "las ediciones del terreno se guardan y se recuperan");
        fs::remove(path.parent_path() / ge3d::pathFromUtf8(ge3d::terrainDataPath(loadedTerrain)));
        warnings.clear();
        const auto regenerated = ge3d::loadSceneFile(path, &warnings);
        expect(warnings.size() == 1 && regenerated.entities().back().terrain->voxels,
               "un terreno sin datos se regenera con aviso");
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
    testVoxels();
    testTerrainHistory();
    testMaterialsAndImages();
    testFiles();
    if (failures > 0) {
        std::cerr << failures << " comprobaciones fallidas\n";
        return 1;
    }
    std::cout << "runtime_tests: todo correcto\n";
    return 0;
}
