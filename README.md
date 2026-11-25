# Game-Engine3D

Motor de escritorio para construir, programar y jugar escenas 3D pixel art desde un editor propio. Plataforma inicial Windows x64. C++20 para el motor, Vulkan 1.3 para gráficos, Luau para gameplay y Slang para materiales.

## Estado del repositorio

El runtime C++ genera mallas de caja y rampa, identifica cada entidad con un UUID, guarda y abre escenas en JSON y lleva un historial de deshacer y rehacer. El editor Vulkan tiene la distribución clásica de un editor de escenas: Jerarquía, Escena, Inspector, Proyecto y Consola, con barra de herramientas y modo Play. El jugador de consola permite comprobar el arranque del runtime. Los scripts Luau, la física y el audio corresponden a etapas posteriores. El avance por etapas está en [docs/roadmap.md](docs/roadmap.md).

## Compilación

Requisitos: un compilador C++20 con CMake 3.24 o superior (MinGW-w64 con g++ y Ninja en el PATH, por ejemplo WinLibs, o Visual Studio 2022 con Desarrollo para el escritorio con C++) y, para el editor gráfico, el [Vulkan SDK](https://vulkan.lunarg.com) con `slangc`. La primera configuración descarga las dependencias ([lista y licencias](docs/dependencies.md)).

Todo se hace con `build.bat` desde la raíz del proyecto, en CMD o PowerShell, o con doble clic:

```bat
build              :: compila el editor grafico y lo abre
build all          :: compila la base, ejecuta las pruebas y el editor de consola
build test         :: solo las pruebas
build run-player   :: player de consola
build vulkan       :: compila el editor grafico y ejecuta el diagnostico de Vulkan
build clean        :: borra out\build
build help         :: lista de acciones
build all msvc     :: igual, pero con Visual Studio 2022 en vez de MinGW
```

Con MinGW los ejecutables quedan en `out\build\windows-mingw` y `out\build\windows-mingw-vulkan`. Con Visual Studio, en `out\build\windows-dev` y `out\build\windows-vulkan`, dentro de una subcarpeta `Debug`. También se pueden usar los presets directamente: `cmake --preset windows-mingw`, `cmake --build --preset windows-mingw` y `ctest --preset windows-mingw`.

El editor requiere una GPU con Vulkan 1.3, dynamic rendering y synchronization2. En Debug arranca con las capas de validación de Vulkan: los avisos aparecen en la Consola del editor y al cerrar se imprime el total. `GE3D_VALIDATION=0` las desactiva.

## Uso del editor

| Panel | Para qué sirve |
|---|---|
| **Jerarquía** (izquierda) | Objetos de la escena. **+** crea objetos, el buscador filtra, clic derecho para duplicar o eliminar, doble clic para enfocar |
| **Escena** (centro) | Vista 320 × 180. Cubo de orientación arriba a la derecha: clic en una cara para alinear la cámara. El mundo no tiene bordes: la cuadrícula sigue a la cámara, pasa a líneas cada 10 m o 100 m al alejarse y se desvanece con tramado |
| **Inspector** (derecha) | Nombre, Transformación (posición, rotación, escala), Forma y Colores por cara del objeto seleccionado |
| **Proyecto** (abajo) | Archivos de `projects/pueblo/Content`. Doble clic en una escena para abrirla |
| **Consola** (abajo) | Mensajes del editor y de Vulkan, con filtros por tipo |

La barra superior tiene las herramientas **Mano (Q)**, **Mover (W)**, **Rotar (E)**, **Escalar (R)**, **Pintar (B)** y **Construir (C)**, el cambio de ejes Global o Local y **Ajuste**, que mueve en pasos de 0,25 y rota en pasos de 15°. Con Pintar, la paleta de 24 colores aparece en la barra de la vista y la cara bajo el cursor se resalta con el color elegido. Todo un trazo se deshace de una vez. Con Construir se colocan bloques o rampas en una cuadrícula de 0,5, 1 o 2 m: sobre el suelo o pegados a la cara de otra pieza, con el color elegido de la paleta. Las celdas ocupadas se marcan en rojo y se saltan. Las caras guardan un índice de la paleta (`engine/scene/include/ge3d/Palette.hpp`), así que cambiar un color de la paleta recolorea toda la escena. En el centro están **Play**, **Pausa** y **Paso**. En Play el editor se tiñe de azul, y al detener se descartan los cambios hechos durante el juego. La simulación llegará con las etapas 3 y 4.

| Acción | Atajo |
|---|---|
| Orbitar / desplazar / acercar | Clic derecho o Alt + clic izquierdo / botón central / rueda |
| Pintar (herramienta B) | Clic o arrastre: la cara bajo el cursor · Shift + clic: toda la pieza · Ctrl + clic: tomar el color |
| Construir (herramienta C) | Clic: una pieza en la celda · Arrastre: trazo · Shift + arrastre: rectángulo · Ctrl + arrastre: borrar · Espacio: girar 90° |
| Enfocar selección | **F** |
| Duplicar / eliminar | **Ctrl+D** / **Supr** |
| Deshacer / rehacer | **Ctrl+Z** / **Ctrl+Y** |
| Nueva / abrir / guardar escena | **Ctrl+N** / **Ctrl+O** / **Ctrl+S** |
| Play / pausa / paso | **Ctrl+P** / **Ctrl+Shift+P** / **Ctrl+Alt+P** |

La escena se guarda en `projects/pueblo/Content/scenes/main.scene.json` y se abre sola al iniciar. El título y la Jerarquía muestran `*` cuando hay cambios sin guardar. Al cerrar, abrir otra escena o crear una nueva con cambios pendientes, el editor pregunta si guardar. Si un archivo está dañado, el editor muestra el error y conserva la escena abierta. **Ventana → Restablecer distribución** recupera los paneles cerrados.

## Documentación

- [Arquitectura y datos](docs/architecture.md)
- [Principios, mejoras y qué NO hacer](docs/guidelines.md)
- [Dependencias y licencias](docs/dependencies.md)
- [Render Vulkan, materiales y estilo](engine/render/README.md)
- [Editor visual y construcción](apps/editor/README.md)
- [Luau y editor de código](engine/scripting/README.md)
- [Simulación y audio](docs/simulation.md)
- [Importación, caché y exportación](engine/assets/README.md)
- [Rendimiento, pruebas y entregas](docs/roadmap.md)
- [Proyecto de referencia](projects/pueblo/README.md)
