# Arquitectura y contratos

## Dependencias

Editor y Player enlazan Runtime. Editor es dueño de ventanas, herramientas, comandos de edición y diagnósticos. Player crea la presentación y carga contenido preparado. Runtime mantiene Scene, Geometry, Render, Assets, Scripting, Physics y Audio. La escena contiene datos y referencias, mientras cada sistema administra sus propios recursos de Vulkan, Jolt, miniaudio o Luau. Los headers de escena evitan exponer tipos de esas bibliotecas.

Se propone GLFW, Dear ImGui Docking, ImGuizmo, EnTT, VMA, Jolt y miniaudio. Introducir cada dependencia con su integración, versión fijada y licencia registrada. La compilación de la base solo requiere la biblioteca estándar, y el sondeo opcional requiere el SDK Vulkan.

## Mundo y serialización

- Convención diestra con Y arriba y −Z hacia delante. Metros, segundos y radianes internamente. Inspector en grados. La base de consola guarda una representación provisional en grados.
- Una entidad persistente tiene UUID y componentes serializados. El registro en memoria expone handles con generación para detectar referencias eliminadas. El ID incremental de la base es local a cada escena.
- Transform local con posición, cuaternión y escala. Matrices de mundo derivadas y marcadas como sucias al cambiar el padre o la transformación. Detectar ciclos y rechazar un reparentado que requiera *shear* si se exige conservar el mundo.
- JSON UTF-8 versionado para edición. Carga en una escena temporal, migraciones por versión y validación de referencias antes de sustituir la escena abierta. Guardado por archivo temporal y reemplazo después de cerrar correctamente la escritura.
- Prefab con IDs locales estables. Instancias guardan cambios por ID y propiedad. Eliminar una propiedad o un hijo debe producir un diagnóstico si se pierde un cambio local.

## Actualización y propiedad

Entrada de plataforma → acciones → eventos de juego → pasos físicos fijos → eventos de contacto → sincronización de transformaciones y audio → extracción de datos de render → presentación.

El mundo se modifica en el hilo de simulación. Mutaciones provocadas durante iteraciones o contactos se encolan y aplican en límites definidos. Workers importan recursos y compilan shaders. Vulkan consume datos de render por fotograma y retira recursos después de la señal de finalización asociada. La física controla cuerpos dinámicos en Play. Un snapshot interpolado alimenta la imagen entre pasos fijos.

## Convenciones de integración

Cada módulo nuevo obtiene un target CMake cuando incorpore código y lo enlaza con la visibilidad mínima que requiera. Los recursos externos se referencian por ID y los trabajos asíncronos llevan una revisión para descartar resultados de escenas cerradas o archivos reemplazados.
