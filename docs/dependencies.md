# Dependencias externas

Todas se descargan con `FetchContent` en la primera configuración y quedan fijadas por tag o commit.

| Biblioteca | Versión | Licencia | Uso | Dónde se declara |
|---|---|---|---|---|
| nlohmann/json | v3.12.0 | MIT | Leer y escribir escenas JSON (solo dentro de `ge3d_runtime`, no se expone en headers) | `engine/CMakeLists.txt` |
| stb_image / stb_image_write | `6b5799b` | Dominio público o MIT | Leer y escribir texturas PNG (solo en `engine/assets/src/Image.cpp`) | `engine/CMakeLists.txt` |
| GLFW | 3.4 | zlib | Ventana y entrada del editor Vulkan | `CMakeLists.txt` |
| Dear ImGui (docking) | `52fe0a0` (1.91.9b) | MIT | Interfaz del editor | `apps/vulkan-editor/CMakeLists.txt` |
| ImGuizmo | `5b473bc` | MIT | Gizmos de mover, rotar y escalar | `apps/vulkan-editor/CMakeLists.txt` |
| GLM | `0af55cc` | MIT | Matemáticas de cámara y transformaciones | `apps/vulkan-editor/CMakeLists.txt` |
| Vulkan Memory Allocator | `c788c52` | MIT | Memoria de buffers e imágenes Vulkan | `apps/vulkan-editor/CMakeLists.txt` |

Requisitos del sistema, no descargados: Vulkan SDK 1.3 o superior con `slangc` (editor Vulkan) y un compilador C++20 (MSVC 2022 o MinGW-w64 GCC).
