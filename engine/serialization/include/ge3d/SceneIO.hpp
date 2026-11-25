#pragma once

#include "ge3d/Scene.hpp"
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ge3d {

// Escenas en JSON UTF-8 versionado. Historial del formato:
//   1  Entidades con UUID, nombre, primitiva y transformación.
//   2  "faces": 6 índices de la paleta de colores, uno por cara.
//   3  Componentes: "transform", "shape" { primitive, materials[6] } y "terrain" { chunks, voxelSize,
//      generator, data }. Las caras usan ids de material del catálogo en lugar de colores.
//   4  "heightmap" { chunks [x, z], cellSize, layers[4], generator, data }: terreno de malla.
// Al leer una versión anterior se migra en memoria; al guardar siempre se escribe la actual.
inline constexpr int sceneFormatVersion = 4;

// El contenido no es una escena válida. La escena abierta no debe sustituirse.
class SceneFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string serializeScene(const Scene& scene);
// Carga en una escena nueva y valida todo antes de devolverla. Lanza SceneFormatError.
// Los terrenos se generan a partir de su configuración; loadSceneFile además lee sus vóxeles.
[[nodiscard]] Scene parseScene(std::string_view json);

// Guarda la escena y, junto a ella, los datos de cada terreno en terrains/<uuid>.terrain.json
// (vóxeles) o terrains/<uuid>.heightmap.json (malla).
// Cada archivo se escribe a "<ruta>.tmp" y se sustituye solo si la escritura terminó bien.
void saveSceneFile(const Scene& scene, const std::filesystem::path& path);
// warnings recibe los avisos no fatales (por ejemplo, un terreno sin datos que se regeneró).
[[nodiscard]] Scene loadSceneFile(const std::filesystem::path& path, std::vector<std::string>* warnings = nullptr);

// Vóxeles de un terreno, en texto: { "format": "ge3d.terrain", "version": 1, "chunks": [x, y, z],
// "materials": [ids], "columns": "..." }. Cada columna (x, z) es una lista de tramos "cantidad:material"
// desde y = 0 hacia arriba, separados por comas; las columnas se separan con ';' y el aire final se omite.
[[nodiscard]] std::string serializeVoxels(const VoxelGrid& grid);
[[nodiscard]] std::shared_ptr<VoxelGrid> parseVoxels(std::string_view json);
[[nodiscard]] std::string terrainDataPath(const Entity& entity);

// Terreno de malla: { "format": "ge3d.heightmap", "version": 1, "chunks": [x, z], "heights": base64,
// "weights": base64 }. heights: int16 little endian en 1/64 m por vértice (filas de z); weights: 4 bytes
// por vértice (peso de cada capa). Se guarda en terrains/<uuid>.heightmap.json junto a la escena.
[[nodiscard]] std::string serializeHeightmap(const Heightmap& map);
[[nodiscard]] std::shared_ptr<Heightmap> parseHeightmap(std::string_view json);
[[nodiscard]] std::string heightmapDataPath(const Entity& entity);

// Material equivalente a un color de la paleta antigua (migración de escenas v1 y v2).
[[nodiscard]] MaterialId materialForPaletteColor(int index);

// Conversión de rutas a/desde texto UTF-8 (la interfaz y el JSON usan UTF-8).
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);

// Escritura atómica de un archivo de texto (temporal + reemplazo).
void writeTextFileAtomic(const std::filesystem::path& path, const std::string& text);
[[nodiscard]] std::string readTextFile(const std::filesystem::path& path);

}
