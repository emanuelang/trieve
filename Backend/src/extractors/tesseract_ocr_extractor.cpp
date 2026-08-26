#include "semantic_fs/extractors/tesseract_ocr_extractor.h"

#include "semantic_fs/language/cld3_language_detector.h"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

namespace semantic_fs::extractors {
namespace {

// Leptonica devuelve Pix* con memoria propia. Este deleter permite guardarlo
// dentro de unique_ptr para que pixDestroy se llame automaticamente.
struct PixDestroyer {
    void operator()(Pix* image) const
    {
        if (image != nullptr) {
            pixDestroy(&image);
        }
    }
};

// Tesseract devuelve el texto OCR como char*. Este deleter evita fugas de memoria
// cuando el unique_ptr sale de scope.
struct TextDestroyer {
    void operator()(char* text) const
    {
        delete[] text;
    }
};

} // namespace

TesseractOcrExtractor::TesseractOcrExtractor(
    std::string language,
    std::string tessdataPath,
    std::shared_ptr<semantic_fs::language::ILanguageDetector> languageDetector
)
    // Se mueven los strings para guardar la configuracion sin copias extra.
    : language_(std::move(language)),
      tessdataPath_(std::move(tessdataPath)),
      languageDetector_(
          languageDetector
              ? std::move(languageDetector)
              : std::make_shared<semantic_fs::language::Cld3LanguageDetector>()
      )
{
}

ExtractedSegment TesseractOcrExtractor::extract(const ContentInput& input) const
{
    // La interfaz IContentExtractor es generica, por eso validamos que esta entrada
    // sea realmente una imagen antes de intentar pasarla a Tesseract.
    if (!supports(input)) {
        throw std::runtime_error("Tesseract OCR does not support this content input");
    }

    // Convertimos la imagen a texto y empaquetamos el resultado como segmento.
    // page/timestamp se conservan para futuros casos: PDF escaneado o video.
    auto segment = ExtractedSegment {
        .text = extractText(input.path),
        .source = name(),
        .contentType = ContentType::Text,
        .page = input.page,
        .timestamp = input.timestamp
    };

    if (languageDetector_) {
        const auto language = languageDetector_->detect(segment.text);
        segment.detectedLanguage = language.languageCode;
        segment.languageConfidence = language.confidence;
    }

    return segment;
}

bool TesseractOcrExtractor::supports(const ContentInput& input) const
{
    // Para OCR alcanza con dos condiciones minimas:
    // 1. la entrada representa una imagen;
    // 2. hay una ruta desde donde Leptonica pueda leer pixeles.
    return input.type == ContentType::Image && !input.path.empty();
}

std::string TesseractOcrExtractor::name() const
{
    return "TesseractOcrExtractor";
}

std::string TesseractOcrExtractor::extractText(const std::string& imagePath) const
{
    // pixRead pertenece a Leptonica. Lee PNG/JPG/TIFF/etc. y devuelve una imagen
    // en memoria que Tesseract puede procesar.
    std::unique_ptr<Pix, PixDestroyer> image(pixRead(imagePath.c_str()));
    if (!image) {
        throw std::runtime_error("Could not read image: " + imagePath);
    }

    // TessBaseAPI es el objeto principal de Tesseract. Mantiene el motor OCR
    // configurado con idioma, datos de entrenamiento e imagen actual.
    tesseract::TessBaseAPI api;

    // Si tessdataPath_ esta vacio, Tesseract busca los modelos en sus rutas
    // por defecto. En este proyecto usamos "tessdata" dentro de Backend.
    const char* dataPath = tessdataPath_.empty() ? nullptr : tessdataPath_.c_str();

    // Init carga el modelo de idioma, por ejemplo tessdata/eng.traineddata.
    // Si falla, normalmente falta el archivo .traineddata o la ruta es incorrecta.
    if (api.Init(dataPath, language_.c_str()) != 0) {
        throw std::runtime_error(
            "Could not initialize Tesseract. Check tessdata path and language: " + language_
        );
    }

    // Entregamos la imagen cargada por Leptonica al motor OCR.
    api.SetImage(image.get());

    // GetUTF8Text ejecuta el reconocimiento y devuelve el texto detectado.
    std::unique_ptr<char, TextDestroyer> text(api.GetUTF8Text());
    if (!text) {
        return "";
    }

    // Copiamos el char* de Tesseract a std::string antes de liberar memoria.
    return std::string(text.get());
}

std::string TesseractOcrExtractor::extractText(const std::filesystem::path& imagePath) const
{
    // Sobrecarga de comodidad para poder pasar std::filesystem::path directamente.
    return extractText(imagePath.string());
}

} // namespace semantic_fs::extractors
