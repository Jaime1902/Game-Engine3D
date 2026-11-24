# Game-Engine3D

Motor de escritorio para construir, programar y jugar escenas 3D pixel art desde un editor propio. Plataforma inicial Windows x64. C++20 para el motor, Vulkan 1.3 para gráficos, Luau para gameplay y Slang para materiales.

## Estado del repositorio

La base C++ contiene escenas en memoria y genera mallas de caja y rampa. El preset Vulkan habilita un editor gráfico con jerarquía, inspector y viewport pixel art. El jugador de consola permite comprobar el arranque del runtime. El guardado de escenas, los scripts Luau, la física y el audio corresponden a etapas posteriores.

## Compilación

Instala Visual Studio 2022 con Desarrollo para el escritorio con C++ y CMake 3.24 o superior. Ejecuta desde la raíz en una terminal donde CMake esté disponible.

```powershell
cmake --preset windows-dev
cmake --build --preset windows-dev
ctest --preset windows-dev
.\out\build\windows-dev\apps\editor\Debug\ge3d_editor.exe
.\out\build\windows-dev\apps\player\Debug\ge3d_player.exe
```

Para compilar el editor gráfico instala un Vulkan SDK con `slangc` y abre una terminal nueva. La primera configuración descarga las dependencias gráficas.

```powershell
cmake --preset windows-vulkan
cmake --build --preset windows-vulkan
.\out\build\windows-vulkan\tools\Debug\ge3d_vulkan_probe.exe
.\out\build\windows-vulkan\apps\vulkan-editor\Debug\ge3d_vulkan_editor.exe
```

El diagnóstico indica la versión del cargador. El editor requiere una GPU con Vulkan 1.3, dynamic rendering y synchronization2.

Haz clic en un objeto de la escena o en la jerarquía para seleccionarlo. Usa **W**, **E** y **R** para mover, rotar y escalar, y **F** para encuadrar la selección. La rueda acerca la cámara, el botón derecho orbita y el botón central desplaza la vista. El inspector edita la transformación directamente. **Ajuste** activa incrementos en el gizmo.

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
