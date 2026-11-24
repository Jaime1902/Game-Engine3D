# Editor visual y constructor integrado

## Ventanas y entrada

El viewport Escena dibuja una cámara propia. Juego toma la cámara activa de simulación. Jerarquía, Inspector, Recursos, Código, Consola y Perfilador comparten selección por ID. El teclado distingue el foco de texto, el viewport y las herramientas. Prioridad de entrada en el viewport: panel activo, gizmo, herramienta de construcción, selección y cámara.

Picking inicial con un rayo contra cajas de mundo y malla cuando se necesite precisión por cara. Definir conversión de píxel de ventana a framebuffer cuando haya bandas o DPI distinto. El gizmo manipula en espacio local o global y confirma una sola operación de historial al soltar.

## Herramientas sin Blender

- Primitivas paramétricas caja y rampa. Guardar tamaño, transform, material por cara y perfil de colisión.
- Pincel de colocar/borrar con vista previa, rejilla configurable, giro de 90°, relleno rectangular y repetición lineal. Cada trazo es una transacción de deshacer.
- Material por cara con UV planas y escala en metros. Mantener el tamaño aparente del texel al extender una pared.
- Agrupar piezas en prefabs. Previsualizar el efecto de modificar el recurso y aplicar cambios a sus instancias.
- Añadir herramientas de caras y extrusión cuando las primitivas y prefabs estén probados. Las geometrías generadas son derivadas de los datos de autoría.

## Edición y Play

Operaciones típicas Create, Delete, SetProperty y Reparent, con datos para deshacer y rehacer. Cerrar un arrastre sin confirmarlo revierte la previsualización. La selección se depura tras borrar o deshacer.

Edit → Play crea una copia del mundo editable. Pause mantiene el mundo de simulación. Step ejecuta un paso fijo mientras está en pausa. Stop descarta la simulación y vuelve a la cámara y selección de edición que sigan existiendo.
