# Luau embebido y edición de código

## Estado actual

`ge3d_scripting` (engine/scripting) contiene `ScriptHost` y `describeScript`. Los headers no exponen tipos de Luau.

- **Formato**: un script devuelve una tabla; `params` declara los parámetros (número, booleano o texto) con su valor por defecto y las funciones `onStart(self)`, `onUpdate(self, dt)`, `onInteract(self)`, `onMessage(self, evento, valor, origen)` y `onStop(self)` son los callbacks. El cuerpo del script se ejecuta una vez por VM; cada entidad recibe su propio `self` con los parámetros y `self.entity`.
- **API**: `Entity` (userdata con `position`, `rotation`, `scale` como `vector`, `name`, `id`, `send`), `scene.find`, `scene.send`, `scene.time`, `input.isDown`, `input.wasPressed`, `log`/`print`. Una entidad eliminada produce un error claro al usarla.
- **Sandbox**: `luaL_sandbox` (bibliotecas y globales de solo lectura) y `luaL_sandboxthread` por script; sin `debug`, `loadstring`, `getfenv`/`setfenv`; Luau no trae `io` ni `os.execute`. Memoria limitada a 64 MB con un asignador propio.
- **Presupuesto**: interrupción de la VM (`lua_callbacks()->interrupt`) a 1 ms por llamada; 3,5 ms por fotograma para todos (lo que no cabe sigue en el siguiente fotograma, en turno rotatorio). Un error o un exceso desactiva la instancia y su `ScriptComponent` en la copia de Play y se informa con la traza.
- **Recarga**: si compila y el cuerpo se ejecuta bien, las instancias se recrean con estado nuevo y los mismos parámetros; si no, se conserva la versión anterior.
- **Editor**: pestaña Código (documentos en pestañas, números de línea, comprobación al dejar de escribir, lista de errores con salto a línea, Ctrl+S), componente Script en el Inspector, clic en Play = `onInteract`, cambios de parámetros en Play aplicados en vivo.
- **Pruebas**: `tests/script_tests.cpp` (parámetros, puerta, bucle infinito, sandbox, mensajes, errores con línea, recarga, guardado).

Pendiente: resaltado de sintaxis, autocompletado desde la API, `onTriggerEnter` con física, ejecución en Player.

## Autoría y API

Un ScriptComponent guarda UUID del archivo, estado habilitado y parámetros serializables. Cada instancia tiene su estado. Un descriptor de script define campos de Inspector con tipo y valor por defecto. La API del motor ofrece handles con generación, acciones de entrada, consultas de física, audio, temporizadores y creación de entidades por prefab.

Editor Código con pestañas, resaltado, diagnósticos y salto a archivo/línea. La API exportada y sus declaraciones de tipos deben derivar de un contrato común. Inicialmente, cubrir creación de archivo, edición, guardado, compilación y ejecución en Play. Incorporar autocompletado a partir de esa API.

## Ejecución

- Compilar fuentes propias con el compilador integrado. Ofrecer bibliotecas reducidas y bindings definidos por el motor. Ejecutar gameplay en Play y Player.
- Callbacks onStart, onStop, onInteract, onTriggerEnter y onFixedUpdate. Registrar slots al cargar para evitar descubrir funciones cada frame. Usar colas de eventos para entidades que interactúan desde física.
- Limitar memoria del entorno, medir asignaciones y comprobar el presupuesto temporal con interrupciones de la VM. Cada binding nativo también debe acotar su trabajo. Mostrar el error y desactivar la instancia que agote reiteradamente el presupuesto.
- Proporcionar operaciones de alto nivel y consultas por lotes. Evitar recorrer todas las entidades con callbacks individuales. Usar C++ para animación masiva, broadphase, simulación y extracción de render.
- Compilar cambios en segundo plano y activar al límite de una actualización. Si falla, mantener el script previo. En el primer modelo, una recarga exitosa reinicia estado privado y conserva parámetros compatibles. Limpiar suscripciones anteriores al reemplazar la instancia.

## Medición

Registrar tiempo y llamadas por tipo de callback, asignaciones, memoria de VM, coste de bindings y fallos de presupuesto. Medir por separado una escena con miles de instancias de decoración y otra con muchos scripts activos. Evaluar generación de código nativo solo a partir de perfiles del gameplay real.
