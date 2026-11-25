# Game-Engine3D

Motor de escritorio para construir, programar y jugar escenas 3D pixel art desde un editor propio. Plataforma inicial Windows x64. C++20 para el motor, Vulkan 1.3 para gráficos, Luau para gameplay y Slang para materiales.

## Estado del repositorio

El runtime C++ organiza cada objeto en componentes (Transformación, Forma, Terreno y Terreno de vóxeles), genera y malla terrenos de malla y de vóxeles, carga materiales con texturas PNG, guarda y abre escenas en JSON y lleva un historial de deshacer y rehacer. El editor Vulkan tiene la distribución clásica de un editor de escenas: Jerarquía, Escena, Inspector, Proyecto y Consola, con barra de herramientas y modo Play. El jugador de consola permite comprobar el arranque del runtime. Los objetos se programan con scripts Luau desde la pestaña Código y se prueban en Play, con física (Jolt), sonido 3D (miniaudio) y un jugador en primera persona. El avance por etapas está en [docs/roadmap.md](docs/roadmap.md).

## Compilación

Requisitos: un compilador C++20 con CMake 3.24 o superior (MinGW-w64 con g++ y Ninja en el PATH, por ejemplo WinLibs, o Visual Studio 2022 con Desarrollo para el escritorio con C++) y, para el editor gráfico, el [Vulkan SDK](https://vulkan.lunarg.com) con `slangc`. La primera configuración descarga las dependencias ([lista y licencias](docs/dependencies.md)).

Todo se hace con `build.bat` desde la raíz del proyecto, en CMD o PowerShell, o con doble clic:

```bat
build              :: compila el editor grafico y lo abre
build demo         :: igual, con la escena de prueba de fisica, sonido y jugador
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
| **Jerarquía** (izquierda) | Objetos de la escena. **+** o clic derecho crea objetos; **Ctrl+clic** y **Shift+clic** seleccionan varios; arrastrar reordena; **F2** renombra; clic derecho: copiar, pegar, duplicar, activar/desactivar. Los inactivos se ven atenuados |
| **Escena** (centro) | Vista pixel art (ver *Estilo pixel art*). Cubo de orientación arriba a la derecha: clic en una cara para alinear la cámara. El mundo no tiene bordes: la cuadrícula sigue a la cámara, pasa a líneas cada 10 m o 100 m al alejarse y se desvanece con tramado |
| **Inspector** (derecha) | Casilla de activo, nombre y componentes. El botón **...** (o clic derecho) de cada componente: Restablecer, Copiar valores, Pegar valores y Quitar. **Agregar componente** tiene buscador (escribe y pulsa Intro). Soltar aquí un script, sonido o material lo asigna |
| **Proyecto** (abajo) | Carpetas a la izquierda y recursos con iconos a la derecha, más la carpeta virtual **Materiales**. Doble clic abre (escena, script, carpeta) o escucha (sonido). **Arrastra** materiales, scripts y sonidos sobre un objeto de la Escena o la Jerarquía (Shift: material solo en esa cara). Clic derecho: crear carpeta, script o escena, renombrar (actualiza las referencias de la escena), eliminar (a la papelera) y mostrar en el Explorador. Buscador en todo el proyecto |
| **Consola** (abajo) | Mensajes del editor, de Vulkan y de los scripts, con filtros por tipo. Clic en un error de script abre la línea en Código. **Limpiar al jugar** y **Pausa en error** como en Unity |
| **Código** (junto a Escena) | Scripts Luau de `Content/scripts`: pestañas, números de línea, errores en vivo y **Guardar** (Ctrl+S) |

La barra superior tiene las herramientas **Mano (Q)**, **Mover (W)**, **Rotar (E)**, **Escalar (R)**, **Pintar (B)**, **Construir (C)** y **Terreno (T)**, el cambio de ejes Global o Local y **Ajuste**, que mueve en pasos de 0,25 y rota en pasos de 15°. En el centro están **Play**, **Pausa** y **Paso**: en Play el editor se tiñe de azul y al detener se descartan los cambios hechos durante el juego.

Con Pintar, Construir y Terreno, la barra de la vista muestra los materiales con su miniatura; el elegido es el que se pinta, se construye o se esculpe. Cada trazo se deshace de una vez.

| Acción | Atajo |
|---|---|
| Mirar y volar (como en Unity) | Mantener **clic derecho** + **WASD** (Q baja, E sube, Shift x3, rueda: velocidad) |
| Orbitar / desplazar / acercar | **Alt + clic izquierdo** / botón central / rueda |
| Seleccionar varios | **Ctrl+clic** o **Shift+clic**; arrastrar en un hueco de la vista dibuja un rectángulo de selección |
| Copiar / pegar / seleccionar todo / renombrar | **Ctrl+C** / **Ctrl+V** / **Ctrl+A** / **F2** |
| Ajuste a la cuadrícula al arrastrar el gizmo | Mantener **Ctrl** |
| Pintar (herramienta B) | Clic o arrastre: la cara bajo el cursor · Shift + clic: toda la pieza · Ctrl + clic: tomar el material (también de un vóxel) |
| Construir (herramienta C) | Clic: una pieza en la celda · Arrastre: trazo · Shift + arrastre: rectángulo · Ctrl + arrastre: borrar piezas · Espacio: girar 90° |
| Terreno (herramienta T) | Pinceles Elevar / Bajar / Pintar / Aplanar / Suavizar / Agua (Shift: quitar) · mantener pulsado para seguir aplicando · **[** y **]**: radio |
| Enfocar selección | **F** |
| Duplicar / eliminar | **Ctrl+D** / **Supr** |
| Deshacer / rehacer | **Ctrl+Z** / **Ctrl+Y** |
| Nueva / abrir / guardar escena | **Ctrl+N** / **Ctrl+O** / **Ctrl+S** |
| Play / pausa / paso | **Ctrl+P** / **Ctrl+Shift+P** / **Ctrl+Alt+P** |

La escena se guarda en `projects/pueblo/Content/scenes/` y al iniciar se abre la última que usaste (**Archivo → Abrir reciente** tiene las demás). El editor recuerda la distribución de paneles, la cámara de cada escena, el estilo y las opciones en `projects/pueblo/UserSettings/` (por usuario, fuera del repositorio). Los datos de cada terreno se guardan junto a ella, en `scenes/terrains/<uuid>.heightmap.json` (malla) o `<uuid>.terrain.json` (vóxeles). El título y la Jerarquía muestran `*` cuando hay cambios sin guardar. Al cerrar, abrir otra escena o crear una nueva con cambios pendientes, el editor pregunta si guardar. Si un archivo está dañado, el editor muestra el error y conserva la escena abierta. **Ventana → Restablecer distribución** recupera los paneles cerrados.

### Materiales y texturas

Las caras y los vóxeles no guardan colores sino **materiales**, definidos en `projects/pueblo/Content/materials.json`:

```json
{ "id": "cesped", "name": "Cesped", "texture": "textures/cesped_lado.png",
  "top": "textures/cesped.png", "bottom": "textures/tierra.png", "tint": "#ffffff" }
```

- `texture` es la textura de los lados; `top` y `bottom` son opcionales (si faltan se usa la de los lados). `tint` multiplica el color.
- Todas las texturas son PNG cuadrados del mismo tamaño y potencia de dos (el juego inicial es de 16 × 16). Se dibujan con filtro nearest y se repiten **una vez por metro**, así que un muro estirado mantiene el tamaño de sus píxeles.
- Al guardar `materials.json` o cualquier PNG, el editor recarga las texturas solo (en menos de un segundo). Un id desconocido o un PNG que no carga se ve con cuadros magenta y aparece un aviso en la Consola.
- `ge3d_texture_gen <carpeta Content>` regenera el juego de texturas inicial (no sobrescribe archivos existentes salvo con `--force`).

### Física, sonido y jugador en primera persona

**Para probarlo rápido:** `build demo` abre `scenes/demo_fisica.scene.json` (una habitación con rampa, plataforma, cajas, una puerta programada, una fuente con sonido y música fuera). Pulsa **Play** y haz clic en la vista:

| Tecla | Acción |
|---|---|
| **W A S D** | Andar · **Shift**: correr |
| **Ratón** | Mirar |
| **Espacio** | Saltar (en el agua, nadar hacia arriba) |
| **E** o **clic** | Usar lo que hay delante (por ejemplo, abrir la puerta) |
| **Esc** | Soltar el ratón (clic en la vista para volver) · **Ctrl+P**: detener |

- **Colisiones sin configurar nada:** las piezas (cajas, rampas) y los terrenos son suelo y paredes. Se sube por rampas y escalones de hasta 45 cm.
- **Agregar componente → Cuerpo físico:** *Dinámico* cae, choca, se empuja y flota en el agua del terreno; *Cinemático* lo mueve un script y empuja a los demás; *Zona (trigger)* no choca y avisa a los scripts con `onTriggerEnter` / `onTriggerExit` (como la campana de la demo).
- **Fuente de audio:** un WAV de `Content/sounds` en bucle o una vez, con volumen y distancias. En 3D se oye a volumen completo dentro de la distancia mínima y deja de oírse en la máxima: al acercarte a la fuente de la demo sube el sonido del agua. **>** lo escucha desde el Inspector.
- **Jugador:** **Objeto → Jugador (primera persona)** coloca dónde aparece (la flecha verde indica hacia dónde mira). Sin Jugador, Play usa la cámara del editor y la física funciona igual.
- **Scripts:** `entity.velocity`, `entity:applyImpulse(v)`, `entity:playSound(clip)`, `audio.play(clip)`, `scene.player()` y los callbacks `onTriggerEnter`, `onTriggerExit` y `onCollision`.
- `ge3d_sound_gen <carpeta Content>` regenera los sonidos de ejemplo (sintetizados, WAV de 16 bits a 44,1 kHz).

### Scripts (Luau)

Un script es un archivo `.luau` en `projects/pueblo/Content/scripts` que devuelve una tabla con sus parámetros y callbacks. El de ejemplo es la puerta:

```lua
local Puerta = {}
Puerta.params = { velocidad = 90, angulo = 90, abierta = false }   -- aparecen en el Inspector

function Puerta.onInteract(self)          -- clic sobre la entidad en Play
	self.abierta = not self.abierta
end

function Puerta.onUpdate(self, dt)        -- cada fotograma
	-- gira self.entity.rotation hacia el ángulo objetivo a self.velocidad grados por segundo
end

return Puerta
```

1. En la pestaña **Código**, **+ Nuevo script** crea uno con una plantilla (o abre `puerta.luau`). Los errores aparecen mientras escribes, con la línea marcada en rojo; clic en un error lleva a su línea.
2. Selecciona un objeto, **Agregar componente → Script** y elige el script. El Inspector muestra sus parámetros; cada objeto puede tener valores distintos (la **x** vuelve al valor del script).
3. Pulsa **Play** y haz clic en el objeto. Si guardas el script durante Play se recarga al momento; si tiene errores sigue funcionando la versión anterior. Al detener, todo vuelve como estaba.

API: `self.entity` (position, rotation, scale como `vector`, name, `send`), `scene.find(nombre)`, `scene.send(nombre, evento, valor)`, `scene.time()`, `input.isDown("E")` / `input.wasPressed("Space")`, `log(...)`; callbacks `onStart`, `onUpdate`, `onInteract`, `onMessage`, `onStop`. El botón **API** de la pestaña Código tiene la referencia completa. Los scripts no tienen acceso a archivos ni al sistema; cada llamada tiene 1 ms (20 ms `onStart`) y todos juntos 3,5 ms por fotograma: un bucle sin fin se interrumpe, se informa en la Consola con su traza y ese script se desactiva hasta detener Play.

### Estilo pixel art

La escena no se dibuja a una resolución fija: se dibuja a la resolución de la vista dividida por el **tamaño del píxel** (3 por defecto), así que cada píxel de la escena ocupa exactamente 3 × 3 píxeles de pantalla. Encima se aplica una pasada de estilo:

- **Contornos**: el borde exterior de cada objeto se oscurece con una línea de un píxel y las aristas interiores (esquinas, crestas, bordes de vóxeles) se aclaran.
- **Luz por bandas**: el sol se cuantiza en pocos escalones, con sombras azuladas y luz cálida.
- **Cámara estable**: la cámara avanza de píxel en píxel y la imagen se desplaza una fracción de píxel en pantalla, así que al moverse los bordes no parpadean.
- **Colores por canal** (opcional): reduce la paleta con tramado ordenado.

Todo se ajusta en el botón **Estilo** de la barra de la vista. Con tamaño de píxel 1 la escena se ve a resolución completa, con las texturas pixel art y los contornos.

### Terreno

**Objeto → Terreno** (o **+** en la Jerarquía) crea un terreno de malla de 128 × 128 m: una rejilla de alturas suave que se modela con pinceles. Tiene 4 **capas** de material (por defecto césped, tierra, piedra y arena) que se pintan y se mezclan con tramado a nivel de texel; en las laderas el césped muestra su textura de abajo (tierra). En el Inspector se eligen las capas, la semilla, el relieve, la anchura de las colinas, la roca en pendientes y el tamaño (hasta 1.024 × 1.024 celdas de 0,25 a 2 m). **Generar** crea el relieve y **Plano** deja un terreno llano para modelarlo a mano.

Con la herramienta **Terreno (T)** sobre un terreno de malla: **Elevar** y **Bajar** suben y bajan el relieve con caída suave, **Aplanar** nivela a la altura donde empieza el trazo, **Suavizar** redondea y **Pintar** aplica la capa elegida en la barra de la vista. **Fuerza** regula la velocidad.

**Agua.** Al generar, todo lo que queda por debajo del **nivel del agua** se llena (mar y lagos, con playa de arena en la orilla). El pincel **Agua** echa agua (Shift: la quita) y una simulación de aguas poco profundas la hace correr cuesta abajo, llenar hoyos y nivelarse; al soltar el trazo el agua se asienta y todo se deshace de una vez. En Play el agua sigue fluyendo sola (por ejemplo, si excavas un canal). Se dibuja en pixel art: color según la profundidad, oleaje, brillos del sol, espuma en la orilla y fondo visible con tramado donde hay poca profundidad. **Llenar hasta el nivel** y **Vaciar agua** están en el Inspector.

### Terreno de vóxeles

**Objeto → Terreno de voxeles** crea un terreno de bloques de 128 × 64 × 128 m centrado en el origen. En el Inspector se cambian la semilla, la altura base, el relieve, la anchura de las colinas, el detalle, el nivel del agua, los materiales de cada capa (superficie, subsuelo, roca, orilla, agua) y el tamaño (hasta 512 × 128 × 512 vóxeles, de 0,25 a 2 m). **Generar** crea el relieve con esa configuración; **Plano** deja un terreno llano para esculpirlo a mano. Ambas cosas se pueden deshacer.

Sobre un terreno de vóxeles, la herramienta **Terreno (T)** trabaja por bloques: **Elevar** añade vóxeles del material elegido, **Bajar** los quita, **Pintar** cambia el material de la superficie y **Aplanar** nivela a la altura donde empezó el trazo.

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
