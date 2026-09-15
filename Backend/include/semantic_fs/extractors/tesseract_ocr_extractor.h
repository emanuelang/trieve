#pragma once

#include "semantic_fs/extractors/i_content_extractor.h"
#include "semantic_fs/language/i_language_detector.h"

#include <filesystem>
#include <memory>
#include <string>

namespace semantic_fs::extractors {

class TesseractOcrExtractor final : public IContentExtractor {
public:
    // language indica que modelo de idioma usa Tesseract, por ejemplo "eng",
    // "spa" o "eng+spa". tessdataPath apunta a la carpeta con archivos .traineddata.
    explicit TesseractOcrExtractor(
        std::string language = "eng",
        std::string tessdataPath = "",
        std::shared_ptr<semantic_fs::language::ILanguageDetector> languageDetector = nullptr
    );

    // Implementacion polimorfica: recibe ContentInput y devuelve un segmento de texto.
    ExtractedSegment extract(const ContentInput& input) const override;

    // Este OCR solo acepta entradas de tipo Image con una ruta no vacia.
    bool supports(const ContentInput& input) const override;
    std::string name() const override;

    // API directa para pruebas o uso interno: recibe una ruta y devuelve solo texto.
    std::string extractText(const std::string& imagePath) const;
    std::string extractText(const std::string& imagePath, int pageSegmentationMode) const;
    std::string extractText(const std::filesystem::path& imagePath) const;
    std::string extractText(const std::filesystem::path& imagePath, int pageSegmentationMode) const;

private:
    // Configuracion que se pasa a TessBaseAPI::Init en cada extraccion.
    std::string language_;
    std::string tessdataPath_;
    std::shared_ptr<semantic_fs::language::ILanguageDetector> languageDetector_;
};

} // namespace semantic_fs::extractors
