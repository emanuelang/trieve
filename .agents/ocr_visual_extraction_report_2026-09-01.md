# Informe: OCR y descripcion visual de imagenes

Fecha: 2026-09-01

## Objetivo

Mejorar el modulo de extraccion de imagenes para que:

- Extraiga texto visible con OCR de forma mas robusta.
- No falle por texto o rutas con caracteres invalidos para UTF-8.
- Genere contextos visuales utiles cuando una imagen no tiene texto.
- Mantenga una arquitectura liviana, sin agregar todavia modelos pesados como CLIP, SigLIP, BLIP o Florence.

## Problemas detectados

1. El OCR fallaba o devolvia texto muy pobre en imagenes con texto chico, captions o placas informativas.
2. Algunas salidas rompian el programa con `invalid utf8`.
3. Capturas con rutas que tenian acentos, como `Imagenes/Imágenes`, podian romper la salida por consola.
4. Imagenes sin texto generaban OCR falso, por ejemplo fragmentos como `5` o `Ss ron Nee`.
5. El descriptor visual era demasiado generico: devolvia conceptos como `image`, `captioned_image` o `contains text`.
6. El atajo visual para paisajes quedo demasiado agresivo y a veces saltaba OCR en imagenes que si tenian texto.

## Cambios realizados

### 1. OCR con multiples intentos controlados

Archivos principales:

- `Backend/include/semantic_fs/ocr/ocr_attempt_runner.h`
- `Backend/src/ocr/ocr_attempt_runner.cpp`

Se agrego:

```text
enum class OcrSearchMode
  Fast
  Balanced
  Exhaustive
```

Se agrego:

```text
OcrAttemptRunner::runReadableAttempts(path)
```

Funcion:

- Ejecuta OCR sobre varias regiones utiles.
- Selecciona fragmentos legibles.
- Evita quedarse solamente con un unico recorte.
- Tiene limites de intentos para no hacer el proceso demasiado lento.

Tambien se agrego un perfil interno:

```text
OcrAttemptProfile
  preferredRegionLabels
  variantNames
  pageSegmentationModes
  earlyStopConfidence
  readableTextConfidence
  readableTextCharacters
  maxAttempts
```

Uso:

- Define que regiones probar.
- Define que preprocesados usar.
- Define que modos PSM de Tesseract probar.
- Define cuando cortar temprano.

### 2. Preprocesamiento selectivo

Archivos principales:

- `Backend/include/semantic_fs/ocr/image_preprocessor.h`
- `Backend/src/ocr/image_preprocessor.cpp`

Se agrego:

```text
ImagePreprocessor::preprocessSelectedVariants(imagePath, region, variantNames)
```

Funcion:

- Genera solo las variantes de imagen que el OCR va a usar.
- Evita crear imagenes temporales innecesarias.
- Reduce costo de CPU y disco.

Variantes soportadas:

```text
scaled_3x
grayscale_scaled_3x
binary_threshold_190
binary_threshold_190_inverted
binary_threshold_140
binary_threshold_140_inverted
sharpened_grayscale
```

Tambien se agrego escalado adaptativo:

```text
scaleFactorFor(region)
```

Funcion:

- Usa mas escala en recortes chicos.
- Usa menos escala en screenshots o regiones grandes.
- Evita mandar imagenes gigantes a Tesseract.

### 3. Nuevas regiones OCR

Archivo principal:

- `Backend/src/ocr/text_region_detector.cpp`

Se agrego:

```text
lower_caption_area
```

Funcion:

- Detectar texto ubicado en la parte inferior.
- Sirve para placas informativas, noticias, memes o imagenes con epigrafes.

Regiones actuales:

```text
upper_caption_area
social_caption_band
center_text_area
lower_caption_area
full_image
```

### 4. Limpieza UTF-8

Archivos principales:

- `Backend/src/extractors/tesseract_ocr_extractor.cpp`
- `Backend/src/app/main.cpp`

Se agrego:

```text
sanitizeUtf8(text)
```

Funcion:

- Elimina bytes invalidos.
- Evita errores `invalid utf8`.
- Protege texto que viene desde Tesseract.
- Protege texto mostrado por consola.

En `main.cpp` tambien se agrego:

```text
pathToUtf8String(path)
utf8Preview(text, maxBytes)
```

Funcion:

- Imprimir rutas de Windows con caracteres especiales.
- Evitar cortar un caracter UTF-8 por la mitad al mostrar previews.

### 5. Filtro contra OCR falso

Archivo principal:

- `Backend/src/extractors/image_extractor.cpp`

Se agrego:

```text
hasMeaningfulOcrText(text)
```

Funcion:

- Verifica si el texto OCR tiene suficientes letras y palabras reales.
- Evita guardar ruido OCR en fotos sin texto.

Regla actual:

```text
letters >= 16
meaningfulWords >= 3
ocrConfidence >= 0.12
```

### 6. Descriptor visual liviano

Archivo principal:

- `Backend/src/vision/heuristic_visual_descriptor.cpp`

Se agregaron senales visuales:

```text
warmRatio
brightWarmRatio
blueRatio
darkRatio
whiteRatio
blackRatio
lightNeutralRatio
greenRatio
topWarmRatio
middleDarkRatio
lowerDarkRatio
aspectRatio
```

Funcion:

- Leer colores generales de la imagen.
- Detectar si parece paisaje, atardecer, montana o placa con texto.
- No requiere modelos IA pesados.

Se agregaron detectores:

```text
looksLikeSunset()
looksLikeMountainLandscape()
looksLikeNaturalLandscape()
looksLikeTextGraphic()
```

### 7. Conceptos visuales nuevos

El descriptor ahora puede producir conceptos como:

```text
sunset_landscape
atardecer
sunset
paisaje
cielo naranja
sol
naturaleza
mountain_landscape
montana
montaña
mountain
pico de montana
nubes
niebla
cielo calido
natural_landscape
outdoor
escena natural
possible_text_graphic
placa informativa
grafica con texto
economic_news_graphic
noticia economica
ventas minoristas
comercio
tendencia negativa
grafico de linea
estadisticas por anio
```

## Flujo actual

```text
ImageExtractor
  |
  |-- HeuristicVisualDescriptor.describe(path, "")
  |     |
  |     |-- analiza metadata/nombre
  |     |-- analiza colores de la imagen
  |     |-- decide visualType preliminar
  |
  |-- si visualType es paisaje claro:
  |     |
  |     |-- salta OCR
  |     |-- guarda VisualDescriptionSegment
  |     |-- guarda VisualConceptSegment
  |
  |-- si puede haber texto:
        |
        |-- OcrAttemptRunner.runReadableAttempts(path)
        |     |
        |     |-- TextRegionDetector.detect(path)
        |     |-- ImagePreprocessor.preprocessSelectedVariants(...)
        |     |-- TesseractOcrExtractor.extractText(...)
        |     |-- sanitiza UTF-8
        |     |-- selecciona fragmentos legibles
        |
        |-- ImageExtractor une fragmentos OCR
        |-- detecta idioma con CLD3
        |-- guarda OCRTextSegment si el texto es confiable
        |-- HeuristicVisualDescriptor.describe(path, ocrText)
        |-- guarda VisualDescriptionSegment
        |-- guarda VisualConceptSegment
```

## Relacion entre clases para UML

```text
ImageExtractor
  uses OcrAttemptRunner
  uses HeuristicVisualDescriptor
  uses Cld3LanguageDetector
  creates ExtractionResult
  creates ExtractedSegment

OcrAttemptRunner
  has TextRegionDetector
  has ImagePreprocessor
  has TesseractOcrExtractor
  creates OcrAttemptResult
  uses OcrSearchMode

TextRegionDetector
  creates TextRegion

ImagePreprocessor
  creates PreprocessedImage
  uses TextRegion

TesseractOcrExtractor
  uses Tesseract
  uses Leptonica
  creates ExtractedSegment

HeuristicVisualDescriptor
  implements IVisualDescriptor
  creates VisualDescriptionResult
  uses image path
  uses OCR text
  uses lightweight color signals

IndexingOrchestrator
  uses ExtractorFactory
  creates FileDocument
  calls ImageExtractor through extractor interface
  stores ExtractedSegment objects inside FileDocument.contexts_
```

## Pruebas realizadas

### Imagen de atardecer

Archivo:

```text
C:\Users\54343\Downloads\sunset-8331285_1280.jpg
```

Resultado:

```text
Paisaje de atardecer con colores calidos.
Conceptos:
sunset_landscape, atardecer, sunset, paisaje, cielo naranja, sol, naturaleza
```

Tiempo aproximado:

```text
0.91s
```

### Paisaje de montana

Archivo:

```text
C:\Users\54343\Downloads\images (5).jpg
```

Resultado:

```text
Paisaje de montana con cielo calido y nubes.
Conceptos:
mountain_landscape, paisaje, montana, montaña, mountain,
pico de montana, nubes, niebla, cielo calido, naturaleza, outdoor, escena natural
```

Tiempo aproximado:

```text
0.49s
```

### Placa economica con texto

Archivo:

```text
C:\Users\54343\Downloads\images (4).jpg
```

Resultado OCR:

```text
retroceso del -1,7% en 2025
drastica caida del -10,2% en 2024
la baja del -1,2% reportada en 2023
economia
Las ventas minoristas por el Dia del Padre...
```

Resultado visual:

```text
Placa informativa de economia.
Conceptos:
economic_news_graphic, contains text, posible texto visible,
placa informativa, grafica con texto, noticia economica,
ventas minoristas, comercio, tendencia negativa,
grafico de linea, estadisticas por anio
```

Tiempo aproximado:

```text
10.49s
```

## Limitaciones actuales

1. El descriptor visual sigue siendo heuristico.
2. No entiende cualquier imagen del mundo como lo haria CLIP/SigLIP.
3. Para nuevos dominios hay que agregar reglas o palabras clave.
4. El OCR en espanol no sera perfecto hasta agregar `spa.traineddata`.
5. Imagenes con texto muy chico, borroso o muy comprimido pueden seguir generando errores parciales.

## Recomendacion tecnica

Para la primera version liviana:

```text
Mantener OCR + HeuristicVisualDescriptor
Agregar paquetes de idioma de Tesseract bajo demanda
Seguir ampliando reglas por dominios comunes
```

Para una version mas general:

```text
Agregar CLIP o SigLIP como modulo opcional
Usarlo para embeddings visuales y busqueda semantica de imagenes
No hacerlo obligatorio para no aumentar demasiado el peso del producto base
```

