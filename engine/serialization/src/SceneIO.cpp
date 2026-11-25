#include "ge3d/SceneIO.hpp"

#include "ge3d/Materials.hpp"
#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <system_error>

namespace ge3d {

namespace {

using Json = nlohmann::ordered_json;
constexpr const char* formatName = "ge3d.scene";

const char* primitiveName(Primitive primitive) {
    switch (primitive) {
    case Primitive::box: return "box";
    case Primitive::ramp: return "ramp";
    }
    return "box";
}

std::optional<Primitive> primitiveFromName(std::string_view name) {
    if (name == "box") return Primitive::box;
    if (name == "ramp") return Primitive::ramp;
    return std::nullopt;
}

// Convierte el float a su representación decimal más corta para que el JSON quede legible
// (0.3 en lugar de 0.30000001192092896) sin perder exactitud al volver a leerlo.
double tidy(float value) {
    char buffer[32];
    const auto written = std::to_chars(buffer, buffer + sizeof buffer, value);
    if (written.ec != std::errc{}) return value;
    double result = value;
    std::from_chars(buffer, written.ptr, result);
    return result;
}

Json vector(Vec3 value) {
    return Json::array({tidy(value.x), tidy(value.y), tidy(value.z)});
}

[[noreturn]] void fail(const std::string& message) {
    throw SceneFormatError(message);
}

const Json& member(const Json& object, const char* key, const std::string& where) {
    const auto it = object.find(key);
    if (it == object.end()) fail(where + ": falta \"" + key + "\"");
    return *it;
}

std::string readString(const Json& object, const char* key, const std::string& where) {
    const auto& value = member(object, key, where);
    if (!value.is_string()) fail(where + ": \"" + key + "\" debe ser texto");
    return value.get<std::string>();
}

double readNumber(const Json& object, const char* key, const std::string& where, double minimum, double maximum) {
    const auto& value = member(object, key, where);
    if (!value.is_number()) fail(where + ": \"" + key + "\" debe ser un numero");
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < minimum || number > maximum) {
        fail(where + ": \"" + key + "\" fuera de rango (" + std::to_string(minimum) + " a " + std::to_string(maximum) + ")");
    }
    return number;
}

long long readInteger(const Json& object, const char* key, const std::string& where, long long minimum, long long maximum) {
    const auto& value = member(object, key, where);
    if (!value.is_number_integer()) fail(where + ": \"" + key + "\" debe ser un entero");
    const auto number = value.get<long long>();
    if (number < minimum || number > maximum) {
        fail(where + ": \"" + key + "\" fuera de rango (" + std::to_string(minimum) + " a " + std::to_string(maximum) + ")");
    }
    return number;
}

Vec3 readVector(const Json& object, const char* key, const std::string& where) {
    const auto& value = member(object, key, where);
    if (!value.is_array() || value.size() != 3) fail(where + ": \"" + key + "\" debe ser una lista de 3 numeros");
    float parts[3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number()) fail(where + ": \"" + key + "\" debe contener solo numeros");
        const double number = value[i].get<double>();
        if (!std::isfinite(number) || std::abs(number) > 1.0e7) {
            fail(where + ": \"" + key + "\" contiene un numero fuera de rango");
        }
        parts[i] = static_cast<float>(number);
    }
    return {parts[0], parts[1], parts[2]};
}

MaterialId readMaterialId(const Json& value, const std::string& where) {
    if (!value.is_string() || !isValidMaterialId(value.get<std::string>())) {
        fail(where + ": material invalido (usa ids como \"piedra\")");
    }
    return value.get<std::string>();
}

Transform readTransform(const Json& object, const std::string& where) {
    Transform transform;
    transform.position = readVector(object, "position", where);
    transform.rotationDegrees = readVector(object, "rotationDegrees", where);
    transform.scale = readVector(object, "scale", where);
    const auto& scale = transform.scale;
    if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0) fail(where + ": la escala debe ser mayor que cero");
    return transform;
}

TerrainSettings readTerrainSettings(const Json& object, const std::string& where) {
    TerrainSettings settings;
    const auto& chunks = member(object, "chunks", where);
    if (!chunks.is_array() || chunks.size() != 3) fail(where + ": \"chunks\" debe ser una lista de 3 enteros");
    const long long limits[3] = {VoxelGrid::maxChunksXZ, VoxelGrid::maxChunksY, VoxelGrid::maxChunksXZ};
    int values[3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!chunks[i].is_number_integer() || chunks[i].get<long long>() < 1 || chunks[i].get<long long>() > limits[i]) {
            fail(where + ": \"chunks\" fuera de rango");
        }
        values[i] = static_cast<int>(chunks[i].get<long long>());
    }
    settings.chunksX = values[0];
    settings.chunksY = values[1];
    settings.chunksZ = values[2];
    settings.voxelSize = static_cast<float>(readNumber(object, "voxelSize", where, 0.05, 16.0));

    const auto& generator = member(object, "generator", where);
    if (!generator.is_object()) fail(where + ": \"generator\" debe ser un objeto");
    const std::string at = where + " (generador)";
    auto& g = settings.generator;
    g.seed = static_cast<std::uint32_t>(readInteger(generator, "seed", at, 0, 4294967295LL));
    g.baseHeight = static_cast<float>(readNumber(generator, "baseHeight", at, 0, 256));
    g.amplitude = static_cast<float>(readNumber(generator, "amplitude", at, 0, 256));
    g.scale = static_cast<float>(readNumber(generator, "scale", at, 1, 4096));
    g.octaves = static_cast<int>(readInteger(generator, "octaves", at, 1, 8));
    g.waterLevel = static_cast<int>(readInteger(generator, "waterLevel", at, 0, 256));
    g.top = readMaterialId(member(generator, "top", at), at);
    g.under = readMaterialId(member(generator, "under", at), at);
    g.deep = readMaterialId(member(generator, "deep", at), at);
    g.shore = readMaterialId(member(generator, "shore", at), at);
    g.water = readMaterialId(member(generator, "water", at), at);
    return settings;
}

Json terrainJson(const Entity& entity) {
    const auto& settings = entity.terrain->settings;
    const auto& g = settings.generator;
    Json generator = Json::object();
    generator["seed"] = g.seed;
    generator["baseHeight"] = tidy(g.baseHeight);
    generator["amplitude"] = tidy(g.amplitude);
    generator["scale"] = tidy(g.scale);
    generator["octaves"] = g.octaves;
    generator["waterLevel"] = g.waterLevel;
    generator["top"] = g.top;
    generator["under"] = g.under;
    generator["deep"] = g.deep;
    generator["shore"] = g.shore;
    generator["water"] = g.water;
    Json terrain = Json::object();
    terrain["chunks"] = Json::array({settings.chunksX, settings.chunksY, settings.chunksZ});
    terrain["voxelSize"] = tidy(settings.voxelSize);
    terrain["generator"] = std::move(generator);
    terrain["data"] = terrainDataPath(entity);
    return terrain;
}

}

MaterialId materialForPaletteColor(int index) {
    static constexpr const char* table[] = {
        "piedra", "piedra", "piedra", "adoquin", "yeso", "tablones", "tablones", "arena",
        "tablones", "ladrillo", "teja", "yeso", "arena", "arena", "hojas", "cesped",
        "cesped", "adoquin", "agua", "agua", "agua", "agua", "nieve", "teja",
    };
    if (index < 0 || index >= static_cast<int>(std::size(table))) return "piedra";
    return table[index];
}

std::string terrainDataPath(const Entity& entity) {
    return "terrains/" + entity.uuid.toString() + ".terrain.json";
}

// --- Escena --------------------------------------------------------------------------------------

std::string serializeScene(const Scene& scene) {
    Json entities = Json::array();
    for (const auto& entity : scene.entities()) {
        Json item = Json::object();
        item["uuid"] = entity.uuid.toString();
        item["name"] = entity.name;
        Json transform = Json::object();
        transform["position"] = vector(entity.transform.position);
        transform["rotationDegrees"] = vector(entity.transform.rotationDegrees);
        transform["scale"] = vector(entity.transform.scale);
        item["transform"] = std::move(transform);
        if (entity.shape) {
            Json shape = Json::object();
            shape["primitive"] = primitiveName(entity.shape->primitive);
            Json materials = Json::array();
            for (const auto& material : entity.shape->materials) materials.push_back(material);
            shape["materials"] = std::move(materials);
            item["shape"] = std::move(shape);
        }
        if (entity.terrain) item["terrain"] = terrainJson(entity);
        entities.push_back(std::move(item));
    }
    Json root = Json::object();
    root["format"] = formatName;
    root["version"] = sceneFormatVersion;
    root["entities"] = std::move(entities);
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

namespace {
Scene parseSceneData(std::string_view text, bool generateTerrains);
}

Scene parseScene(std::string_view text) {
    return parseSceneData(text, true);
}

namespace {
Scene parseSceneData(std::string_view text, bool generateTerrains) {
    const auto root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded()) fail("El archivo no es JSON valido");
    if (!root.is_object()) fail("La raiz del archivo debe ser un objeto JSON");

    const std::string where = "Escena";
    if (readString(root, "format", where) != formatName) fail("El archivo no es una escena de Game-Engine3D");
    const auto& version = member(root, "version", where);
    if (!version.is_number_integer()) fail("Escena: \"version\" debe ser un entero");
    const auto number = version.get<long long>();
    if (number < 1) fail("Escena: version " + std::to_string(number) + " invalida");
    if (number > sceneFormatVersion) {
        fail("La escena usa la version " + std::to_string(number) + " del formato y este editor solo entiende hasta la " +
             std::to_string(sceneFormatVersion));
    }

    const auto& entities = member(root, "entities", where);
    if (!entities.is_array()) fail("Escena: \"entities\" debe ser una lista");

    Scene scene;
    std::set<Uuid> seen;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& item = entities[i];
        const std::string at = "Entidad " + std::to_string(i + 1);
        if (!item.is_object()) fail(at + ": debe ser un objeto");

        Entity entity;
        const auto uuid = Uuid::parse(readString(item, "uuid", at));
        if (!uuid || uuid->isNil()) fail(at + ": \"uuid\" no es un UUID valido");
        if (!seen.insert(*uuid).second) fail(at + ": UUID repetido " + uuid->toString());
        entity.uuid = *uuid;
        entity.name = readString(item, "name", at);

        if (number <= 2) {
            // v1 y v2: pieza con transformación plana y colores de paleta (v2) o colores por defecto (v1).
            const auto primitiveText = readString(item, "primitive", at);
            const auto primitive = primitiveFromName(primitiveText);
            if (!primitive) fail(at + ": primitiva desconocida \"" + primitiveText + "\"");
            entity.transform = readTransform(item, at);
            ShapeComponent shape;
            shape.primitive = *primitive;
            if (number == 2) {
                const auto& faces = member(item, "faces", at);
                if (!faces.is_array() || faces.size() != maxFaces) fail(at + ": \"faces\" debe ser una lista de 6 colores");
                for (std::size_t face = 0; face < maxFaces; ++face) {
                    if (!faces[face].is_number_integer() || faces[face].get<long long>() < 0 || faces[face].get<long long>() > 23) {
                        fail(at + ": \"faces\" contiene un color fuera de la paleta (0 a 23)");
                    }
                    shape.materials[face] = materialForPaletteColor(static_cast<int>(faces[face].get<long long>()));
                }
            } else {
                shape.materials.fill(materialForPaletteColor(*primitive == Primitive::box ? 8 : 17));
            }
            entity.shape = std::move(shape);
        } else {
            const auto& transform = member(item, "transform", at);
            if (!transform.is_object()) fail(at + ": \"transform\" debe ser un objeto");
            entity.transform = readTransform(transform, at);
            if (item.contains("shape")) {
                const auto& shapeJson = item["shape"];
                const std::string shapeAt = at + " (forma)";
                if (!shapeJson.is_object()) fail(shapeAt + ": debe ser un objeto");
                const auto primitiveText = readString(shapeJson, "primitive", shapeAt);
                const auto primitive = primitiveFromName(primitiveText);
                if (!primitive) fail(shapeAt + ": primitiva desconocida \"" + primitiveText + "\"");
                const auto& materials = member(shapeJson, "materials", shapeAt);
                if (!materials.is_array() || materials.size() != maxFaces) {
                    fail(shapeAt + ": \"materials\" debe ser una lista de 6 materiales");
                }
                ShapeComponent shape;
                shape.primitive = *primitive;
                for (std::size_t face = 0; face < maxFaces; ++face) shape.materials[face] = readMaterialId(materials[face], shapeAt);
                entity.shape = std::move(shape);
            }
            if (item.contains("terrain")) {
                const auto& terrainJson = item["terrain"];
                const std::string terrainAt = at + " (terreno)";
                if (!terrainJson.is_object()) fail(terrainAt + ": debe ser un objeto");
                // Sin archivo, los vóxeles se generan con la configuración; loadSceneFile lee los guardados.
                const auto settings = readTerrainSettings(terrainJson, terrainAt);
                entity.terrain = generateTerrains ? makeTerrain(settings) : TerrainComponent{settings, nullptr};
            }
        }
        scene.add(std::move(entity));
    }
    return scene;
}
}

// --- Vóxeles ------------------------------------------------------------------------------------

std::string serializeVoxels(const VoxelGrid& grid) {
    std::string columns;
    columns.reserve(static_cast<std::size_t>(grid.sizeX()) * static_cast<std::size_t>(grid.sizeZ()) * 12);
    for (int z = 0; z < grid.sizeZ(); ++z) {
        for (int x = 0; x < grid.sizeX(); ++x) {
            if (x || z) columns += ';';
            // Tramos de abajo arriba; el aire del final se omite.
            int top = grid.sizeY();
            while (top > 0 && grid.get(x, top - 1, z) == 0) --top;
            bool first = true;
            for (int y = 0; y < top;) {
                const auto material = grid.get(x, y, z);
                int count = 1;
                while (y + count < top && grid.get(x, y + count, z) == material) ++count;
                if (!first) columns += ',';
                columns += std::to_string(count);
                columns += ':';
                columns += std::to_string(material);
                first = false;
                y += count;
            }
        }
    }
    Json root = Json::object();
    root["format"] = "ge3d.terrain";
    root["version"] = 1;
    root["chunks"] = Json::array({grid.chunksX(), grid.chunksY(), grid.chunksZ()});
    root["materials"] = grid.materials();
    root["columns"] = std::move(columns);
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::shared_ptr<VoxelGrid> parseVoxels(std::string_view text) {
    const auto root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) fail("Los datos del terreno no son JSON valido");
    const std::string where = "Terreno";
    if (readString(root, "format", where) != "ge3d.terrain") fail("El archivo no contiene datos de terreno");
    if (readInteger(root, "version", where, 1, 1) != 1) fail("Terreno: version no soportada");
    const auto& chunks = member(root, "chunks", where);
    if (!chunks.is_array() || chunks.size() != 3) fail("Terreno: \"chunks\" debe ser una lista de 3 enteros");
    const long long limits[3] = {VoxelGrid::maxChunksXZ, VoxelGrid::maxChunksY, VoxelGrid::maxChunksXZ};
    int counts[3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!chunks[i].is_number_integer() || chunks[i].get<long long>() < 1 || chunks[i].get<long long>() > limits[i]) {
            fail("Terreno: \"chunks\" fuera de rango");
        }
        counts[i] = static_cast<int>(chunks[i].get<long long>());
    }
    auto grid = std::make_shared<VoxelGrid>(counts[0], counts[1], counts[2]);

    const auto& materials = member(root, "materials", where);
    if (!materials.is_array() || materials.size() > VoxelGrid::maxMaterials) fail("Terreno: \"materials\" invalido");
    std::vector<std::string> ids;
    for (const auto& material : materials) ids.push_back(readMaterialId(material, where));
    grid->setMaterials(std::move(ids));

    const auto columns = readString(root, "columns", where);
    std::size_t position = 0;
    const auto readNumberAt = [&](char terminator1, char terminator2) -> long long {
        long long value = 0;
        const auto start = position;
        while (position < columns.size() && columns[position] >= '0' && columns[position] <= '9') {
            value = value * 10 + (columns[position] - '0');
            if (value > 1'000'000) fail("Terreno: numero demasiado grande en \"columns\"");
            ++position;
        }
        if (position == start) fail("Terreno: \"columns\" mal formado");
        if (position < columns.size() && columns[position] != terminator1 && columns[position] != terminator2) {
            fail("Terreno: \"columns\" mal formado");
        }
        return value;
    };
    const auto materialCount = static_cast<long long>(grid->materials().size());
    for (int z = 0; z < grid->sizeZ(); ++z) {
        for (int x = 0; x < grid->sizeX(); ++x) {
            if (x || z) {
                if (position >= columns.size() || columns[position] != ';') fail("Terreno: faltan columnas en \"columns\"");
                ++position;
            }
            int y = 0;
            while (position < columns.size() && columns[position] != ';') {
                const auto count = readNumberAt(':', ':');
                ++position;  // ':'
                const auto material = readNumberAt(',', ';');
                if (position < columns.size() && columns[position] == ',') ++position;
                if (count < 1 || y + count > grid->sizeY()) fail("Terreno: una columna supera la altura del terreno");
                if (material > materialCount) fail("Terreno: material fuera del catalogo del terreno");
                for (long long k = 0; k < count; ++k) grid->set(x, y++, z, static_cast<VoxelGrid::Voxel>(material));
            }
        }
    }
    if (position != columns.size()) fail("Terreno: sobran datos en \"columns\"");
    return grid;
}

// --- Archivos ------------------------------------------------------------------------------------

void writeTextFileAtomic(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) throw std::runtime_error("No se puede crear la carpeta " + pathToUtf8(path.parent_path()) + ": " + error.message());
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("No se puede escribir " + pathToUtf8(temporary));
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("Fallo al escribir " + pathToUtf8(temporary));
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        const auto message = error.message();
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("No se puede reemplazar " + pathToUtf8(path) + ": " + message);
    }
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("No se puede abrir " + pathToUtf8(path));
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) throw std::runtime_error("Fallo al leer " + pathToUtf8(path));
    return buffer.str();
}

void saveSceneFile(const Scene& scene, const std::filesystem::path& path) {
    // Primero los terrenos: si fallan, el archivo de escena anterior sigue apuntando a datos válidos.
    const auto folder = path.parent_path();
    for (const auto& entity : scene.entities()) {
        if (entity.terrain && entity.terrain->voxels) {
            writeTextFileAtomic(folder / pathFromUtf8(terrainDataPath(entity)), serializeVoxels(*entity.terrain->voxels));
        }
    }
    writeTextFileAtomic(path, serializeScene(scene));
}

Scene loadSceneFile(const std::filesystem::path& path, std::vector<std::string>* warnings) {
    Scene scene;
    try {
        scene = parseSceneData(readTextFile(path), false);
    } catch (const SceneFormatError& error) {
        throw SceneFormatError(pathToUtf8(path.filename()) + ": " + error.what());
    }
    const auto folder = path.parent_path();
    for (const auto& entity : scene.entities()) {
        if (!entity.terrain) continue;
        const auto dataPath = folder / pathFromUtf8(terrainDataPath(entity));
        std::error_code error;
        if (!std::filesystem::exists(dataPath, error)) {
            if (warnings) warnings->push_back("Terreno \"" + entity.name + "\" sin datos guardados: se genero con su configuracion");
            scene.find(entity.id)->terrain->voxels = generateTerrain(entity.terrain->settings);
            continue;
        }
        std::shared_ptr<VoxelGrid> voxels;
        try {
            voxels = parseVoxels(readTextFile(dataPath));
        } catch (const SceneFormatError& failure) {
            throw SceneFormatError(pathToUtf8(dataPath.filename()) + ": " + failure.what());
        }
        const auto& settings = entity.terrain->settings;
        if (voxels->chunksX() != settings.chunksX || voxels->chunksY() != settings.chunksY ||
            voxels->chunksZ() != settings.chunksZ) {
            throw SceneFormatError(pathToUtf8(dataPath.filename()) + ": el tamano no coincide con el de la escena");
        }
        scene.find(entity.id)->terrain->voxels = std::move(voxels);
    }
    return scene;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}
