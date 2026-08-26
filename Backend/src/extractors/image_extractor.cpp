#include "semantic_fs/extractors/image_extractor.h"

#include "semantic_fs/extractors/tesseract_ocr_extractor.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace semantic_fs::extractors {
namespace {

std::string normalizedExtension(const std::filesystem::path& filePath)
{
    auto extension = filePath.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension;
}

} // namespace

ExtractionResult ImageExtractor::extract(const std::filesystem::path& filePath) const
{
    ExtractionResult result;
    result.originalPath = filePath;
    result.fileType = FileType::Image;

    // Primera version funcional: una imagen genera contexto textual mediante OCR.
    // TesseractOcrExtractor etiqueta tambien el idioma detectado del texto.
    const TesseractOcrExtractor ocrExtractor("eng", "tessdata");
    const ContentInput input {
        .type = ContentType::Image,
        .path = filePath,
        .source = name()
    };
    result.segments.push_back(ocrExtractor.extract(input));

    return result;
}

bool ImageExtractor::supports(const std::filesystem::path& filePath) const
{
    static constexpr std::array supportedExtensions {
        ".png",
        ".jpg",
        ".jpeg",
        ".bmp",
        ".tif",
        ".tiff",
        ".webp"
    };

    const auto extension = normalizedExtension(filePath);
    return std::find(
        supportedExtensions.begin(),
        supportedExtensions.end(),
        extension
    ) != supportedExtensions.end();
}

std::string ImageExtractor::name() const
{
    return "ImageExtractor";
}

} // namespace semantic_fs::extractors
