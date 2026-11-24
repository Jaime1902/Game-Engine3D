# Recursos y exportación

El proyecto contiene recursos editables y un registro de metadatos con UUID estable. La caché local es regenerable y se identifica por contenido, ajustes de importación, formato y versión de herramienta. Renombrar desde el editor actualiza rutas conservando referencias por UUID. Duplicar genera UUID nuevo.

Tipos iniciales: primitivas propias, escenas, prefabs, materiales, Luau, Slang, PNG y WAV. GLB es una vía opcional de importación. Registro inverso de dependencias para reimportar materiales dependientes cuando cambia una textura o un módulo Slang.

El pipeline de exportación propuesto parte de la escena inicial, resuelve dependencias transitivas, valida referencias y produce un manifiesto versionado, recursos preparados y un ejecutable Player. Resolver rutas relativas al manifiesto. Escribir primero a una carpeta temporal y publicar solo la exportación completa. La prueba final ejecuta el resultado desde una carpeta limpia.
