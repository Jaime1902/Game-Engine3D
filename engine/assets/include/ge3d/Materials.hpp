#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ge3d {

// Un material de superficie: texturas PNG por orientación y un tinte opcional.
// Las caras y los vóxeles guardan el id del material, no la textura.
struct MaterialDef {
    std::string id;           // identificador estable en minúsculas: "piedra", "cesped"...
    std::string name;         // nombre visible
    std::string side;         // textura de los lados (y por defecto de todo), relativa al catálogo
    std::string top;          // vacía = la de los lados
    std::string bottom;       // vacía = la de los lados
    std::uint32_t tint{0xFFFFFF};  // 0xRRGGBB que multiplica la textura
    friend bool operator==(const MaterialDef&, const MaterialDef&) = default;

    [[nodiscard]] const std::string& topTexture() const { return top.empty() ? side : top; }
    [[nodiscard]] const std::string& bottomTexture() const { return bottom.empty() ? side : bottom; }
};

// Catálogo de materiales del proyecto (Content/materials.json):
// { "format": "ge3d.materials", "version": 1, "materials": [ { "id", "name", "texture",
//   "top"?, "bottom"?, "tint"? } ] }
class MaterialLibrary {
public:
    [[nodiscard]] const std::vector<MaterialDef>& materials() const noexcept { return materials_; }
    [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view id) const noexcept;
    [[nodiscard]] const MaterialDef* find(std::string_view id) const noexcept;
    void add(MaterialDef material);
    [[nodiscard]] bool empty() const noexcept { return materials_.empty(); }

private:
    std::vector<MaterialDef> materials_;
};

[[nodiscard]] bool isValidMaterialId(std::string_view id) noexcept;
// Lanza SceneFormatError (ge3d/SceneIO.hpp) si el catálogo no es válido.
[[nodiscard]] MaterialLibrary parseMaterialLibrary(std::string_view json);
[[nodiscard]] std::string serializeMaterialLibrary(const MaterialLibrary& library);
[[nodiscard]] MaterialLibrary loadMaterialLibrary(const std::filesystem::path& path);

// --- Imágenes PNG ---------------------------------------------------------------------------------

struct Image {
    int width{};
    int height{};
    std::vector<std::uint8_t> rgba;   // width * height * 4
};

// Lanza std::runtime_error si no se puede leer.
[[nodiscard]] Image loadPng(const std::filesystem::path& path);
void savePng(const std::filesystem::path& path, const Image& image);

}
