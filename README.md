# Game-Engine3D

Motor de escritorio para construir, programar y jugar escenas 3D pixel art desde un editor propio. Plataforma inicial Windows x64. C++20 para el motor, Vulkan 1.3 para gráficos, Luau para gameplay y Slang para materiales.

## Estado del repositorio

La base C++ contiene escenas en memoria, una escena de arranque y generación de malla para caja y rampa con identificadores de cara. Los ejecutables de editor y jugador son de consola. El sondeo Vulkan es opcional y consulta la versión del cargador. El viewport gráfico, el guardado de escenas, Luau, Slang, Jolt y miniaudio tienen diseños de integración y quedan pendientes de implementación.

## Compilación

Instala Visual Studio 2022 con Desarrollo para el escritorio con C++ y CMake 3.24 o superior. Ejecuta desde la raíz en una terminal donde CMake esté disponible.

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev
ctest --preset windows-dev
.\out\build\windows-dev\apps\editor\Debug\ge3d_editor.exe
.\out\build\windows-dev\apps\player\Debug\ge3d_player.exe
```

Para el diagnóstico gráfico instala también Vulkan SDK 1.3 o superior y abre una terminal nueva.

```powershell
cmake --preset windows-vulkan
cmake --build --preset windows-vulkan
.\out\build\windows-vulkan\tools\Debug\ge3d_vulkan_probe.exe
```

El diagnóstico indica la versión del cargador. El futuro backend comprobará aparte la GPU, las características y la presentación de superficies.

## Documentación

- [Arquitectura y datos](docs/architecture.md)
- [Principios, mejoras y qué NO hacer](docs/guidelines.md)
- [Render Vulkan, materiales y estilo](engine/render/README.md)
- [Editor visual y construcción](apps/editor/README.md)
- [Luau y editor de código](engine/scripting/README.md)
- [Simulación y audio](docs/simulation.md)
- [Importación, caché y exportación](engine/assets/README.md)
- [Rendimiento, pruebas y entregas](docs/roadmap.md)
- [Proyecto de referencia](projects/pueblo/README.md)
