#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::extractors {

enum class FileType {
    Unknown,
    Text,
    Image,
    Pdf,
    Audio,
    Video
};

enum class ContentType {
    Unknown,
    Text,
    Image,
    Audio,
    VideoFrame
};

// Entrada generica para servicios internos de extraccion.
// En OCR, lo importante es que type sea Image y path apunte a la imagen a leer.
struct ContentInput {
    ContentType type = ContentType::Unknown;
    std::filesystem::path path;
    std::string source;

    // Metadata opcional para contenido compuesto. Por ejemplo, una imagen
    // extraida de la pagina 3 de un PDF o de un frame de video.
    int page = -1;
    double timestamp = -1.0;
};

// Fragmento de contenido ya convertido a texto o metadata extraida.
// El OCR devuelve uno de estos segmentos con contentType = Text.
struct ExtractedSegment {
    std::string text;
    std::string source;
    ContentType contentType = ContentType::Unknown;
    std::string detectedLanguage = "unknown";
    double languageConfidence = 0.0;

    // Se copia desde ContentInput para no perder el origen del segmento.
    int page = -1;
    double timestamp = -1.0;
};

struct ExtractionResult {
    std::filesystem::path originalPath;
    FileType fileType = FileType::Unknown;
    std::vector<ExtractedSegment> segments;
};

} // namespace semantic_fs::extractors
