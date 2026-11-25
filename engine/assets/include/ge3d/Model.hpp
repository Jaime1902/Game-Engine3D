#pragma once

#include "ge3d/Materials.hpp"
#include "ge3d/Scene.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ge3d {

// Modelo 3D rígido importado de glTF 2.0 (.gltf o .glb, lo que exporta Blender). Toda la jerarquía de
// nodos del archivo se combina en una sola malla en el espacio del modelo. No hay esqueleto ni
// animación (guidelines: solo mallas rígidas).
//
// Convención: glTF mira hacia +Z y este motor hacia -Z. Al importar se gira 180 grados en Y para que la
// parte delantera del modelo quede hacia delante. 1 unidad = 1 metro.
struct ModelVertex {
    float position[3];
    float normal[3];
    float uv[2];
    std::uint32_t color;    // RGBA8 (color de vértice; blanco si no tiene)
};
static_assert(sizeof(ModelVertex) == 36, "ModelVertex debe ocupar 36 bytes");

struct ModelMaterial {
    std::string name;
    std::array<float, 4> baseColor{1, 1, 1, 1};
    int texture{-1};        // índice en ModelData::textures
};

// Trozo de malla con un material.
struct ModelPart {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    int material{-1};
};

struct ModelData {
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ModelPart> parts;
    std::vector<ModelMaterial> materials;
    std::vector<Image> textures;
    Vec3 boundsMin{};
    Vec3 boundsMax{};
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

// Lanza std::runtime_error con un mensaje claro si el archivo no se puede importar.
[[nodiscard]] ModelData loadModel(const std::filesystem::path& path);
// Ruta de modelo válida dentro de Content: relativa, sin "..", .gltf o .glb.
[[nodiscard]] bool isValidModelPath(const std::string& path);

// Modelos cargados por ruta (relativa a Content), compartidos entre las entidades que los usan.
// Se vuelven a importar si el archivo cambia en disco.
class ModelLibrary {
public:
    explicit ModelLibrary(std::filesystem::path contentRoot = {});
    void setRoot(std::filesystem::path contentRoot);
    // nullptr si no existe o no se puede importar (el error queda en lastError).
    [[nodiscard]] std::shared_ptr<const ModelData> get(const std::string& path);
    // Como get, pero sin mirar el disco si ya se intentó cargar (para cada fotograma). refresh() mira
    // la fecha de todos los conocidos y reimporta los cambiados.
    [[nodiscard]] std::shared_ptr<const ModelData> cached(const std::string& path);
    void refresh();
    [[nodiscard]] const std::string& lastError(const std::string& path) const;
    // Revisión que cambia cada vez que se (re)importa algún modelo.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    struct Entry {
        std::filesystem::file_time_type time;
        std::shared_ptr<const ModelData> data;
        std::string error;
    };
    std::filesystem::path root_;
    std::map<std::string, Entry> entries_;
    std::uint64_t revision_{1};
};

}
