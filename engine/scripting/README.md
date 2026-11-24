# Luau embebido y edición de código

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
