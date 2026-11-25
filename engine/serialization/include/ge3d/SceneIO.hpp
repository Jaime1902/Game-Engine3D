#pragma once

#include "ge3d/Scene.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ge3d {

// Escenas en JSON UTF-8 versionado:
// { "format": "ge3d.scene", "version": 2, "entities": [ { "uuid", "name", "primitive",
//   "position", "rotationDegrees", "scale", "faces" } ] }
// Historial del formato:
//   1  Entidades con UUID, nombre, primitiva y transformación.
//   2  "faces": 6 índices de la paleta, uno por cara. Al leer una v1 se usan los colores por defecto.
inline constexpr int sceneFormatVersion = 2;

// El contenido no es una escena válida. La escena abierta no debe sustituirse.
class SceneFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string serializeScene(const Scene& scene);
// Carga en una escena nueva y valida todo antes de devolverla. Lanza SceneFormatError.
[[nodiscard]] Scene parseScene(std::string_view json);

// Escribe a "<ruta>.tmp" y lo sustituye por la ruta final solo si la escritura terminó bien.
void saveSceneFile(const Scene& scene, const std::filesystem::path& path);
[[nodiscard]] Scene loadSceneFile(const std::filesystem::path& path);

// Conversión de rutas a/desde texto UTF-8 (la interfaz y el JSON usan UTF-8).
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);

}
