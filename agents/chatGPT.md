# AGENTS.md

# Documento de Traspaso de Sesión de IA – Semantic FS

Este documento proporciona el contexto autorizado para cualquier agente de IA que trabaje en este repositorio. Su objetivo es mantener consistencia en la arquitectura, metodología y decisiones del proyecto.

---

# 01. Misión Principal

## Objetivo fundamental

Desarrollar **Semantic FS**, un sistema de archivos semántico completamente local que permita buscar archivos mediante lenguaje natural de forma rápida, eficiente y escalable.

El sistema deberá:

- Indexar archivos de múltiples formatos.
- Convertir su contenido a texto.
- Generar embeddings locales.
- Construir un índice vectorial eficiente.
- Recuperar archivos relevantes mediante prompts.
- Ejecutarse completamente en la máquina del usuario.

## Objetivo a largo plazo

Construir una arquitectura modular, mantenible y portable donde cualquier desarrollador pueda clonar el repositorio y levantar el proyecto con un único comando.

---

# 02. Marco Establecido

## Principios

- Backend en **C++20**.
- Arquitectura modular.
- Separación estricta de responsabilidades.
- Procesamiento completamente local.
- Desarrollo incremental.
- Evitar sobreingeniería en las primeras etapas.

## Pipeline objetivo

```text
FileScanner
    ↓
Extractor
    ↓
TextCleaner
    ↓
Chunker
    ↓
Embeddings
    ↓
Vector Store
    ↓
Semantic Search
```

## Restricciones

- No depender de servicios cloud.
- No subir modelos IA al repositorio.
- Automatizar el entorno mediante Make + CMake + vcpkg.

---

# 03. Áreas Temáticas

## Arquitectura

- Organización modular del backend.
- Separación entre indexación y búsqueda.

## C++

- C++20.
- Organización por módulos.
- Clases pequeñas y responsabilidades únicas.

## Build System

- CMake.
- Make.
- vcpkg.

## IA Local

- llama.cpp (embeddings).
- whisper.cpp (audio/video).
- OCR para imágenes.
- Modelos GGUF locales.

## Búsqueda Vectorial

- Chunks.
- Embeddings.
- Índices tipo HNSW.

---

# 04. Requisitos de Comunicación

## Tono

- Técnico.
- Profesional.
- Práctico.

## Nivel de detalle

- Paso a paso.
- Explicaciones profundas cuando se trate de arquitectura.

## Formato

- Markdown.
- Títulos claros.
- Diagramas ASCII cuando ayuden.

## Alcance

- Priorizar implementación práctica.
- Explicar el razonamiento detrás de las decisiones.

## Restricciones

- No introducir complejidad innecesaria.
- Mantener coherencia con la arquitectura existente.

---

# 05. Perfil del Usuario y Contexto

## Rol

- Estudiante avanzado de Licenciatura en Sistemas Informáticos.
- Desarrollador con conocimientos de C++, Git, Docker y arquitectura de software.

## Objetivos

- Construir un proyecto sólido para portfolio.
- Aprender arquitectura profesional.
- Crear una base escalable.

## Restricciones

- Desarrollo incremental.
- Evitar empezar con todos los módulos implementados.
- Priorizar una base limpia.

---

# 06. Trayectoria del Proyecto

## Punto de partida

La idea inicial fue construir un sistema que buscara archivos mediante lenguaje natural utilizando IA local.

## Decisiones importantes

Se definió separar:

- Scanner
- Extractores
- Procesamiento
- Chunking
- Embeddings
- Storage
- Search

## Estado actual

Actualmente se está construyendo la infraestructura del proyecto:

- estructura de carpetas
- CMake
- Make
- vcpkg
- automatización

Todavía no se implementaron todos los módulos.

---

# 07. Recursos y Referencias Externas

## Tecnologías acordadas

- C++20
- CMake
- GNU Make
- vcpkg
- Git
- SQLite
- llama.cpp
- whisper.cpp
- Tesseract OCR
- FFmpeg
- Drogon (API HTTP futura)

## Modelos

Los modelos deberán descargarse automáticamente mediante scripts.

No deben versionarse.

---

# 08. Resultados y Artefactos Generados

## Arquitectura propuesta

```text
backend/
├── CMakeLists.txt
├── Makefile
├── vcpkg.json
├── external/
│   └── vcpkg/
├── models/
├── scripts/
├── include/
│   └── semantic_fs/
│       ├── scanner/
│       ├── extractors/
│       ├── processing/
│       ├── chunking/
│       ├── embeddings/
│       ├── storage/
│       └── search/
└── src/
    ├── app/
    ├── scanner/
    ├── extractors/
    ├── processing/
    ├── chunking/
    ├── embeddings/
    ├── storage/
    └── search/
```

## Sistema de Build

Se acordó utilizar:

- Make para automatización.
- CMake para compilación.
- vcpkg para dependencias.

---

# 09. Decisiones Críticas y Razonamiento

## C++ como lenguaje principal

Elegido por:

- rendimiento
- bajo consumo
- ejecución local

## CMake

Elegido porque:

- estándar moderno
- multiplataforma
- integración con vcpkg

## Make

Elegido para ocultar comandos largos y simplificar el onboarding.

## vcpkg como submódulo

Se decidió incluir:

```text
backend/external/vcpkg
```

Motivos:

- misma versión para todos
- instalación automática
- mayor portabilidad

## Desarrollo incremental

No crear todas las clases desde el inicio.

Primero construir:

- scanner
- extractor
- cleaner

Luego agregar módulos.

---

# 10. Próximos Pasos Inmediatos

## Prioridad 1

Implementar el sistema de build completo.

- Makefile
- CMake
- vcpkg

## Prioridad 2

Implementar el primer flujo funcional.

```text
carpeta
↓
scanner
↓
lector de texto
↓
limpieza
↓
salida por consola
```

## Dependencias

Antes de IA debe existir una base estable.

## Puntos de decisión futuros

Elegir el motor vectorial definitivo:

- sqlite-vec
- FAISS
- Qdrant local

---

# 11. Elementos Sin Resolver

## API HTTP

Está previsto usar Drogon, pero aún no se implementó.

## Integración con llama.cpp

Se definió que inicialmente la comunicación será mediante HTTP con un servidor local de IA.

La integración directa como librería queda para una etapa posterior.

## Modelos

Queda pendiente automatizar:

- descarga
- ubicación
- actualización

mediante scripts.

---

# 12. Próximo Prompt de Activación de IA

Estás continuando el desarrollo del proyecto **Semantic FS**.

Tu rol es actuar como arquitecto y desarrollador senior de C++.

Contexto actual:

- El proyecto utiliza C++20.
- La arquitectura modular ya fue definida.
- El backend usa CMake, Make y vcpkg.
- El desarrollo debe mantenerse incremental.
- Los modelos IA son locales.
- La comunicación futura será mediante HTTP con servicios locales.

Tarea inmediata:

Trabajá únicamente sobre la siguiente etapa solicitada por el usuario, respetando la arquitectura existente, evitando sobreingeniería y proponiendo implementaciones escalables.

Usá este documento como contexto autorizado durante toda la interacción y no redefinas la arquitectura salvo que el usuario lo solicite explícitamente.

---

# Flujo de Bootstrap del Proyecto

El objetivo es que cualquier desarrollador pueda ejecutar únicamente:

```bash
git clone <repo>
cd backend
make setup
make run
```

## Responsabilidades

### Make

- Inicializar submódulos.
- Bootstrapping de vcpkg.
- Configurar CMake.
- Compilar el proyecto.
- Descargar modelos cuando corresponda.

### CMake

- Configurar el proyecto.
- Encontrar dependencias mediante vcpkg.
- Generar el sistema de compilación.
- Construir el ejecutable.

### vcpkg

- Instalar automáticamente las dependencias declaradas en `vcpkg.json`.

### Modelos IA

- Descargar mediante scripts.
- Nunca versionar archivos `.gguf`, `.bin` u otros pesos del modelo.

---

# Filosofía del Proyecto

Antes de agregar IA, OCR, Whisper o búsqueda vectorial, cada nueva funcionalidad debe dejar una base estable y comprobable.

Orden recomendado de implementación:

1. Sistema de build.
2. Scanner de archivos.
3. Lectura de `.txt`.
4. Limpieza de texto.
5. Chunking.
6. SQLite.
7. Búsqueda simple.
8. Embeddings.
9. Vector Search.
10. OCR.
11. Audio/Video.
12. Integración completa del motor semántico.
