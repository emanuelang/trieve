# 🏗️ Arquitectura y Estructura del Proyecto – Semantic FS

Este documento detalla la estructura global de carpetas del proyecto **Semantic FS**, la responsabilidad única de cada componente y la hoja de ruta de crecimiento del repositorio.

El diseño del proyecto sigue una arquitectura **modular y desacoplada**, garantizando que a medida que el sistema crezca (incorporando OCR, transcripción de audio, bases de datos vectoriales e interfaz gráfica), cada elemento mantenga un lugar claro y aislado sin generar deuda técnica ni desorden.

---

## 📂 Vista General del Repositorio

```text
Semantic-FS/
├── agents/            # Contexto e instrucciones para Agentes de IA
├── Backend/           # Corazón del proyecto (Motor C++20)
├── data/              # Base de datos, índices vectoriales, caché y logs
├── docs/              # Documentación técnica, roadmap y diagramas
├── Frontend/          # Interfaz gráfica de usuario
├── models/            # Modelos locales de IA (Embeddings, Whisper, OCR)
├── scripts/           # Automatización de tareas (Setup, Descargas, Limpieza)
├── .gitignore         # Exclusión de archivos binarios, pesados o temporales
└── README.md          # Puerta de entrada y guía rápida del proyecto
```

---

## 🧩 Desglose Detallado por Carpeta

### 🤖 1. `agents/`

Guarda el contexto, las convenciones y los límites operativos para cualquier agente de Inteligencia Artificial que colabore en el desarrollo del repositorio.

```text
agents/
├── AGENTS.md          # Contexto autorizado y reglas principales del proyecto
├── architecture.md    # Definición de capas y decisiones de diseño
├── coding-rules.md    # Estándares de código C++ / Markdown / Formato
└── prompts/           # Prompts de activación y plantillas reutilizables
```

* **Qué va acá:** Instrucciones de arquitectura, convenciones de codificación, contexto del sistema y prompts de trabajo.
* **Qué NO va acá:** Código fuente del sistema, archivos binarios, ejecutables o modelos.

---

### ⚙️ 2. `Backend/`

Es el **corazón y motor del proyecto**. Todo el procesamiento pesado, análisis de archivos e inferencias locales ocurren en esta carpeta.

```text
Backend/
├── CMakeLists.txt     # Script de configuración de CMake
├── Makefile           # Automatización de comandos (setup, build, run)
├── vcpkg.json         # Declaración de dependencias nativas C++
├── external/          # Submódulos o dependencias externas (ej. vcpkg)
├── include/           # Encabezados públicos C++ (.h / .hpp)
├── src/               # Implementación de código fuente (.cpp)
├── tests/             # Pruebas unitarias e integración
└── build/             # Archivos generados por la compilación (no se sube a Git)
```

#### Responsabilidades del Backend:
* **Escanear** el sistema de archivos local.
* **Extraer** texto de diversos formatos (archivos planos, OCR de imágenes, transcripción de audio/video).
* **Limpiar y normalizar** el contenido extraído.
* **Generar chunks** (fragmentos de texto) para procesamiento.
* **Generar embeddings** vectoriales locales.
* **Búsqueda y recuperación semántica** de archivos.

#### Carpetas internas de `Backend/`:
| Carpeta | Función Principal |
| :--- | :--- |
| **`include/`** | Definiciones de clases e interfaces públicas agrupadas por módulo (`scanner`, `extractors`, `processing`, `chunking`, `embeddings`, `storage`, `search`). |
| **`src/`** | Implementación de las clases (`.cpp`) y punto de entrada (`src/app/main.cpp`). |
| **`external/`** | Herramientas integradas como submódulos Git (por ejemplo, `external/vcpkg`). |
| **`tests/`** | Test suites para validación de componentes. |
| **`build/`** | Directorio de salida de binarios y compilación. |

---

### 🗄️ 3. `data/`

Guarda todos los datos estructurados y semiestructurados generados por el sistema en tiempo de ejecución. **No contiene código fuente**.

```text
data/
├── semantic_fs.db     # Base de datos relacional SQLite
├── vector_index/      # Índices vectoriales persistidos
├── cache/             # Archivos procesados temporalmente
└── logs/              # Archivos de registro del sistema
```

* **`semantic_fs.db` (SQLite):** Almacena metadatos de los archivos indexados, rutas, hashes de verificación, texto extraído y fragmentos (*chunks*).
* **`vector_index/`:** Almacena los índices de búsqueda vectorial eficiente (ej. `sqlite-vec`, FAISS o HNSW local).
* **`cache/`:** Almacenamiento temporal para acelerar el re-procesamiento.
* **`logs/`:** Traza de ejecución y depuración de errores.

---

### 📚 4. `docs/`

Centraliza la documentación técnica del proyecto para que cualquier desarrollador pueda entender la arquitectura y empezar a colaborar rápidamente.

```text
docs/
├── architecture.md    # Explicación técnica de la arquitectura interna
├── roadmap.md         # Fases de desarrollo y objetivos futuros
├── setup.md           # Guía detallada de entorno y dependencias
└── diagrams/          # Diagramas conceptuales y de arquitectura (ASCII / Imágenes)
```

---

### 💻 5. `Frontend/`

Contiene la interfaz gráfica de usuario. Mantiene una separación estricta: **nunca procesa ni indexa archivos directamente**, sino que consume los servicios expuestos por el `Backend`.

```text
Frontend/
├── src/               # Componentes visuales y lógica de la UI
├── public/            # Recursos estáticos (iconos, imágenes)
├── package.json       # Configuración del entorno de Node/JavaScript
└── vite.config.ts     # Configuración del empaquetador frontend
```

#### Responsabilidades:
* Permitir al usuario realizar búsquedas en lenguaje natural.
* Mostrar resultados visuales ordenados por relevancia semántica.
* Ofrecer paneles de configuración de carpetas a indexar e historial.

---

### 🧠 6. `models/`

Una de las carpetas clave del sistema. Almacena los **pesos de los modelos de IA que se ejecutan 100% de forma local**.

```text
models/
├── embeddings/        # Modelos para generación de vectores (ej. bge-small.gguf)
├── whisper/           # Modelos para transcripción de audio (ej. ggml-base.bin)
├── ocr/               # Modelos/datos de entrenamiento OCR (ej. eng.traineddata)
└── cache/             # Descargas parciales o caché de inferencia
```

> ⚠️ **RESTRICCIÓN CRÍTICA DE GIT:**
> Esta carpeta contiene archivos binarios pesados (de cientos de MB a varios GB). **Nunca se deben subir a Git**. Su descarga se automatiza mediante los scripts del proyecto.

---

### 🛠️ 7. `scripts/`

Contiene las herramientas y scripts de automatización para tareas repetitivas de desarrollo y configuración.

```text
scripts/
├── download_models.ps1 / .sh   # Automatiza la descarga de modelos de IA a /models
├── setup_backend.ps1 / .sh     # Configuración inicial de vcpkg y entorno
└── clean.ps1 / .sh             # Limpieza rápida de /build, caché y archivos temporales
```

---

### 🙈 8. `.gitignore`

Define los patrones de archivos que Git debe ignorar para mantener el repositorio liviano y libre de binarios.

**Ejemplos de exclusión:**
```gitignore
# Compilación y VS
Backend/build/
Backend/.vs/
*.exe
*.obj
*.pdb

# Modelos y datos pesados
models/
data/cache/
data/logs/
data/*.db
```

---

### 📖 9. `README.md`

Es la puerta de entrada al repositorio. Responde de forma concisa a tres preguntas clave:
1. **¿Qué es Semantic FS?** (Definición del proyecto).
2. **¿Cómo se instala y ejecuta?** (Comandos `make setup` / `make run`).
3. **¿Cómo está organizado?** (Resumen ejecutivo de la arquitectura).

---

## 🔄 Flujo Completo del Sistema

El procesamiento de información sigue una cadena de responsabilidades unidireccional:

```text
[ Documento / Imagen / Audio ]
              │
              ▼
    1. Backend/src/scanner     ──────► Detecta cambios en el disco
              │
              ▼
   2. Backend/src/extractors   ──────► Usa models/ (OCR / Whisper) para extraer texto
              │
              ▼
   3. Backend/src/processing   ──────► Limpia y normaliza el texto
              │
              ▼
    4. Backend/src/chunking    ──────► Divide el texto en bloques
              │
              ▼
   5. Backend/src/embeddings   ──────► Usa models/embeddings para vectorizar
              │
              ▼
     6. Backend/src/storage    ──────► Guarda metadatos e índices en data/
              │
              ▼
          7. Frontend          ──────► Consulta el Backend y muestra resultados
```

---

## 🚀 Filosofía de Crecimiento Incremental

La arquitectura está diseñada para evolucionar por etapas sin necesidad de reestructurar el repositorio:

1. **Etapa Inicial (Actual):** Trabajo enfocado en `Backend/`, `scripts/`, `docs/` y `agents/`.
2. **Próxima Etapa:** Incorporación del modelo relacional en `data/` (SQLite).
3. **Etapa de Vectores:** Integración de modelos locales en `models/` (Embeddings) e índice vectorial en `data/vector_index/`.
4. **Etapa de Interfaz:** Desarrollo del `Frontend/`.
5. **Versión Avanzada:** Integración total de componentes operando de forma asíncrona.
