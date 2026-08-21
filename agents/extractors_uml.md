# Extractors UML

Este diagrama separa dos ramas:

- `IFileExtractor`: extractores de archivos completos.
- `IContentExtractor`: funcionalidades reutilizables para extraer contenido interno.

```mermaid
classDiagram
    class ExtractionResult {
        +path originalPath
        +FileType fileType
        +vector~ExtractedSegment~ segments
    }

    class ExtractedSegment {
        +string text
        +string source
        +ContentType contentType
        +int page
        +double timestamp
    }

    class ContentInput {
        +ContentType type
        +path path
        +string source
        +int page
        +double timestamp
    }

    class IFileExtractor {
        <<interface>>
        +extract(path filePath) ExtractionResult
        +supports(path filePath) bool
        +name() string
    }

    class IContentExtractor {
        <<interface>>
        +extract(ContentInput input) ExtractedSegment
        +supports(ContentInput input) bool
        +name() string
    }

    class ImageExtractor {
        +extract(path filePath) ExtractionResult
        +supports(path filePath) bool
        +name() string
    }

    class TesseractOcrExtractor {
        +extract(ContentInput input) ExtractedSegment
        +supports(ContentInput input) bool
        +name() string
        +extractText(path imagePath) string
    }

    class ExtractorFactory {
        +create(path filePath) unique_ptr~IFileExtractor~
    }

    IFileExtractor <|.. ImageExtractor
    IContentExtractor <|.. TesseractOcrExtractor
    ExtractorFactory ..> IFileExtractor : creates
    TesseractOcrExtractor ..> ContentInput : reads
    TesseractOcrExtractor ..> ExtractedSegment : returns
    ImageExtractor ..> ExtractionResult : returns
```

## Flujo actual

```text
archivo de imagen
    -> ExtractorFactory
    -> ImageExtractor
    -> ExtractionResult vacio, sin OCR conectado todavia

imagen para debug OCR
    -> IContentExtractor
    -> TesseractOcrExtractor
    -> ExtractedSegment con texto
```

## Decision de diseno

`ImageExtractor` todavia no usa `TesseractOcrExtractor`. Esto deja separada la parte de reconocer y clasificar figuras/contenido visual antes de conectar OCR como servicio interno.
