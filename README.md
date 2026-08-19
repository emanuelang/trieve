# Semantic FS - Backend

Backend en **C++20** para **Semantic FS**, un sistema de archivos semántico completamente local diseñado para extraer contenido de múltiples formatos (documentos, imágenes con OCR, audio, video) y habilitar búsquedas en lenguaje natural mediante embeddings locales.

Actualmente, el proyecto cuenta con la infraestructura base de compilación (`Make` + `CMake` + `vcpkg`) y la implementación del primer extractor funcional: **OCR sobre imágenes utilizando Tesseract**.

---

## 🛠️ Requisitos Previos

Antes de comenzar, asegúrate de tener instalado en tu sistema:
* **Compilador C++20:** Visual Studio 2022 (MSVC en Windows), GCC 10+ o Clang 11+.
* **CMake:** Versión 3.15 o superior.
* **GNU Make:** Para la ejecución de comandos simplificados del proyecto.
* **Git:** Para la gestión de dependencias y código fuente.

---

## 🚀 Guía de Instalación y Compilación Rápida

El proyecto utiliza `vcpkg` para gestionar librerías nativas de C++ y un `Makefile` que automatiza la descarga de dependencias y modelos.

### 1. Clonar o acceder a la carpeta del proyecto

Asegúrate de estar ubicado en la carpeta raíz del backend:

```bash
cd Backend
```

### 2. Configuración e Instalación (`make setup`)

Ejecuta el siguiente comando para preparar todo el entorno de desarrollo:

```bash
make setup
```

#### ¿Qué hace `make setup` automáticamente por ti?
* Clona/inicializa `vcpkg` en la ruta `external/vcpkg` si no existe.
* Descarga e instala las dependencias declaradas en `vcpkg.json` (Tesseract OCR, `fmt`, `spdlog`, `nlohmann-json`).
* Descarga el modelo de datos de idioma para Tesseract (`eng.traineddata`) y lo ubica en la carpeta `tessdata/`.
* Configura el proyecto con CMake apuntando al conjunto de herramientas de `vcpkg`.
* Compila el proyecto en modo Debug.

> **Nota:** La primera ejecución de `make setup` puede tomar unos minutos mientras `vcpkg` descarga y compila Tesseract y sus dependencias nativas.

---

## 💻 Ejecución y Pruebas

El punto de entrada ejecutable (`main.cpp`) está configurado en modo Debug/Prueba. Recibe como parámetro la ruta de una imagen, aplica el OCR y muestra por consola el texto detectado.

### Opción A: Ejecutar mediante Makefile (Recomendado)

```bash
make run
```

### Opción B: Ejecución directa del binario ejecutable

* **En Windows (PowerShell / CMD):**
  ```powershell
  .\build\Debug\semantic_fs_backend.exe "ruta\a\tu\imagen.png"
  ```

* **En Linux / macOS:**
  ```bash
  ./build/semantic_fs_backend "ruta/a/tu/imagen.png"
  ```

### Ejemplo de Salida Esperada:
Si ejecutas el programa sobre una imagen con texto (por ejemplo, una imagen que contenga `HELLO OCR 123`), el resultado en consola será:

```text
HELLO OCR 123
```

---

## 🛠️ Comandos de Compilación Útiles

* **Compilar cambios (Build rápido):**
  ```bash
  make build
  ```

* **Limpiar archivos generados por la compilación:**
  ```bash
  make clean
  ```

---

## 💡 Cómo Usar la Clase OCR en Código C++

Para integrar el módulo OCR en cualquier otra parte del sistema:

```cpp
#include "semantic_fs/extractors/tesseract_ocr_extractor.h"
#include <iostream>

int main() {
    // Instancia el extractor pasando el idioma y el directorio de modelos
    semantic_fs::extractors::TesseractOcrExtractor extractor("eng", "tessdata");

    // Procesa una imagen y obtiene el texto
    std::string texto_extraido = extractor.extractText("ruta/a/imagen.png");

    std::cout << "Texto Detectado:\n" << texto_extraido << std::endl;
    return 0;
}
```

---

## ⚠️ Notas Importantes y Solución de Problemas

* **Git Repository Warning:** Si la carpeta aún no ha sido inicializada como un repositorio Git (`git init`), es normal que comandos como `git status` muestren el mensaje *"not a git repository"*. El Makefile está diseñado para funcionar de todas formas.
* **Modelos de Idioma OCR:** Tesseract requiere archivos `.traineddata` en runtime para funcionar. Si requieres reconocer texto en español, asegúrate de descargar `spa.traineddata` e incluirlo dentro de la carpeta `tessdata/`.
