#include "semantic_fs/extractors/tesseract_ocr_extractor.h"

#include "semantic_fs/language/cld3_language_detector.h"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
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

std::string sanitizeUtf8(const std::string& text)
{
    std::string clean;
    clean.reserve(text.size());

    for (std::size_t index = 0; index < text.size();) {
        const auto byte = static_cast<unsigned char>(text[index]);
        std::size_t sequenceLength = 0;

        if (byte <= 0x7F) {
            sequenceLength = 1;
        } else if ((byte & 0xE0) == 0xC0) {
            sequenceLength = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            sequenceLength = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            sequenceLength = 4;
        } else {
            ++index;
            continue;
        }

        if (index + sequenceLength > text.size()) {
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < sequenceLength; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }

        if (valid) {
            clean.append(text, index, sequenceLength);
            index += sequenceLength;
        } else {
            ++index;
        }
    }

    return clean;
}

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
    return extractText(imagePath, -1);
}

std::string TesseractOcrExtractor::extractText(
    const std::string& imagePath,
    int pageSegmentationMode
) const
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

    if (pageSegmentationMode >= 0) {
        api.SetPageSegMode(static_cast<tesseract::PageSegMode>(pageSegmentationMode));
    }

    // Evita que Tesseract ensucie la consola con warnings internos de layout
    // cuando analiza recortes chicos o con mucho ruido visual.
    api.SetVariable("debug_file", "NUL");

    // Entregamos la imagen cargada por Leptonica al motor OCR.
    api.SetImage(image.get());

    // GetUTF8Text ejecuta el reconocimiento y devuelve el texto detectado.
    std::unique_ptr<char, TextDestroyer> text(api.GetUTF8Text());
    if (!text) {
        return "";
    }

    // Copiamos el char* de Tesseract y limpiamos bytes invalidos para que CLD3,
    // fmt/spdlog y la consola no fallen con "invalid utf8".
    return sanitizeUtf8(text.get());
}

std::string TesseractOcrExtractor::extractText(const std::filesystem::path& imagePath) const
{
    // Sobrecarga de comodidad para poder pasar std::filesystem::path directamente.
    return extractText(imagePath.string());
}

std::string TesseractOcrExtractor::extractText(
    const std::filesystem::path& imagePath,
    int pageSegmentationMode
) const
{
    return extractText(imagePath.string(), pageSegmentationMode);
}

} // namespace semantic_fs::extractors
