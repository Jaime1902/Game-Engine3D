#include "ge3d/Materials.hpp"

#include "ge3d/SceneIO.hpp"
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ge3d {

namespace {
using Json = nlohmann::ordered_json;

[[noreturn]] void fail(const std::string& message) {
    throw SceneFormatError(message);
}

std::uint32_t parseTint(const std::string& text, const std::string& where) {
    if (text.size() != 7 || text[0] != '#') fail(where + ": \"tint\" debe tener la forma #rrggbb");
    std::uint32_t value = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
        else fail(where + ": \"tint\" debe tener la forma #rrggbb");
        value = (value << 4) | digit;
    }
    return value;
}

std::string tintText(std::uint32_t tint) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "#%06x", static_cast<unsigned>(tint & 0xFFFFFF));
    return buffer;
}

bool validTexturePath(const std::string& path) {
    // Relativa al catálogo y sin subir de carpeta: el proyecto debe poder moverse entero.
    return !path.empty() && path.find("..") == std::string::npos && path.front() != '/' && path.front() != '\\' &&
           path.find(':') == std::string::npos;
}
}

std::optional<std::size_t> MaterialLibrary::indexOf(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < materials_.size(); ++i) {
        if (materials_[i].id == id) return i;
    }
    return std::nullopt;
}

const MaterialDef* MaterialLibrary::find(std::string_view id) const noexcept {
    const auto index = indexOf(id);
    return index ? &materials_[*index] : nullptr;
}

void MaterialLibrary::add(MaterialDef material) {
    if (!isValidMaterialId(material.id)) throw std::invalid_argument("MaterialLibrary: id invalido " + material.id);
    if (indexOf(material.id)) throw std::invalid_argument("MaterialLibrary: id repetido " + material.id);
    materials_.push_back(std::move(material));
}

bool isValidMaterialId(std::string_view id) noexcept {
    if (id.empty() || id.size() > 48) return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

MaterialLibrary parseMaterialLibrary(std::string_view text) {
    const auto root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) fail("El catalogo de materiales no es JSON valido");
    if (root.value("format", std::string{}) != "ge3d.materials") fail("El archivo no es un catalogo de materiales");
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<int>() != 1) {
        fail("Catalogo de materiales: version no soportada");
    }
    if (!root.contains("materials") || !root["materials"].is_array()) fail("Catalogo de materiales: falta \"materials\"");

    MaterialLibrary library;
    const auto& list = root["materials"];
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& item = list[i];
        const std::string where = "Material " + std::to_string(i + 1);
        if (!item.is_object()) fail(where + ": debe ser un objeto");
        const auto text = [&](const char* key, bool required) -> std::string {
            if (!item.contains(key)) {
                if (required) fail(where + ": falta \"" + key + "\"");
                return {};
            }
            if (!item[key].is_string()) fail(where + ": \"" + key + "\" debe ser texto");
            return item[key].get<std::string>();
        };
        MaterialDef material;
        material.id = text("id", true);
        if (!isValidMaterialId(material.id)) fail(where + ": id \"" + material.id + "\" invalido (usa a-z, 0-9, _ y -)");
        if (library.indexOf(material.id)) fail(where + ": id repetido \"" + material.id + "\"");
        material.name = text("name", false);
        if (material.name.empty()) material.name = material.id;
        material.side = text("texture", true);
        material.top = text("top", false);
        material.bottom = text("bottom", false);
        for (const auto* path : {&material.side, &material.top, &material.bottom}) {
            if (!path->empty() && !validTexturePath(*path)) fail(where + ": ruta de textura invalida \"" + *path + "\"");
        }
        const auto tint = text("tint", false);
        if (!tint.empty()) material.tint = parseTint(tint, where);
        library.add(std::move(material));
    }
    return library;
}

std::string serializeMaterialLibrary(const MaterialLibrary& library) {
    Json list = Json::array();
    for (const auto& material : library.materials()) {
        Json item = Json::object();
        item["id"] = material.id;
        item["name"] = material.name;
        item["texture"] = material.side;
        if (!material.top.empty()) item["top"] = material.top;
        if (!material.bottom.empty()) item["bottom"] = material.bottom;
        if ((material.tint & 0xFFFFFF) != 0xFFFFFF) item["tint"] = tintText(material.tint);
        list.push_back(std::move(item));
    }
    Json root = Json::object();
    root["format"] = "ge3d.materials";
    root["version"] = 1;
    root["materials"] = std::move(list);
    return root.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
}

MaterialLibrary loadMaterialLibrary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("No se puede abrir " + pathToUtf8(path));
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseMaterialLibrary(buffer.str());
}

}
