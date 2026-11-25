#include "ge3d/History.hpp"
#include "ge3d/Materials.hpp"
#include "ge3d/Model.hpp"
#include "ge3d/Prefab.hpp"
#include "ge3d/Primitives.hpp"
#include "ge3d/SceneIO.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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

// Terreno de malla: generación, pesos, chunks compartidos, deshacer, guardado y copia independiente.
void testHeightmap() {
    ge3d::HeightmapSettings settings;
    settings.chunksX = 2;
    settings.chunksZ = 1;
    const auto a = ge3d::generateHeightmap(settings);
    const auto b = ge3d::generateHeightmap(settings);
    expect(a->heights() == b->heights() && a->allWeights() == b->allWeights(), "terreno de malla determinista");
    expect(a->verticesX() == 129 && a->verticesZ() == 65, "vertices del terreno de malla (celdas + 1)");
    const auto [low, high] = std::minmax_element(a->heights().begin(), a->heights().end());
    expect(*high - *low > 1.0f, "el generador crea relieve");

    // Los pesos de las capas siempre suman 255.
    auto weights = ge3d::singleLayer(0);
    for (int i = 0; i < 20; ++i) weights = ge3d::blendTowardLayer(weights, i % 4, 0.37f);
    int sum = 0;
    for (int layer = 0; layer < 4; ++layer) sum += ge3d::layerWeight(weights, layer);
    expect(sum == 255, "los pesos de las capas suman 255");
    expect(ge3d::blendTowardLayer(ge3d::singleLayer(0), 2, 1.0f) == ge3d::singleLayer(2), "pintar del todo deja una sola capa");

    // La superficie sigue la misma diagonal que la malla.
    auto flat = ge3d::makeFlatHeightmap(settings, 2.0f);
    flat->setHeight(1, 1, 4.0f);
    expect(std::abs(flat->surfaceHeight(0.5f, 0.5f) - 3.0f) < 1e-4f, "altura en la diagonal de la celda");
    expect(std::abs(flat->surfaceHeight(10.0f, 10.0f) - 2.0f) < 1e-4f, "altura lejos de la edicion");

    // Un vértice del borde entre chunks cambia los dos chunks; deshacer los devuelve a la vez.
    ge3d::Scene scene;
    auto entity = scene.makeEntity("Terreno");
    entity.heightmap = ge3d::HeightmapComponent{settings, ge3d::makeFlatHeightmap(settings)};
    const auto id = scene.add(entity);
    auto& map = *scene.find(id)->heightmap->data;
    const auto revisionRight = map.chunkRevision(1);
    const auto before0 = map.chunkData(0), before1 = map.chunkData(1);
    map.setHeight(64, 10, 3.0f);
    expect(map.chunkRevision(1) != revisionRight, "editar el borde marca el chunk vecino");
    ge3d::EditCommand command;
    command.label = "Elevar";
    command.heightmapChanges.push_back({id, 0, before0, map.chunkData(0)});
    command.heightmapChanges.push_back({id, 1, before1, map.chunkData(1)});
    ge3d::History history;
    history.record(command);
    history.undo(scene);
    expect(scene.find(id)->heightmap->data->height(64, 10) == 0.0f, "deshacer devuelve la altura del borde");
    history.redo(scene);
    expect(scene.find(id)->heightmap->data->height(64, 10) == 3.0f, "rehacer vuelve a aplicar la altura");

    // Play trabaja sobre una copia: sus cambios no tocan el terreno de edición.
    auto copy = scene.deepCopy();
    copy.find(id)->heightmap->data->setHeight(5, 5, 9.0f);
    expect(scene.find(id)->heightmap->data->height(5, 5) == 0.0f, "la copia de Play no comparte alturas");

    // Guardado: alturas en 1/64 m y pesos exactos.
    map.setHeight(7, 3, -12.34f);
    map.setWeights(7, 3, ge3d::blendTowardLayer(ge3d::singleLayer(0), 3, 0.5f));
    const auto parsed = ge3d::parseHeightmap(ge3d::serializeHeightmap(map));
    expect(parsed->chunksX() == 2 && parsed->chunksZ() == 1, "tamano del terreno de malla tras guardar");
    expect(std::abs(parsed->height(7, 3) + 12.34f) < 1.0f / 64.0f && parsed->weights(7, 3) == map.weights(7, 3),
           "alturas y capas sobreviven al guardado");
    expect(throwsFormatError([] { (void)ge3d::parseHeightmap(R"({"format":"ge3d.heightmap","version":1,"chunks":[1,1],"heights":"AAA=","weights":""})"); }),
           "datos de terreno de malla incompletos se rechazan");

    // Escena v4: el componente se guarda con su configuración y sus capas.
    const auto text = ge3d::serializeScene(scene);
    const auto reloaded = ge3d::parseScene(text);
    const auto& component = reloaded.entities().front().heightmap;
    expect(component && component->settings == settings && component->data, "el componente de terreno de malla se guarda en la escena");
}

// Agua: el volumen se conserva, baja por la pendiente, llena un hoyo, cruza chunks y se guarda.
void testWater() {
    ge3d::HeightmapSettings settings;
    settings.chunksX = 2;
    settings.chunksZ = 1;
    settings.generator.water = false;
    auto map = ge3d::makeFlatHeightmap(settings, 0.0f);
    // Pendiente hacia +x y un hoyo en el chunk de la derecha.
    for (int z = 0; z < map->verticesZ(); ++z) {
        for (int x = 0; x < map->verticesX(); ++x) {
            float height = 4.0f - static_cast<float>(x) * 0.05f;
            if (std::hypot(static_cast<float>(x - 100), static_cast<float>(z - 32)) < 6.0f) height -= 3.0f;
            map->setHeight(x, z, height);
        }
    }
    const auto volume = [&] {
        double total = 0.0;
        for (const float depth : map->allWater()) total += depth;
        return total;
    };
    for (int z = 28; z <= 36; ++z) {
        for (int x = 8; x <= 16; ++x) map->setWater(x, z, 1.0f);
    }
    const double before = volume();
    ge3d::WaterSimulation simulation;
    simulation.wake(*map, 0);
    simulation.settle(*map, settings.cellSize, 4000.0);
    expect(std::abs(volume() - before) < before * 1e-3, "la simulacion conserva el volumen de agua");
    expect(map->water(10, 32) < 0.05f, "el agua baja de la ladera");
    expect(map->water(100, 32) > 0.3f, "el agua llena el hoyo del chunk vecino");
    expect(simulation.awakeChunks() == 0, "el agua quieta duerme sus chunks");

    // Generar con mar: agua hasta el nivel y arena en la orilla.
    ge3d::HeightmapSettings sea;
    sea.chunksX = 1;
    sea.chunksZ = 1;
    sea.generator.waterLevel = 0.0f;
    const auto generated = ge3d::generateHeightmap(sea);
    bool levelOk = true, any = false;
    for (int z = 0; z < generated->verticesZ(); ++z) {
        for (int x = 0; x < generated->verticesX(); ++x) {
            const float depth = generated->water(x, z);
            if (depth > 0.0f) {
                any = true;
                levelOk = levelOk && std::abs(generated->height(x, z) + depth) < 1e-3f;
            }
        }
    }
    expect(any && levelOk, "el mar llena hasta el nivel del agua");
    const auto parsed = ge3d::parseHeightmap(ge3d::serializeHeightmap(*map));
    expect(std::abs(parsed->water(100, 32) - map->water(100, 32)) < 1.0f / 64.0f, "el agua se guarda y se recupera");
}

// Selección múltiple: borrar, duplicar, pegar y reordenar son un solo paso de deshacer.
void testMultiCommands() {
    auto scene = ge3d::makeStarterScene();
    ge3d::History history;
    const auto ids = std::vector<ge3d::EntityId>{scene.entities()[0].id, scene.entities()[2].id};
    const auto names = std::vector<std::string>{scene.entities()[0].name, scene.entities()[1].name, scene.entities()[2].name};
    history.execute(scene, *ge3d::makeDeleteCommand(scene, ids));
    expect(scene.entities().size() == 1 && scene.entities()[0].name == names[1], "borrar varias entidades a la vez");
    history.undo(scene);
    expect(scene.entities().size() == 3 && scene.entities()[0].name == names[0] && scene.entities()[2].name == names[2],
           "deshacer el borrado multiple devuelve cada entidad a su sitio");

    std::vector<ge3d::EntityId> created;
    history.execute(scene, *ge3d::makeDuplicateCommand(scene, ids, {0, 0, 0}, &created));
    expect(scene.entities().size() == 5 && created.size() == 2 && scene.find(created[0]) && scene.find(created[1]),
           "duplicar varias entidades");
    history.undo(scene);
    expect(scene.entities().size() == 3, "deshacer el duplicado multiple");

    const std::vector<ge3d::Entity> clipboard{scene.entities()[1]};
    created.clear();
    history.execute(scene, ge3d::makePasteCommand(scene, clipboard, &created));
    expect(scene.entities().size() == 4 && scene.find(created[0])->uuid != clipboard[0].uuid &&
               scene.find(created[0])->name == clipboard[0].name,
           "pegar crea una copia con identidad nueva");

    const auto last = scene.entities().back().id;
    history.execute(scene, *ge3d::makeMoveCommand(scene, last, 0));
    expect(scene.entities().front().id == last, "reordenar mueve la entidad en la lista");
    history.undo(scene);
    expect(scene.entities().back().id == last, "deshacer el reordenado");

    auto before = std::vector<ge3d::Entity>{scene.entities()[0], scene.entities()[1]};
    scene.find(before[0].id)->transform.position.x += 3;
    scene.find(before[1].id)->active = false;
    history.record(*ge3d::makeModifyCommand(scene, before, "Mover dos"));
    history.undo(scene);
    expect(scene.find(before[0].id)->transform.position.x == before[0].transform.position.x && scene.find(before[1].id)->active,
           "modificar varias entidades se deshace de una vez");

    ge3d::Scene saved;
    auto hidden = saved.makeEntity("Oculta", ge3d::Primitive::box);
    hidden.active = false;
    saved.add(std::move(hidden));
    expect(!ge3d::parseScene(ge3d::serializeScene(saved)).entities().front().active, "una entidad inactiva se guarda asi");
}

// Jerarquía padre/hijo: transformaciones, guardado, duplicar y borrar con los hijos.
void testHierarchy() {
    ge3d::Scene scene;
    auto parent = scene.makeEntity("Padre", {{10, 0, 0}, {0, 90, 0}, {2, 2, 2}});
    const auto parentId = scene.add(parent);
    auto child = scene.makeEntity("Hijo", ge3d::Primitive::box, {{1, 0, 0}, {}, {1, 1, 1}});
    child.parent = parentId;
    const auto childId = scene.add(child);
    auto grandchild = scene.makeEntity("Nieto", ge3d::Primitive::box, {{0, 1, 0}, {}, {1, 1, 1}});
    grandchild.parent = childId;
    const auto grandchildId = scene.add(grandchild);

    // El hijo está 1 m en +X local; el padre gira 90 grados en Y (+X pasa a -Z) y escala x2.
    const auto world = ge3d::worldTransform(scene, *scene.find(childId));
    expect(std::abs(world.position.x - 10) < 1e-4f && std::abs(world.position.z + 2) < 1e-4f, "posicion del hijo en el mundo");
    expect(std::abs(world.rotationDegrees.y - 90) < 1e-3f && std::abs(world.scale.x - 2) < 1e-5f, "rotacion y escala heredadas");
    const auto back = ge3d::localTransform(scene, parentId, world);
    expect(std::abs(back.position.x - 1) < 1e-4f && std::abs(back.position.z) < 1e-4f && std::abs(back.scale.x - 1) < 1e-5f,
           "del mundo a local devuelve la transformacion original");
    expect(ge3d::isAncestor(scene, parentId, grandchildId) && !ge3d::isAncestor(scene, grandchildId, parentId), "ancestros");
    expect(ge3d::descendantsOf(scene, parentId).size() == 2, "descendencia completa");
    scene.find(parentId)->active = false;
    expect(!ge3d::activeInHierarchy(scene, *scene.find(grandchildId)), "desactivar el padre desactiva a los hijos");
    scene.find(parentId)->active = true;

    // Guardado: el padre se guarda por UUID y se recupera aunque venga después en el archivo.
    auto loaded = ge3d::parseScene(ge3d::serializeScene(scene));
    const auto* loadedChild = &loaded.entities()[1];
    expect(loadedChild->parent == loaded.entities()[0].id && loaded.entities()[2].parent == loadedChild->id, "el padre se guarda");
    expect(throwsFormatError([] {
               (void)ge3d::parseScene(R"({"format":"ge3d.scene","version":7,"entities":[{"uuid":"11111111-1111-4111-8111-111111111111",)"
                                      R"("name":"A","parent":"22222222-2222-4222-8222-222222222222","transform":{"position":[0,0,0],)"
                                      R"("rotationDegrees":[0,0,0],"scale":[1,1,1]}}]})");
           }),
           "un padre inexistente se rechaza");

    // Duplicar el padre duplica a los hijos, colgando de la copia; borrar el padre borra a los hijos.
    ge3d::History history;
    std::vector<ge3d::EntityId> created;
    history.execute(scene, *ge3d::makeDuplicateCommand(scene, {parentId}, {0, 0, 0}, &created));
    expect(scene.entities().size() == 6 && created.size() == 1, "duplicar copia la descendencia");
    const auto copiedChildren = ge3d::descendantsOf(scene, created[0]);
    expect(copiedChildren.size() == 2 && scene.find(copiedChildren[0])->parent == created[0], "los hijos copiados cuelgan de la copia");
    history.undo(scene);
    history.execute(scene, *ge3d::makeDeleteCommand(scene, {parentId}));
    expect(scene.entities().empty(), "borrar el padre borra a los hijos");
    history.undo(scene);
    expect(scene.entities().size() == 3 && scene.find(grandchildId)->parent == childId, "deshacer recupera la jerarquia");
}

// Prefabs: instanciar, propagar cambios respetando lo modificado en cada instancia, añadir, quitar y revertir.
void testPrefabs() {
    ge3d::Scene scene;
    auto lamp = scene.makeEntity("Farol", ge3d::Primitive::box, {{5, 0, 5}, {}, {0.3f, 3, 0.3f}});
    const auto lampId = scene.add(lamp);
    auto light = scene.makeEntity("Luz", ge3d::Primitive::box, {{0, 1.6f, 0}, {}, {1, 0.2f, 1}});
    light.parent = lampId;
    scene.add(light);

    std::map<ge3d::EntityId, ge3d::Uuid> mapping;
    auto prefab = ge3d::makePrefabContent(scene, lampId, &mapping);
    expect(prefab.entities().size() == 2 && ge3d::prefabRoot(prefab) && ge3d::prefabRoot(prefab)->name == "Farol", "contenido del prefab");
    expect(ge3d::prefabRoot(prefab)->transform.position == ge3d::Vec3{}, "la raiz del prefab queda en el origen");
    // El formato de prefab es el de una escena.
    prefab = ge3d::parseScene(ge3d::serializeScene(prefab));

    ge3d::Scene level;
    ge3d::History history;
    ge3d::EntityId a{}, b{};
    history.execute(level, ge3d::makeInstantiateCommand(level, prefab, "prefabs/farol.prefab.json", {{1, 0, 0}, {}, {0.3f, 3, 0.3f}}, 0, &a));
    history.execute(level, ge3d::makeInstantiateCommand(level, prefab, "prefabs/farol.prefab.json", {{9, 0, 0}, {}, {0.3f, 3, 0.3f}}, 0, &b));
    expect(level.entities().size() == 4 && ge3d::descendantsOf(level, a).size() == 1, "dos instancias con su hijo");
    expect(ge3d::prefabInstanceRoot(level, ge3d::descendantsOf(level, b)[0]) == b, "raiz de la instancia");

    // La instancia B cambia el material de su luz: es un override.
    const auto lightB = ge3d::descendantsOf(level, b)[0];
    level.find(lightB)->shape->materials.fill("ladrillo");
    ge3d::refreshPrefabOverrides(level, "prefabs/farol.prefab.json", prefab);
    const auto& overrides = level.find(lightB)->prefab->overrides;
    expect(overrides.size() == 1 && overrides[0] == "shape", "un cambio en la instancia queda como override");
    expect(level.find(b)->prefab->overrides.empty(), "mover la raiz no es un override");

    // El prefab cambia: nombre de la luz, material y un hijo nuevo.
    auto updated = prefab;
    auto* prefabLight = updated.find(updated.entities()[1].id);
    prefabLight->name = "Bombilla";
    prefabLight->shape->materials.fill("yeso");
    auto extra = updated.makeEntity("Cartel", ge3d::Primitive::box);
    extra.parent = ge3d::prefabRoot(updated)->id;
    updated.add(extra);
    auto sync = ge3d::makePrefabSyncCommand(level, "prefabs/farol.prefab.json", updated);
    expect(sync.has_value(), "actualizar el prefab genera cambios");
    history.execute(level, *sync);
    const auto lightA = ge3d::descendantsOf(level, a)[0];
    expect(level.find(lightA)->name == "Bombilla" && level.find(lightA)->shape->materials[0] == "yeso", "la instancia A recibe los cambios");
    expect(level.find(lightB)->name == "Bombilla" && level.find(lightB)->shape->materials[0] == "ladrillo",
           "la instancia B conserva su override y recibe el resto");
    expect(ge3d::descendantsOf(level, a).size() == 2 && ge3d::descendantsOf(level, b).size() == 2, "el hijo nuevo aparece en las instancias");
    expect(level.find(a)->transform.position.x == 1 && level.find(b)->transform.position.x == 9, "cada instancia conserva su sitio");

    // Revertir el override de B.
    history.execute(level, *ge3d::makeRevertCommand(level, lightB, updated, "shape"));
    expect(level.find(lightB)->shape->materials[0] == "yeso" && level.find(lightB)->prefab->overrides.empty(), "revertir al prefab");

    // Aplicar: el contenido sale de la instancia con los mismos UUID del prefab.
    level.find(lightA)->name = "Luz grande";
    std::map<ge3d::EntityId, ge3d::Uuid> links;
    const auto applied = ge3d::prefabFromInstance(level, a, &links);
    expect(applied.entities().size() == 3 && links.empty() && applied.entities()[1].name == "Luz grande" &&
               applied.entities()[1].uuid == level.find(lightA)->prefab->source,
           "aplicar la instancia conserva las identidades del prefab");

    // Quitar del prefab el cartel lo quita de las instancias; deshacer lo devuelve.
    auto smaller = updated;
    smaller.remove(smaller.entities().back().id);
    history.execute(level, *ge3d::makePrefabSyncCommand(level, "prefabs/farol.prefab.json", smaller));
    expect(ge3d::descendantsOf(level, a).size() == 1, "lo que el prefab quita desaparece de las instancias");
    history.undo(level);
    expect(ge3d::descendantsOf(level, a).size() == 2, "deshacer la actualizacion");
    const auto reloaded = ge3d::parseScene(ge3d::serializeScene(level));
    expect(reloaded.entities().front().prefab && reloaded.entities().front().prefab->asset == "prefabs/farol.prefab.json",
           "el enlace al prefab se guarda");
}

// Modelos glTF: importación, orientación, colores, texturas y errores.
void testModels() {
    const std::string content = GE3D_CONTENT_DIR;
    const auto villager = ge3d::loadModel(content + "/models/aldeano.gltf");
    expect(villager.triangleCount() > 50 && villager.parts.size() == 1, "el aldeano se importa");
    expect(std::abs(villager.boundsMin.y) < 1e-4f && villager.boundsMax.y > 1.6f && villager.boundsMax.y < 1.8f,
           "pies en y = 0 y 1,7 m de alto");
    // Los ojos (color casi negro) estaban en +Z en glTF: tras importar quedan delante, en -Z.
    bool eyesInFront = false;
    for (const auto& vertex : villager.vertices) {
        if ((vertex.color & 0xFFu) < 0x30u && vertex.position[1] > 1.4f) eyesInFront = eyesInFront || vertex.position[2] < -0.1f;
    }
    expect(eyesInFront, "el modelo mira hacia delante (-Z)");
    const auto barrel = ge3d::loadModel(content + "/models/barril.gltf");
    expect(barrel.textures.size() == 1 && barrel.textures[0].width == 16 && barrel.materials[0].texture == 0, "textura incrustada");

    namespace fs = std::filesystem;
    const auto broken = fs::temp_directory_path() / "ge3d_roto.gltf";
    { std::ofstream(broken) << "{ esto no es json"; }
    bool failed = false;
    try {
        (void)ge3d::loadModel(broken);
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()).find("ge3d_roto.gltf") != std::string::npos;
    }
    expect(failed, "un glTF roto da un error con su nombre");
    fs::remove(broken);

    ge3d::ModelLibrary library(content);
    const auto shared = library.get("models/arbol.gltf");
    expect(shared && library.get("models/arbol.gltf") == shared, "la biblioteca comparte el modelo cargado");
    expect(!library.get("models/no_existe.gltf") && !library.lastError("models/no_existe.gltf").empty(), "modelo inexistente");
    expect(!ge3d::isValidModelPath("../fuera.gltf") && ge3d::isValidModelPath("models/a.glb"), "rutas de modelo");

    ge3d::Scene scene;
    auto entity = scene.makeEntity("Casa");
    entity.model = ge3d::ModelComponent{"models/casita.gltf", ge3d::ColliderShape::box};
    scene.add(std::move(entity));
    const auto loaded = ge3d::parseScene(ge3d::serializeScene(scene));
    expect(loaded.entities()[0].model && loaded.entities()[0].model->collider == ge3d::ColliderShape::box, "el componente Modelo se guarda");

    // Cámara del jugador: tercera persona por defecto; primera persona y distancia se guardan.
    ge3d::Scene players;
    auto player = players.makeEntity("Jugador");
    player.player = ge3d::PlayerComponent{};
    expect(player.player->camera == ge3d::PlayerCamera::thirdPerson, "el jugador empieza en tercera persona");
    player.player->camera = ge3d::PlayerCamera::firstPerson;
    player.player->cameraDistance = 6.5f;
    players.add(std::move(player));
    const auto reloaded = ge3d::parseScene(ge3d::serializeScene(players));
    const auto& saved = reloaded.entities()[0].player;
    expect(saved && saved->camera == ge3d::PlayerCamera::firstPerson && saved->cameraDistance == 6.5f, "la camara del jugador se guarda");
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
        auto hills = scene.makeEntity("Colinas");
        ge3d::HeightmapSettings hillSettings;
        hillSettings.chunksX = 1;
        hillSettings.chunksZ = 1;
        hills.heightmap = ge3d::makeHeightmap(hillSettings);
        hills.heightmap->data->setHeight(10, 20, 33.0f);
        scene.add(hills);
        scene.add(terrain);
        ge3d::saveSceneFile(scene, path);
        auto temporary = path;
        temporary += ".tmp";
        expect(!fs::exists(temporary), "no queda archivo temporal");
        std::vector<std::string> warnings;
        const auto loaded = ge3d::loadSceneFile(path, &warnings);
        expect(loaded.entities().size() == 6 && warnings.empty(), "volver a guardar reemplaza el archivo");
        const auto& loadedHills = loaded.entities()[4];
        expect(loadedHills.heightmap && loadedHills.heightmap->data && loadedHills.heightmap->data->height(10, 20) == 33.0f,
               "el terreno de malla editado se guarda y se recupera");
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
    testMultiCommands();
    testHierarchy();
    testPrefabs();
    testModels();
    testSerialization();
    testVoxels();
    testTerrainHistory();
    testMaterialsAndImages();
    testHeightmap();
    testWater();
    testFiles();
    if (failures > 0) {
        std::cerr << failures << " comprobaciones fallidas\n";
        return 1;
    }
    std::cout << "runtime_tests: todo correcto\n";
    return 0;
}
