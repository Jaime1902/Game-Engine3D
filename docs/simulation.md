# Física y audio

## Física con Jolt

Mapear entidades a cuerpos mediante handles y conservar la capa de colisión en el componente. Colisionadores caja, esfera, cápsula y convex hull para rampas. Cuerpos estáticos, dinámicos y cinemáticos. Las superficies de autoría generan colisionadores sencillos acordes a su geometría.

Simular inicialmente a 60 Hz con acumulador y máximo de subpasos por frame. Registrar desbordes. Interpolar poses visuales con el estado previo y actual. En Play, las órdenes sobre cuerpos dinámicos pasan por fuerzas, impulsos o teletransporte explícito. Validar escalas no uniformes y restricciones de jerarquía antes de crear el cuerpo.

Encolar contactos y triggers para despacharlos después de simular. Proveer raycasts, capas y visualización de colisionadores. Casos de aceptación: piso soporta una caja, personaje sube una rampa y un trigger invoca un evento una sola vez al entrar.

## Audio con miniaudio

AudioSource referencia un recurso y define volumen, pitch, loop, atenuación y grupo. AudioListener sigue la cámara de juego. Grupos Master, Música y SFX. Precargar efectos cortos usados con frecuencia y usar streaming para música larga. Presupuestar voces y priorizar por distancia e importancia.

Separar la mezcla en tiempo real del trabajo de E/S. Publicar poses y comandos sin bloquear el callback de audio. Stop libera o pone en silencio las voces del mundo de Play. Verificar orientación, caída por distancia, cambio de grupo y ciclos Play/Stop.
