# Etapas de Game-Engine3D

La base C++20 incluye un runtime compartido, ejecutables de consola, un editor Vulkan y escenas que se guardan en JSON.

## Estado actual

| Etapa | Estado | Hecho | Pendiente |
|---|---|---|---|
| 0 | ✅ Completa | CMake con presets MSVC y MinGW, runtime, CTest, sondeo Vulkan. | — |
| 1 | ✅ Completa | Ventana GLFW, Vulkan 1.3 con dynamic rendering, offscreen 320 × 180 con sampler nearest, cuadrícula, picking, gizmos, Inspector. Capas de validación activas en Debug: 0 errores tras redimensionar, minimizar, restaurar y cerrar minimizada. Pool de geometría por páginas de 4 MB con VMA y staging por fotograma (lo usa el terreno). | — |
| 2 | ✅ Criterio cubierto | UUID v4 por entidad, escena JSON versionada con migraciones y guardado atómico, historial deshacer/rehacer, menús y atajos. Materiales con textura por cara (herramienta B, trazo = una operación, cuentagotas). Pincel de construcción (herramienta C). Cuadrícula infinita. | Hecho después: jerarquía padre/hijo (transformación local, mundo derivado, activo heredado) y prefabs con overrides por componente, aplicar/revertir y modo prefab. Pendiente: cuaternión en la transformación guardada (hoy grados). |
| 3 | ✅ Criterio cubierto | Modelo de componentes y Play sobre una copia. Luau 0.739 en sandbox (VM nueva por Play, destruida en Stop; sin io/debug/loadstring; bibliotecas de solo lectura; 64 MB). Componente Script con parámetros en el Inspector (escena v5), callbacks onStart / onUpdate / onInteract / onMessage / onStop, mensajes entre entidades, entrada de teclado, presupuesto de 1 ms por llamada y 3,5 ms por fotograma con interrupción y desactivación, recarga al guardar conservando la versión anterior si falla. Pestaña Código con pestañas, números de línea, errores en vivo y salto a línea. Ejemplo: `scripts/puerta.luau`. | Resaltado de sintaxis y autocompletado, onTriggerEnter (con la física de la etapa 4), Player ejecutando scripts (etapa 7). |
| 5 | 🟡 Materiales y estilo | Catálogo `materials.json` con texturas PNG por orientación (lado, arriba, abajo) y tinte; Texture2DArray con mipmaps nearest; UV planas en metros; recarga en caliente. Resolución interna = vista / tamaño del píxel (configurable), pasada de post con contorno de silueta y aristas por profundidad y normales, luz por bandas, reducción de colores con tramado, cámara ajustada a la rejilla de píxeles con desplazamiento de subpíxel. | Paletas por proyecto, niebla, sombra direccional, shaders Slang editables por material. |
| — | ✅ Extra: terreno de malla | Rejilla de alturas en chunks de 64 × 64 celdas (hasta 1.024²), 4 capas de material mezcladas con tramado anclado a la textura, pinceles elevar / bajar / aplanar / suavizar / pintar con caída suave y deshacer por chunk, remallado incremental y descarte por frustum, guardado en base64 (alturas de 1/64 m). Agua: mar y lagos al generar, pincel de agua, simulación de aguas poco profundas (tuberías virtuales, conserva el volumen, chunks que se duermen al quedarse quietos) en los trazos y en Play, render con profundidad, oleaje, espuma y tramado. | Interacción del agua con cuerpos físicos (flotar, empujar) en la etapa 4; ríos con caudal constante; LOD a distancia; importar mapas de altura en PNG. |
| — | ✅ Extra: terreno de vóxeles | Terreno de vóxeles: chunks de 32³, greedy meshing, remallado incremental con presupuesto, vértices de 8 bytes, descarte por frustum, generación procedural determinista (ruido fractal con semilla, capas, orilla y agua), pinceles elevar / excavar / pintar / aplanar con deshacer por chunk, guardado en texto RLE por columnas. | Agua transparente, árboles y decoración procedural, LOD para terrenos mayores de 512 × 512. |
| 4 | ✅ Criterio cubierto | Jolt 5.3 en el hilo principal con paso fijo de 60 Hz y acumulador acotado. Piezas y terrenos (malla y vóxeles, un cuerpo por chunk, rehecho al editarlo) como colisionadores estáticos; Cuerpo físico dinámico, cinemático o zona; flotación en el agua del terreno. Jugador en primera persona (CharacterVirtual: rampas, escalones, salto, nado, empuja cajas). miniaudio: emisores 3D con atenuación lineal, efectos en cola con robo de voz por distancia, sonidos precargados al pulsar Play. Scripts: velocidad, impulsos, sonidos, onTriggerEnter/Exit, onCollision. Escena de prueba `demo_fisica` (`build demo`). | Sombras del jugador, articulaciones, música por zonas, mezclador con buses. |
| — | ✅ Extra: modelos, tercera persona e interfaz | Modelos glTF / GLB con cgltf: mallas con sus nodos, colores de vértice, color base y texturas (Texture2DArray de 128 × 128), componente Modelo (escena v8) con colisionador ninguno / caja / malla exacta (envolvente convexa si es dinámico), reimportación al guardar, selección por triángulos, arrastrar desde el Proyecto. Jugador en tercera persona (por defecto) o primera: cámara orbital detrás con colisión, personaje que gira hacia donde anda, rueda para la distancia. Interfaz de scripts `ui.*` en modo inmediato y píxeles de la escena: texto con fuente de píxeles, paneles, barras, botones, ajuste de líneas y cursor libre para menús. Ejemplos: `hud.luau`, `aldeano.luau`. | Animación esquelética de los modelos, guardar partida, vista Juego separada, fuente de píxeles propia más pequeña. |
| — | ✅ Flujo tipo Unity | Selección múltiple (Jerarquía, Ctrl/Shift, rectángulo) con contorno naranja y gizmo sobre todos; copiar, pegar, renombrar en el sitio y reordenar arrastrando; objetos activos/inactivos; cámara de vuelo con clic derecho + WASD y ajuste con Ctrl; Inspector con menú de componente y buscador; Proyecto en dos columnas con arrastrar y soltar recursos; preferencias, distribución, cámara y escenas recientes recordadas; Consola con enlaces a scripts, limpiar al jugar y pausa en error. | Vista Juego separada de la Escena. |
| 6, 7 | ⬜ Sin empezar | — | Según la tabla siguiente. |

### Medidas del terreno (equipo de desarrollo, editor en Debug)

| Terreno | Generar | Mallar todo | Peor chunk | Caras tras greedy meshing | Caras sin fusionar | Memoria GPU |
|---|---|---|---|---|---|---|
| 128 × 64 × 128 (32 chunks) | 8 ms | 17 ms | 1,3 ms | 4.289 | ~1,5 millones | 0,1 MB |
| 512 × 128 × 512 (1.024 chunks) | 97 ms | 306 ms | 1,8 ms | 61.118 | ~22 millones | 1,9 MB |

El remallado se reparte entre fotogramas con un presupuesto de 6 ms, así que editar o generar no congela el editor.

| Etapa | Qué se construye | Criterio de aceptación |
|---|---|---|
| **0. Base compilable** | Configurar CMake, compilar el runtime y ejecutar las pruebas de geometría. Preparar el diagnóstico opcional del cargador Vulkan. | Editor y Player arrancan y crean la escena de prueba. CTest pasa. El diagnóstico informa la versión del cargador en un equipo con Vulkan. |
| **1. Ventana y viewport Vulkan** | Seleccionar la GPU, crear la superficie de presentación y dibujar la escena con profundidad. Añadir cámara de edición, cuadrícula, paneles, selección y gizmos. | Abrir el editor, ver la caja y la cuadrícula, seleccionar la caja, moverla y redimensionar la ventana con validación activa. |
| **2. Construcción sin Blender** | Colocar cajas y rampas con ajuste a cuadrícula, duplicar tramos, pintar caras y guardar grupos como prefabs. Añadir deshacer y rehacer, guardado de escenas y referencias estables a recursos. | Construir una habitación con entrada y rampa, pintar sus superficies, deshacer un trazo y reabrir la escena con sus piezas editables. |
| **3. Código y modo Play** | Integrar Luau y la pestaña Código. Adjuntar scripts, exponer parámetros en el Inspector, despachar eventos y recargar al guardar con diagnósticos. | Programar una puerta interactiva en el editor, cambiar su velocidad en el Inspector y probarla. Stop descarta la copia de simulación y recupera la escena de edición. |
| **4. Física y sonido** | Integrar Jolt para cuerpos, colisiones y zonas de activación. Integrar miniaudio para efectos, música y emisores 3D. Exponer eventos y operaciones a Luau. | Recorrer la habitación, subir la rampa, empujar una caja y oír cómo cambia un sonido al acercarse a su emisor. |
| **5. Pixel art y shaders** | Dibujar a resolución interna configurable, con un preset de 320 × 180 y escalado nearest. Añadir iluminación por bandas, paletas, niebla y sombras. Integrar Slang y los parámetros de material en el Inspector. | Editar el shader de un farol y ver el cambio al guardar. Un error conserva el material anterior. Revisar nitidez, siluetas y parpadeo al recorrer el pueblo. |
| **6. Rendimiento y escenas grandes** | Medir CPU, GPU, física, scripts y memoria. Compartir mallas y materiales, dibujar repetidos con instancias, descartar sectores fuera de cámara y reconstruir solo zonas editadas. | Comparar recorridos con 1.000 y 10.000 objetos decorativos e identificar los cuellos de botella. Usar 60 FPS como objetivo provisional en el PC de referencia por definir. |
| **7. Exportación del primer juego** | Preparar escenas, scripts, shaders y recursos para Player. Empaquetar el juego y comprobar su arranque desde una carpeta limpia. | Otra persona puede ejecutar el pueblo exportado, recorrerlo, abrir la puerta y oír el sonido 3D desde Player. |

## Dependencias entre etapas

- El viewport aporta la cámara, selección y coordenadas de pantalla que comparte el constructor.
- La escena editable se copia al pulsar Play. Stop descarta esa copia y vuelve al mundo de edición.
- Luau controla el comportamiento de juego. Slang controla el aspecto de los materiales. Los dos tienen edición y diagnósticos propios.
- La medición empieza con el primer viewport. La etapa de rendimiento usa esas métricas y separa decoración, cuerpos activos, luces y scripts para atribuir el coste.

## Entregables de referencia

El primer entregable visual es una caja seleccionable que se puede mover con un gizmo en el viewport Vulkan. La primera demo jugable es una habitación construida en el editor, con una puerta programada en Luau y un recorrido con física y audio. La habitación se amplía hasta el pueblo pixel art para probar el estilo, el rendimiento y la exportación.

## Protocolo de rendimiento

Definir PC de referencia, GPU y driver, resolución de salida y preset. Compilar Release y ejecutar el mismo recorrido con calentamiento y captura de tiempos. Registrar p50, p95 y p99 en ms, tiempos CPU/GPU por subsistema, llamadas de dibujo, instancias visibles y memoria. Comparar editor y Player por separado.

Variar un eje cada vez: 1.000 y 10.000 instancias decorativas, cantidad de scripts activos, cuerpos despiertos, luces que proyectan sombras y transparencias. Usar semilla fija. Presupuesto provisional de 16,67 ms para 60 FPS y p95 como criterio inicial en el equipo de referencia. Ninguna cifra representa un rendimiento ya medido.

## Regresiones de integración

Duplicar una pieza conserva material y cambia su identidad. Renombrar un recurso conserva referencias. Una escena inválida deja la actual abierta. Un shader inválido preserva la imagen. Un script con bucle agota su presupuesto y devuelve el control. Repetir Play/Stop sin dejar cuerpos o voces huérfanos. Probar resize, minimización, cargas canceladas y sustitución de recursos con Vulkan Validation Layers.
