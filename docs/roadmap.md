# Etapas de Game-Engine3D

La base C++20 incluye un runtime compartido, ejecutables de consola y una escena en memoria con piso, caja y rampa. La compilación y CTest siguen pendientes de verificar.

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
