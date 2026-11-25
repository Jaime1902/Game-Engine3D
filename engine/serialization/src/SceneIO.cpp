#include "ge3d/SceneIO.hpp"

#include "ge3d/Materials.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
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
constexpr float heightmapUnits = 64.0f;   // pasos por metro al guardar alturas

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

HeightmapSettings readHeightmapSettings(const Json& object, const std::string& where) {
    HeightmapSettings settings;
    const auto& chunks = member(object, "chunks", where);
    if (!chunks.is_array() || chunks.size() != 2) fail(where + ": \"chunks\" debe ser una lista de 2 enteros");
    for (const auto& value : chunks) {
        if (!value.is_number_integer() || value.get<long long>() < 1 || value.get<long long>() > Heightmap::maxChunks) {
            fail(where + ": \"chunks\" fuera de rango");
        }
    }
    settings.chunksX = static_cast<int>(chunks[0].get<long long>());
    settings.chunksZ = static_cast<int>(chunks[1].get<long long>());
    settings.cellSize = static_cast<float>(readNumber(object, "cellSize", where, 0.05, 16.0));
    const auto& layers = member(object, "layers", where);
    if (!layers.is_array() || layers.size() != Heightmap::layerCount) fail(where + ": \"layers\" debe ser una lista de 4 materiales");
    for (std::size_t i = 0; i < settings.layers.size(); ++i) settings.layers[i] = readMaterialId(layers[i], where);

    const auto& generator = member(object, "generator", where);
    if (!generator.is_object()) fail(where + ": \"generator\" debe ser un objeto");
    const std::string at = where + " (generador)";
    auto& g = settings.generator;
    g.seed = static_cast<std::uint32_t>(readInteger(generator, "seed", at, 0, 4294967295LL));
    g.amplitude = static_cast<float>(readNumber(generator, "amplitude", at, 0, Heightmap::maxHeight));
    g.scale = static_cast<float>(readNumber(generator, "scale", at, 1, 4096));
    g.octaves = static_cast<int>(readInteger(generator, "octaves", at, 1, 8));
    g.rockSlope = static_cast<float>(readNumber(generator, "rockSlope", at, 0, 1));
    // Agua (opcional: las primeras escenas v4 no la tenían).
    if (generator.contains("water")) {
        if (!generator["water"].is_boolean()) fail(at + ": \"water\" debe ser true o false");
        g.water = generator["water"].get<bool>();
    }
    if (generator.contains("waterLevel")) {
        g.waterLevel = static_cast<float>(readNumber(generator, "waterLevel", at, -Heightmap::maxHeight, Heightmap::maxHeight));
    }
    if (object.contains("waterMaterial")) settings.waterMaterial = readMaterialId(object["waterMaterial"], where);
    return settings;
}

Json heightmapJson(const Entity& entity) {
    const auto& settings = entity.heightmap->settings;
    const auto& g = settings.generator;
    Json generator = Json::object();
    generator["seed"] = g.seed;
    generator["amplitude"] = tidy(g.amplitude);
    generator["scale"] = tidy(g.scale);
    generator["octaves"] = g.octaves;
    generator["rockSlope"] = tidy(g.rockSlope);
    generator["water"] = g.water;
    generator["waterLevel"] = tidy(g.waterLevel);
    Json layers = Json::array();
    for (const auto& layer : settings.layers) layers.push_back(layer);
    Json heightmap = Json::object();
    heightmap["chunks"] = Json::array({settings.chunksX, settings.chunksZ});
    heightmap["cellSize"] = tidy(settings.cellSize);
    heightmap["layers"] = std::move(layers);
    heightmap["waterMaterial"] = settings.waterMaterial;
    heightmap["generator"] = std::move(generator);
    heightmap["data"] = heightmapDataPath(entity);
    return heightmap;
}

constexpr char base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::vector<std::uint8_t>& bytes) {
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
        const std::uint32_t c = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        text += base64Alphabet[(triple >> 18) & 63u];
        text += base64Alphabet[(triple >> 12) & 63u];
        text += i + 1 < bytes.size() ? base64Alphabet[(triple >> 6) & 63u] : '=';
        text += i + 2 < bytes.size() ? base64Alphabet[triple & 63u] : '=';
    }
    return text;
}

std::uint32_t base64Value(char c, const std::string& where) {
    if (c >= 'A' && c <= 'Z') return static_cast<std::uint32_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<std::uint32_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    fail(where + ": base64 mal formado");
}

std::vector<std::uint8_t> decodeBase64(const std::string& text, const std::string& where) {
    if (text.size() % 4 != 0) fail(where + ": base64 mal formado");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool pad2 = text[i + 2] == '=', pad3 = text[i + 3] == '=';
        if ((pad2 && !pad3) || ((pad2 || pad3) && i + 4 != text.size())) fail(where + ": base64 mal formado");
        const std::uint32_t triple = (base64Value(text[i], where) << 18) | (base64Value(text[i + 1], where) << 12) |
                                     (pad2 ? 0u : base64Value(text[i + 2], where) << 6) |
                                     (pad3 ? 0u : base64Value(text[i + 3], where));
        bytes.push_back(static_cast<std::uint8_t>(triple >> 16));
        if (!pad2) bytes.push_back(static_cast<std::uint8_t>(triple >> 8));
        if (!pad3) bytes.push_back(static_cast<std::uint8_t>(triple));
    }
    return bytes;
}

const char* bodyTypeName(BodyType type) {
    switch (type) {
    case BodyType::dynamic: return "dynamic";
    case BodyType::kinematic: return "kinematic";
    case BodyType::trigger: return "trigger";
    }
    return "dynamic";
}

bool readBool(const Json& object, const char* key, const std::string& where) {
    const auto& value = member(object, key, where);
    if (!value.is_boolean()) fail(where + ": \"" + key + "\" debe ser true o false");
    return value.get<bool>();
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

std::string heightmapDataPath(const Entity& entity) {
    return "terrains/" + entity.uuid.toString() + ".heightmap.json";
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
        if (entity.heightmap) item["heightmap"] = heightmapJson(entity);
        if (entity.body) {
            Json body = Json::object();
            body["type"] = bodyTypeName(entity.body->type);
            body["mass"] = tidy(entity.body->mass);
            body["friction"] = tidy(entity.body->friction);
            body["restitution"] = tidy(entity.body->restitution);
            item["body"] = std::move(body);
        }
        if (entity.audio) {
            const auto& a = *entity.audio;
            Json audio = Json::object();
            audio["clip"] = a.clip;
            audio["volume"] = tidy(a.volume);
            audio["minDistance"] = tidy(a.minDistance);
            audio["maxDistance"] = tidy(a.maxDistance);
            audio["loop"] = a.loop;
            audio["playOnStart"] = a.playOnStart;
            audio["spatial"] = a.spatial;
            item["audio"] = std::move(audio);
        }
        if (entity.player) {
            const auto& p = *entity.player;
            Json player = Json::object();
            player["height"] = tidy(p.height);
            player["radius"] = tidy(p.radius);
            player["walkSpeed"] = tidy(p.walkSpeed);
            player["runSpeed"] = tidy(p.runSpeed);
            player["jumpSpeed"] = tidy(p.jumpSpeed);
            player["mouseSensitivity"] = tidy(p.mouseSensitivity);
            item["player"] = std::move(player);
        }
        if (entity.script) {
            Json params = Json::object();
            for (const auto& [name, value] : entity.script->parameters) {
                if (const auto* number = std::get_if<double>(&value)) params[name] = *number;
                else if (const auto* flag = std::get_if<bool>(&value)) params[name] = *flag;
                else params[name] = std::get<std::string>(value);
            }
            Json script = Json::object();
            script["path"] = entity.script->path;
            script["enabled"] = entity.script->enabled;
            script["params"] = std::move(params);
            item["script"] = std::move(script);
        }
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
            if (number >= 6 && item.contains("body")) {
                const auto& json = item["body"];
                const std::string bodyAt = at + " (cuerpo fisico)";
                if (!json.is_object()) fail(bodyAt + ": debe ser un objeto");
                BodyComponent body;
                const auto type = readString(json, "type", bodyAt);
                if (type == "dynamic") body.type = BodyType::dynamic;
                else if (type == "kinematic") body.type = BodyType::kinematic;
                else if (type == "trigger") body.type = BodyType::trigger;
                else fail(bodyAt + ": tipo desconocido \"" + type + "\"");
                body.mass = static_cast<float>(readNumber(json, "mass", bodyAt, 0, 1.0e6));
                body.friction = static_cast<float>(readNumber(json, "friction", bodyAt, 0, 10));
                body.restitution = static_cast<float>(readNumber(json, "restitution", bodyAt, 0, 1));
                entity.body = body;
            }
            if (number >= 6 && item.contains("audio")) {
                const auto& json = item["audio"];
                const std::string audioAt = at + " (audio)";
                if (!json.is_object()) fail(audioAt + ": debe ser un objeto");
                AudioSourceComponent audio;
                audio.clip = readString(json, "clip", audioAt);
                if (!audio.clip.empty() && !isValidSoundPath(audio.clip)) fail(audioAt + ": ruta de sonido invalida \"" + audio.clip + "\"");
                audio.volume = static_cast<float>(readNumber(json, "volume", audioAt, 0, 4));
                audio.minDistance = static_cast<float>(readNumber(json, "minDistance", audioAt, 0.01, 10000));
                audio.maxDistance = static_cast<float>(readNumber(json, "maxDistance", audioAt, 0.01, 10000));
                audio.loop = readBool(json, "loop", audioAt);
                audio.playOnStart = readBool(json, "playOnStart", audioAt);
                audio.spatial = readBool(json, "spatial", audioAt);
                entity.audio = std::move(audio);
            }
            if (number >= 6 && item.contains("player")) {
                const auto& json = item["player"];
                const std::string playerAt = at + " (jugador)";
                if (!json.is_object()) fail(playerAt + ": debe ser un objeto");
                PlayerComponent player;
                player.height = static_cast<float>(readNumber(json, "height", playerAt, 0.5, 10));
                player.radius = static_cast<float>(readNumber(json, "radius", playerAt, 0.05, 2));
                if (player.radius * 2 >= player.height) fail(playerAt + ": el radio debe ser menor que la mitad de la altura");
                player.walkSpeed = static_cast<float>(readNumber(json, "walkSpeed", playerAt, 0, 100));
                player.runSpeed = static_cast<float>(readNumber(json, "runSpeed", playerAt, 0, 100));
                player.jumpSpeed = static_cast<float>(readNumber(json, "jumpSpeed", playerAt, 0, 100));
                player.mouseSensitivity = static_cast<float>(readNumber(json, "mouseSensitivity", playerAt, 0.001, 10));
                entity.player = player;
            }
            if (number >= 5 && item.contains("script")) {
                const auto& scriptJson = item["script"];
                const std::string scriptAt = at + " (script)";
                if (!scriptJson.is_object()) fail(scriptAt + ": debe ser un objeto");
                ScriptComponent script;
                script.path = readString(scriptJson, "path", scriptAt);
                if (!isValidScriptPath(script.path)) fail(scriptAt + ": ruta de script invalida \"" + script.path + "\"");
                const auto& enabled = member(scriptJson, "enabled", scriptAt);
                if (!enabled.is_boolean()) fail(scriptAt + ": \"enabled\" debe ser true o false");
                script.enabled = enabled.get<bool>();
                const auto& params = member(scriptJson, "params", scriptAt);
                if (!params.is_object()) fail(scriptAt + ": \"params\" debe ser un objeto");
                for (const auto& [name, value] : params.items()) {
                    if (value.is_boolean()) script.parameters[name] = value.get<bool>();
                    else if (value.is_number() && std::isfinite(value.get<double>())) script.parameters[name] = value.get<double>();
                    else if (value.is_string()) script.parameters[name] = value.get<std::string>();
                    else fail(scriptAt + ": el parametro \"" + name + "\" debe ser numero, booleano o texto");
                }
                entity.script = std::move(script);
            }
            if (number >= 4 && item.contains("heightmap")) {
                const auto& heightmapItem = item["heightmap"];
                const std::string heightmapAt = at + " (terreno de malla)";
                if (!heightmapItem.is_object()) fail(heightmapAt + ": debe ser un objeto");
                const auto settings = readHeightmapSettings(heightmapItem, heightmapAt);
                entity.heightmap = generateTerrains ? makeHeightmap(settings) : HeightmapComponent{settings, nullptr};
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

// --- Terreno de malla --------------------------------------------------------------------------

std::string serializeHeightmap(const Heightmap& map) {
    // Alturas en 1/64 m como enteros de 16 bits con signo (±512 m, error < 1 cm).
    std::vector<std::uint8_t> heights;
    heights.reserve(map.heights().size() * 2);
    for (const float height : map.heights()) {
        const auto value = static_cast<std::int16_t>(std::clamp(std::lround(height * heightmapUnits), -32767L, 32767L));
        const auto bits = static_cast<std::uint16_t>(value);
        heights.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
        heights.push_back(static_cast<std::uint8_t>(bits >> 8));
    }
    std::vector<std::uint8_t> weights;
    weights.reserve(map.allWeights().size() * 4);
    for (const auto w : map.allWeights()) {
        for (int i = 0; i < 4; ++i) weights.push_back(static_cast<std::uint8_t>((w >> (i * 8)) & 0xFFu));
    }
    Json root = Json::object();
    root["format"] = "ge3d.heightmap";
    root["version"] = 1;
    root["chunks"] = Json::array({map.chunksX(), map.chunksZ()});
    root["heights"] = encodeBase64(heights);
    root["weights"] = encodeBase64(weights);
    if (map.hasWater()) {
        // Profundidad del agua con la misma codificación que las alturas (solo si hay agua).
        std::vector<std::uint8_t> water;
        water.reserve(map.allWater().size() * 2);
        for (const float depth : map.allWater()) {
            const auto bits = static_cast<std::uint16_t>(std::clamp(std::lround(depth * heightmapUnits), 0L, 65535L));
            water.push_back(static_cast<std::uint8_t>(bits & 0xFFu));
            water.push_back(static_cast<std::uint8_t>(bits >> 8));
        }
        root["water"] = encodeBase64(water);
    }
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

std::shared_ptr<Heightmap> parseHeightmap(std::string_view text) {
    const auto root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) fail("Los datos del terreno no son JSON valido");
    const std::string where = "Terreno de malla";
    if (readString(root, "format", where) != "ge3d.heightmap") fail("El archivo no contiene un terreno de malla");
    if (readInteger(root, "version", where, 1, 1) != 1) fail(where + ": version no soportada");
    const auto& chunks = member(root, "chunks", where);
    if (!chunks.is_array() || chunks.size() != 2) fail(where + ": \"chunks\" debe ser una lista de 2 enteros");
    for (const auto& value : chunks) {
        if (!value.is_number_integer() || value.get<long long>() < 1 || value.get<long long>() > Heightmap::maxChunks) {
            fail(where + ": \"chunks\" fuera de rango");
        }
    }
    auto map = std::make_shared<Heightmap>(static_cast<int>(chunks[0].get<long long>()), static_cast<int>(chunks[1].get<long long>()));
    const auto count = static_cast<std::size_t>(map->verticesX()) * static_cast<std::size_t>(map->verticesZ());
    const auto heights = decodeBase64(readString(root, "heights", where), where + " (heights)");
    const auto weights = decodeBase64(readString(root, "weights", where), where + " (weights)");
    if (heights.size() != count * 2) fail(where + ": \"heights\" no tiene el tamano del terreno");
    if (weights.size() != count * 4) fail(where + ": \"weights\" no tiene el tamano del terreno");
    std::vector<std::uint8_t> water;
    if (root.contains("water")) {
        water = decodeBase64(readString(root, "water", where), where + " (water)");
        if (water.size() != count * 2) fail(where + ": \"water\" no tiene el tamano del terreno");
    }
    for (int z = 0; z < map->verticesZ(); ++z) {
        for (int x = 0; x < map->verticesX(); ++x) {
            const auto index = static_cast<std::size_t>(z) * static_cast<std::size_t>(map->verticesX()) + static_cast<std::size_t>(x);
            const auto bits = static_cast<std::uint16_t>(heights[index * 2] | (heights[index * 2 + 1] << 8));
            map->setHeight(x, z, static_cast<float>(static_cast<std::int16_t>(bits)) / heightmapUnits);
            Heightmap::Weights w = 0;
            for (std::size_t i = 0; i < 4; ++i) w |= static_cast<Heightmap::Weights>(weights[index * 4 + i]) << (i * 8);
            map->setWeights(x, z, w);
            if (!water.empty()) {
                const auto depth = static_cast<std::uint16_t>(water[index * 2] | (water[index * 2 + 1] << 8));
                map->setWater(x, z, static_cast<float>(depth) / heightmapUnits);
            }
        }
    }
    return map;
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
        if (entity.heightmap && entity.heightmap->data) {
            writeTextFileAtomic(folder / pathFromUtf8(heightmapDataPath(entity)), serializeHeightmap(*entity.heightmap->data));
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
        if (!entity.heightmap) continue;
        const auto dataPath = folder / pathFromUtf8(heightmapDataPath(entity));
        std::error_code error;
        auto& component = *scene.find(entity.id)->heightmap;
        if (!std::filesystem::exists(dataPath, error)) {
            if (warnings) warnings->push_back("Terreno \"" + entity.name + "\" sin datos guardados: se genero con su configuracion");
            component.data = generateHeightmap(component.settings);
            continue;
        }
        std::shared_ptr<Heightmap> map;
        try {
            map = parseHeightmap(readTextFile(dataPath));
        } catch (const SceneFormatError& failure) {
            throw SceneFormatError(pathToUtf8(dataPath.filename()) + ": " + failure.what());
        }
        if (map->chunksX() != component.settings.chunksX || map->chunksZ() != component.settings.chunksZ) {
            throw SceneFormatError(pathToUtf8(dataPath.filename()) + ": el tamano no coincide con el de la escena");
        }
        component.data = std::move(map);
    }
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
