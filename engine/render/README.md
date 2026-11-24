# Backend Vulkan y pixel art

## Inicialización y fotograma

Crear instancia, superficie, selección de GPU, dispositivo, colas, swapchain y VMA. Comprobar dynamicRendering, synchronization2 y soporte de presentación. Preparar recursos por frame en vuelo y sincronización de adquisición y presentación. Tratar redimensionado, ventana minimizada y swapchain obsoleta. Recrear sus imágenes y vistas cuando terminen los usos pendientes.

Orden inicial de pasadas:

1. Escena forward en color y profundidad a resolución interna configurable.
2. Composición nearest a tamaño entero con bandas centradas cuando quepa.
3. HUD, herramientas y GUI a resolución nativa.

Para ventanas menores que la resolución interna, definir una salida explícita de escalado fraccional nearest. Los gizmos de escena usan las mismas matrices y el mismo rectángulo de composición para mantener la coincidencia visual y de picking.

## Datos y rendimiento

Separar malla, material e instancia. Clasificar opacos por pase, pipeline, material y malla. Instanciar repetidos y almacenar tintes e índice de paleta por instancia. Descartar sectores y después objetos usando sus volúmenes. Las transparencias que lo requieran se ordenan por distancia. La geometría estática derivada puede almacenarse por sector y material y regenerarse por revisión.

Medir tiempos con queries de GPU sin bloquear el frame para esperar resultados. Considerar multi-draw indirect si preparar comandos en CPU domina las mediciones y la GPU lo soporta.

## Contrato de shaders

Slang → SPIR-V y reflexión. Reservar bloques para frame/cámara y recursos del material, con datos de objeto o instancia suministrados por el motor. El descriptor del material agrega valores por defecto, rangos y nombres visibles. Plantillas iniciales unlit, iluminación por bandas y recorte alfa.

Compilar con una clave de fuente, dependencias, opciones, etapa, target y versión de compilador. Validar las interfaces en recarga, conservar el pipeline previo ante error y retirar sus recursos después de la GPU. La caché de pipelines Vulkan se guarda aparte y se invalida si corresponde a un dispositivo incompatible.

## Dirección visual

320 × 180 como preset inicial. Probar cámara ortográfica y perspectiva, mipmaps para superficies distantes, sombras estables y contraste de siluetas. La escena del pueblo comprueba parpadeo en movimiento, continuidad de texel y sobrecarga de transparencias. Paleta, niebla, dithering y luces locales se habilitan como decisiones de cada proyecto.
