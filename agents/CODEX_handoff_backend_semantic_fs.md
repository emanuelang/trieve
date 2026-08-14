# Documento de Traspaso de Sesion de IA

## 01. Mision Principal

Construir progresivamente el backend de un sistema de archivos semantico en C++.

Objetivo inmediato: dejar armado un backend con estructura clara, CMake, vcpkg, Makefile, dependencias, y un primer extractor OCR basado en Tesseract capaz de convertir imagenes en texto.

Proposito mas amplio: crear un producto que pueda escanear archivos locales, extraer contenido textual de distintos formatos, generar chunks, embeddings, guardar datos y permitir busqueda semantica.

## 02. Marco Establecido

- Lenguaje principal del backend: C++20.
- Sistema de build: CMake.
- Gestor de dependencias: vcpkg.
- Entorno actual: Windows, PowerShell, Visual Studio/MSVC.
- Carpeta del proyecto: `C:\Users\54343\OneDrive\Desktop\sistema_archivos_semanticos`.
- Backend ubicado en `Backend`.
- La etapa actual es de desarrollo, no de producto final.
- Para cliente final, no se espera que compile ni instale modelos manualmente.
- Se acordo separar:
  - Extractores principales de archivo.
  - Servicios reutilizables de extraccion interna, como OCR o Whisper.
- Se prefiere composicion sobre herencia cuando un extractor necesita usar otra funcionalidad.

## 03. Areas Tematicas

- Arquitectura de backend en C++.
- Organizacion de carpetas para un sistema semantico.
- CMake y vcpkg.
- Makefile para simplificar setup y build.
- OCR con Tesseract.
- Uso de Leptonica para cargar imagenes.
- Diferencia entre entorno de desarrollo y producto final.
- Diseno con Factory Method.
- Separacion entre extractores de archivo y servicios reutilizables.
- Extraccion de contenido compuesto, por ejemplo PDF con texto e imagenes.
- Representacion de resultados como un unico archivo original con multiples segmentos extraidos.

## 04. Requisitos de Comunicacion

- **Tono:** casual, claro, tecnico pero accesible.
- **Nivel de detalle:** paso a paso cuando se trabaja con comandos o arquitectura.
- **Formato:** ejemplos concretos, diagramas simples en texto, snippets C++ cuando ayudan.
- **Alcance:** practico, orientado a implementar el sistema.
- **Restricciones:** evitar sobrecomplicar; explicar decisiones de arquitectura con ejemplos simples.

## 05. Perfil del Usuario y Contexto

- **Rol/Nivel de Experiencia:** estudiante o desarrollador en etapa de diseno e implementacion de backend C++.
- **Objetivos Actuales:** construir un sistema semantico escalable y eventualmente convertirlo en producto.
- **Restricciones de Trabajo:** trabaja en Windows, usa PowerShell, CMake, vcpkg y Makefile.
- **Antecedentes Relevantes:** esta disenando arquitectura orientada a extractores, OCR, futuros PDFs, audio, video y quiza Whisper.

## 06. Trayectoria de la Conversacion

- **Punto de Partida:** el usuario pidio crear la estructura inicial de carpetas del backend.
- **Desarrollos Clave:**
  - Se creo la estructura `Backend/include/semantic_fs/...` y `Backend/src/...`.
  - Se configuro `vcpkg.json` con `fmt`, `spdlog`, `nlohmann-json`.
  - Se creo `CMakeLists.txt`.
  - Se agrego `Makefile`.
  - Se implemento OCR con Tesseract.
  - Se compilo y probo el backend.
  - Se discutio como deberia funcionar esto en etapa de desarrollo vs producto final.
  - Se discutio la arquitectura con Factory Method, extractores y servicios reutilizables.
- **Estado Actual:** el backend compila en Debug y tiene un extractor OCR funcional.
- **Impulso:** la conversacion esta avanzando hacia diseno de arquitectura limpia para multiples tipos de archivo.

## 07. Recursos y Referencias Externas

- `C:\tools\vcpkg`: instalacion local usada para verificar dependencias.
- `tesseract` via vcpkg.
- `eng.traineddata` descargado desde `tessdata_fast`.
- Imagen de prueba del usuario:
  - `C:\Users\54343\OneDrive\Imagenes\Screenshots\prueba.png`
- Diagramas enviados por el usuario sobre:
  - `extractor`
  - `extraer_contenido`
  - `ocr`
  - `whisper`
  - `video`
  - `extractor_audio`
  - `clasificar_contenido`

## 08. Resultados y Artefactos Generados

- Estructura de backend creada en:
  - `Backend/include/semantic_fs/...`
  - `Backend/src/...`

- Archivos configurados:
  - `Backend/CMakeLists.txt`
  - `Backend/vcpkg.json`
  - `Backend/Makefile`

- Clase OCR creada:
  - `Backend/include/semantic_fs/extractors/tesseract_ocr_extractor.h`
  - `Backend/src/extractors/tesseract_ocr_extractor.cpp`

- `main.cpp` actualizado para debug:
  - recibe ruta de imagen por argumento.
  - ejecuta OCR.
  - imprime texto detectado.

- Dependencias agregadas:
  - `fmt`
  - `spdlog`
  - `nlohmann-json`
  - `tesseract`

- Datos OCR agregados:
  - `Backend/tessdata/eng.traineddata`

- Prueba realizada:
  - Imagen temporal con texto `HELLO OCR 123`.
  - Resultado OCR correcto: `HELLO OCR 123`.

## 09. Decisiones Criticas y Razonamiento

- **Usar Tesseract como OCR inicial.**
  - Razon: permite convertir imagenes en texto localmente desde C++.

- **Usar vcpkg para dependencias.**
  - Razon: facilita instalacion reproducible de librerias nativas en Windows.

- **Agregar `tessdata` al flujo de setup.**
  - Razon: Tesseract necesita modelos de idioma en runtime; la libreria sola no alcanza.

- **Separar extractor principal de servicio OCR.**
  - Razon: una imagen y un PDF pueden usar OCR, pero no deben modelarse como la misma entidad.

- **Guardar resultados bajo el archivo original.**
  - Razon: si un PDF contiene imagenes, el texto OCR de esas imagenes sigue perteneciendo al PDF original.

- **Factory solo para el extractor inicial.**
  - Razon: evita que cada subcontenido se convierta en un archivo separado.

## 10. Proximos Pasos Inmediatos

- **Prioridad 1:** formalizar interfaces base:
  - `IFileExtractor`
  - `IOcrExtractor` o `IOcrService`
  - `ExtractionResult`
  - `ExtractedSegment`

- **Prioridad 2:** implementar `ImageExtractor` usando `TesseractOcrExtractor`.

- **Dependencias:** definir bien el modelo de resultado antes de agregar PDF, audio o video.

- **Puntos de Decision:**
  - si usar `"eng"`, `"spa"` o `"spa+eng"`.
  - si `tessdata` debe versionarse en el repo o descargarse en setup.
  - si el extractor PDF primero solo extrae texto o tambien imagenes internas.

## 11. Elementos Sin Resolver

- Falta agregar soporte OCR en espanol con `spa.traineddata`.
- Falta definir interfaces definitivas.
- Falta implementar `ExtractorFactory`.
- Falta definir como guardar resultados en base de datos.
- Falta decidir si los segmentos tendran metadata como pagina, timestamp, tipo de fuente, bounding box, etc.
- Falta decidir como empaquetar el producto final para clientes.
- Falta implementar extractores reales de PDF, audio y video.
- Falta preprocesamiento de imagenes para mejorar OCR.

## 12. Proximo Prompt de Activacion de IA

Estas continuando una conversacion sobre el backend C++ de un sistema de archivos semantico. Basado en este documento de traspaso, tu rol es ayudar a disenar e implementar una arquitectura limpia de extractores usando Factory Method, interfaces y servicios reutilizables.

Contexto actual: ya existe un backend en `C:\Users\54343\OneDrive\Desktop\sistema_archivos_semanticos\Backend` con CMake, vcpkg, Makefile y un OCR funcional basado en Tesseract. El usuario quiere que los extractores principales representen archivos completos, mientras que funcionalidades como OCR o Whisper sean servicios reutilizables usados internamente.

Tarea inmediata: disenar e implementar las interfaces base `IFileExtractor`, `IOcrExtractor`, `ExtractionResult` y `ExtractedSegment`, y adaptar `TesseractOcrExtractor` para que encaje en esa arquitectura.

Restricciones de trabajo: mantener C++20, respetar la estructura existente del backend, usar composicion en vez de herencia innecesaria, y conservar que los resultados pertenezcan siempre al archivo original.

Por favor revisa primero los archivos actuales del backend, propone la estructura minima necesaria y luego implementa los cambios de forma incremental, verificando que el proyecto compile en Debug.
