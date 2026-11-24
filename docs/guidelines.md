# Directrices técnicas y contratos de Game-Engine3D

Esta guía fija las decisiones arquitectónicas, los invariantes técnicos y los **límites estrictos (Non-Goals)** para la implementación del roadmap. Su objetivo es evitar la sobre-ingeniería, la invención de formatos o capas innecesarias y los errores críticos de implementación en Vulkan, ECS, física y scripting.

---

## 1. Límites Infranqueables de Alcance (NON-GOALS)

Queda **terminantemente prohibido** implementar o proponer cualquiera de los siguientes elementos durante las etapas 0 a 7:

* ❌ **NO Multi-API RHI:** Prohibido crear capas de abstracción para DirectX 12, Metal o WebGPU. El único backend gráfico es Vulkan 1.3 nativo.
* ❌ **NO Renderizado PBR Complejo:** Prohibido implementar modelos de microfacetas GGX complejos, múltiples texturas de rugosidad/metálico o iluminación global (GI). El estilo visual es **retro pixel art 3D**: shading por bandas (cel-shading), unlit, paletas indexadas y diffuse ramps.
* ❌ **NO Sombras en Cascada (CSM) de múltiples splits ni Ray Tracing:** Máximo una única sombra ortográfica direccional para la luz principal ("Sol").
* ❌ **NO Formatos Binarios Propietarios de Malla o Escena:** Prohibido diseñar parsers binarios (`.bin`, `.ge3dmesh`, `.pak`). Todo recurso de autoría debe ser legible en texto plano o formatos estándar.
* ❌ **NO Networking / Multijugador:** El motor es monousuario estricto para escritorio Windows x64.
* ❌ **NO Compute Shaders para partículas masivas ni Skinning Esquelético:** Toda la geometría inicial son primitivas estáticas/paramétricas (cajas, rampas) y mallas rígidas.

---

## 2. Invariantes de Vulkan, Memoria y Sincronización

### 2.1. Gestión de Buffers y Memoria con VMA (Vulkan Memory Allocator)
* **Prohibición:** Prohibido invocar `vkAllocateMemory` de forma directa o crear un `VkBuffer`/`VmaAllocation` individual para cada entidad o primitivo en escena.
* **Política de Crecimiento del Geometry Pool:**
  - El almacén de geometría no se deja a la improvisación futura. Se estructura mediante **chunks fijos**: bloques de **4 MB para Vertex Buffers** y **2 MB para Index Buffers** gestionados con VMA (`VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE`).
  - Cuando un chunk se llena, se asigna un **nuevo chunk independiente**. Prohibido realizar `realloc` destructivos que invaliden descriptores o fuercen un `vkDeviceWaitIdle()`.
  - Para subidas CPU $\rightarrow$ GPU, usar un **Staging Buffer reutilizable** (ring buffer de 8 MB) persistente mapeado en memoria host visible.

### 2.2. Manejo de Minimización y Ciclo de Vida del Swapchain
* **Bucle de Minimización Correcto:** En Windows, al minimizar la ventana, `glfwGetFramebufferSize` devuelve `(0, 0)`. El bucle debe esperar eventos pero **permitir el cierre si el usuario cierra la ventana minimizada**:
  ```cpp
  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  while ((width == 0 || height == 0) && !glfwWindowShouldClose(window)) {
      glfwWaitEvents();
      glfwGetFramebufferSize(window, &width, &height);
  }
  if (glfwWindowShouldClose(window)) {
      return; // Salida limpia sin recrear swapchain
  }
  ```
* **Contrato de Sincronización Canónica de Presentación:**
  - **Por Frame-in-Flight (2 frames):** 1 `VkFence` (para saber cuándo la CPU puede reescribir el command buffer del frame) y 1 `VkSemaphore` (`imageAvailableSemaphore`).
  - **Por Imagen de Swapchain (típicamente 3 imágenes):** 1 `VkSemaphore` (`renderFinishedSemaphore`). Esto garantiza que nunca se intente reutilizar un semáforo de presentación que el Presentation Engine del driver aún tenga en uso.

---

## 3. Arquitectura de Escena, Jerarquía y ECS

### 3.1. Estructura de Datos y EnTT
* El almacenamiento principal de componentes se delega a **EnTT**, pero las relaciones espaciales no deben modelarse mediante búsquedas recursivas lineales.
* **Componente de Jerarquía Explícito:**
  ```cpp
  struct HierarchyComponent {
      EntityHandle parent{NullEntity};
      EntityHandle firstChild{NullEntity};
      EntityHandle nextSibling{NullEntity};
      EntityHandle prevSibling{NullEntity};
      uint32_t depth{0};
  };
  ```
* **Invariante de Escala:** Prohibido el *shear* (deformación oblicua). La escala no uniforme combinada con rotación jerárquica queda rechazada por el inspector. La escala en jerarquías debe ser uniforme o no propagarse a la rotación de los hijos.
* **Topological Sort:** El cálculo de matrices mundiales derivadas (`worldMatrix`) debe realizarse en orden topológico (padres antes que hijos) y únicamente cuando la bandera `isDirty` esté activa.

### 3.2. Aislamiento Estricto de Modo Play (Inmortalidad del Editor)
* Al pulsar **Play**, se serializa la escena a un snapshot en memoria (JSON o clonación binaria directa de los pools de EnTT).
* Durante Play, Jolt Physics y Luau operan sobre la copia de simulación.
* Al pulsar **Stop**, se destruye completamente el mundo de simulación y se restaura el snapshot. **Ningún puntero, cuerpo de Jolt o instancia de Luau puede sobrevivir a Stop.**

---

## 4. Pipeline de Render Pixel Art (320 × 180) y Samplers

### 4.1. Separación de Espacios y UI
1. **Pass 3D Offscreen:** Se renderiza a un `VkImage` offscreen de 320 × 180 (color `R8G8B8A8_UNORM`, profundidad `D32_SFLOAT`).
2. **Pass Composición:** Se realiza un reescalado nearest-neighbor con bandas negras (letterboxing/pillarboxing) dentro de la textura del viewport del editor.
3. **Pass UI:** Dear ImGui se dibuja **a resolución nativa** de la ventana (ej. 1080p, 1440p) con fuentes rasterizadas a tamaño completo. Prohibido dibujar ImGui a 320 × 180.

### 4.2. Contrato Estricto de Samplers y Anti-Blur
* En estética pixel art 3D a 320 × 180 con paletas indexadas, los mipmaps convencionales con filtrado bilineal o trilineal **destruyen el estilo** al emborronar y promediar colores de paleta.
* **Contrato Obligatorio:**
  - Samplers de escena configurados con `VK_FILTER_NEAREST` tanto en minificación (`minFilter`) como en magnificación (`magFilter`).
  - Prohibido el filtrado trilineal. Si se usan mipmaps en objetos lejanos para evitar aliasing extremo, el sampler debe forzar `mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST`.
  - Los atlases de texturas deben tener un **padding mínimo de 2 texels con bordes repetidos (clamp-to-edge)** por cuadrante para evitar sangrado (*texture bleeding*).

---

## 5. Sandboxing y Presupuesto Matemático de Luau

### 5.1. Lista Negra de Bibliotecas
* La máquina virtual de Luau debe configurarse sin acceso al sistema operativo ni al sistema de archivos del host.
* **Bibliotecas terminantemente prohibidas en scripts de juego:**
  - `io.*` (Completamente deshabilitada).
  - `os.execute`, `os.remove`, `os.rename`, `os.exit`, `os.getenv` (Eliminadas).
  - `package.*` y `require` sin resolver por el asset manager del motor.
  - `debug.*` (Deshabilitada en runtime de Player).

### 5.2. Presupuesto Exacto de Frame (Frame Budget a 60 FPS = 16.67 ms)
* El reparto de tiempo por fotograma queda prefijado:
  - Backend Vulkan + Viewport + ImGui: **6.0 ms**
  - Simulación física Jolt (paso fijo a 60 Hz + contactos): **4.0 ms**
  - Audio miniaudio + sincronización de matrices: **1.5 ms**
  - Margen del SO y fluctuaciones: **1.67 ms**
  - **Presupuesto TOTAL AGREGADO para Luau:** Máximo **3.5 ms por fotograma**.
  - **Presupuesto MÁXIMO por script individual:** **1.0 ms**.
* Si un script individual supera 1.0 ms o entra en bucle infinito (`while true do end`), la VM interrumpe cooperativamente la ejecución (`lua_sethook`), desactiva el componente `ScriptComponent` de esa entidad y emite un error con backtrace a la consola del editor.

---

## 6. Audio y Físicas: Reglas de Tiempo Real

### 6.1. Miniaudio: Política de Saturación y Colas Lock-Free
* El callback de `miniaudio` corre en un hilo de tiempo real del SO.
* **Prohibido dentro del callback:** `malloc`, `free`, llamadas a disco o locks no acotados.
* **Política de Saturación de Colas:** Se implementan **dos canales separados**:
  1. **Cola de Control Crítica (Alta Prioridad):** Comandos `Stop`, `Release`, cambios de volumen y pause. **Nunca se descartan.** Si esta cola se satura, se considera error fatal de diseño (`GE3D_ASSERT`).
  2. **Cola de Disparo de Efectos (One-Shots):** Si se satura, se aplica **robo de voces (*voice stealing*)** descartando el sonido de menor prioridad o más distante respecto al `AudioListener`.

### 6.2. Jolt Physics: Paso Fijo Obligatorio
* Frecuencia fija obligatoria: **60 Hz** ($\Delta t = 1/60 \text{ s} \approx 16.66 \text{ ms}$).
* Uso de acumulador de tiempo con clamp para evitar el "espiral de la muerte" (*death spiral*):
  ```cpp
  accumulator = std::min(accumulator + frameTime, maxAccumulatedTime); // max ~ 0.1 s
  while (accumulator >= fixedDeltaTime) {
      physicsSystem->Update(fixedDeltaTime, ...);
      accumulator -= fixedDeltaTime;
  }
  float alpha = accumulator / fixedDeltaTime; // Factor de interpolación para el render
  ```

---

## 7. Modelo de Concurrencia y Hilos

* **Hilo Principal:** Dueño exclusivo del bucle de eventos, la simulación física (Jolt), la ejecución de scripts (Luau), la actualización de EnTT y la grabación de Command Buffers de Vulkan.
* **Worker Threads (Job Pool fijo):** Únicamente para:
  1. Carga y descompresión de texturas/sonidos en memoria RAM.
  2. Compilación de shaders Slang $\rightarrow$ SPIR-V en caliente.
* **Invariante:** Los hilos de fondo **nunca** mutan el registro de EnTT ni llaman a funciones de Vulkan que requieran sincronización en la misma cola. Los resultados terminados se encolan y se integran en el hilo principal al inicio del siguiente frame.

---

## 8. Formato de Recursos y Metadatos de Shaders

* **Escenas y Prefabs:** Archivos JSON en formato UTF-8 (validables mediante schema de versión).
* **Identificadores:** UUID v4 (RFC 4122) de 128 bits en string canónico (`xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx`).
* **Imágenes:** Formato PNG estándar cargado vía `stb_image`.
* **Audio:** Formato WAV (PCM 16-bit / 44.1 kHz o 48 kHz).
* **Shaders y Materiales:**
  - Los shaders se escriben en texto plano `.slang` y se compilan a bytecode SPIR-V (`.spv`).
  - Los parámetros expuestos, rangos, tintes y texturas enlazadas se guardan en un archivo de texto **`.material.json`** derivado de la reflexión de Slang. **Queda prohibido crear formatos binarios propietarios para materiales o mallas.**

---

## 9. Prueba de Aceptación para la Etapa 1 (Smoke Test de Vida)

Para dar por concluida la Etapa 1 antes de integrar Dear ImGui, gizmos o herramientas complejas, el motor debe superar la siguiente prueba automatizada/verificada:
1. Crear ventana GLFW y contexto Vulkan 1.3 con `dynamic_rendering` y `synchronization2`.
2. Asignar el Geometry Pool inicial (chunks fijos con VMA) y subir la geometría de una caja usando el Staging Buffer.
3. Renderizar la caja en pantalla con un shader Slang básico unlit.
4. **Prueba de robustez de ventana:**
   - Redimensionar la ventana arrastrando esquinas (recreación limpia del swapchain).
   - Minimizar a la barra de tareas (el bucle se suspende sin crashear).
   - Restaurar desde la barra de tareas (el render continúa inmediatamente).
   - Cerrar la ventana con la cruz mientras está minimizada y normal.
5. **Criterio de salida:** Las **Vulkan Validation Layers** deben terminar en **verde absoluto** (0 errores, 0 advertencias de memoria o sincronización).
