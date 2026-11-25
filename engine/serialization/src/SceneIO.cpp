#include "ge3d/SceneIO.hpp"

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

}

std::string serializeScene(const Scene& scene) {
    Json entities = Json::array();
    for (const auto& entity : scene.entities()) {
        Json item = Json::object();
        item["uuid"] = entity.uuid.toString();
        item["name"] = entity.name;
        item["primitive"] = primitiveName(entity.primitive);
        item["position"] = vector(entity.transform.position);
        item["rotationDegrees"] = vector(entity.transform.rotationDegrees);
        item["scale"] = vector(entity.transform.scale);
        Json faces = Json::array();
        for (const auto face : entity.faces) faces.push_back(static_cast<int>(face));
        item["faces"] = std::move(faces);
        entities.push_back(std::move(item));
    }
    Json root = Json::object();
    root["format"] = formatName;
    root["version"] = sceneFormatVersion;
    root["entities"] = std::move(entities);
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

Scene parseScene(std::string_view text) {
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
    // Migraciones: una escena v1 no tiene "faces" y recibe los colores por defecto de su primitiva.
    const bool hasFaces = number >= 2;

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

        const auto primitiveText = readString(item, "primitive", at);
        const auto primitive = primitiveFromName(primitiveText);
        if (!primitive) fail(at + ": primitiva desconocida \"" + primitiveText + "\"");
        entity.primitive = *primitive;

        entity.transform.position = readVector(item, "position", at);
        entity.transform.rotationDegrees = readVector(item, "rotationDegrees", at);
        entity.transform.scale = readVector(item, "scale", at);
        const auto& scale = entity.transform.scale;
        if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0) fail(at + ": la escala debe ser mayor que cero");

        entity.faces = defaultFaceColors(entity.primitive);
        if (hasFaces) {
            const auto& faces = member(item, "faces", at);
            if (!faces.is_array() || faces.size() != maxFaces) {
                fail(at + ": \"faces\" debe ser una lista de " + std::to_string(maxFaces) + " colores");
            }
            for (std::size_t face = 0; face < maxFaces; ++face) {
                const auto& value = faces[face];
                if (!value.is_number_integer() || value.get<long long>() < 0 ||
                    value.get<long long>() >= static_cast<long long>(paletteSize)) {
                    fail(at + ": \"faces\" contiene un color fuera de la paleta (0 a " +
                         std::to_string(paletteSize - 1) + ")");
                }
                entity.faces[face] = static_cast<PaletteIndex>(value.get<long long>());
            }
        }

        scene.add(std::move(entity));
    }
    return scene;
}

void saveSceneFile(const Scene& scene, const std::filesystem::path& path) {
    const auto text = serializeScene(scene);
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

Scene loadSceneFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("No se puede abrir " + pathToUtf8(path));
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) throw std::runtime_error("Fallo al leer " + pathToUtf8(path));
    try {
        return parseScene(buffer.str());
    } catch (const SceneFormatError& error) {
        throw SceneFormatError(pathToUtf8(path.filename()) + ": " + error.what());
    }
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

}
