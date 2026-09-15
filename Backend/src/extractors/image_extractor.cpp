#include "semantic_fs/extractors/image_extractor.h"

#include "semantic_fs/language/cld3_language_detector.h"
#include "semantic_fs/ocr/ocr_attempt_runner.h"
#include "semantic_fs/vision/heuristic_visual_descriptor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

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

std::string joinConcepts(const std::vector<std::string>& concepts)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < concepts.size(); ++index) {
        if (index > 0) {
            output << ", ";
        }
        output << concepts[index];
    }
    return output.str();
}

std::string joinOcrFragments(const std::vector<semantic_fs::ocr::OcrAttemptResult>& fragments)
{
    std::ostringstream output;
    for (const auto& fragment : fragments) {
        if (fragment.text.empty()) {
            continue;
        }
        if (output.tellp() > 0) {
            output << "\n";
        }
        output << fragment.text;
    }
    return output.str();
}

double averageConfidence(const std::vector<semantic_fs::ocr::OcrAttemptResult>& fragments)
{
    if (fragments.empty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const auto& fragment : fragments) {
        total += fragment.confidence;
    }
    return total / static_cast<double>(fragments.size());
}

bool hasMeaningfulOcrText(const std::string& text)
{
    int letters = 0;
    int meaningfulWords = 0;

    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        const auto alphaCount = std::count_if(word.begin(), word.end(), [](unsigned char value) {
            return std::isalpha(value) != 0;
        });

        letters += static_cast<int>(alphaCount);
        if (alphaCount >= 3) {
            ++meaningfulWords;
        }
    }

    return letters >= 16 && meaningfulWords >= 3;
}

} // namespace

ExtractionResult ImageExtractor::extract(const std::filesystem::path& filePath) const
{
    ExtractionResult result;
    result.originalPath = filePath;
    result.fileType = FileType::Image;

    const semantic_fs::vision::HeuristicVisualDescriptor visualDescriptor;
    auto visual = visualDescriptor.describe(filePath, "");
    const bool visualOnlyIsEnough = visual.visualType == "sunset_landscape"
        || visual.visualType == "mountain_landscape"
        || visual.visualType == "natural_landscape";

    // OCR mejorado: prueba regiones preprocesadas y varios PSM de Tesseract.
    std::string ocrText;
    double ocrConfidence = 0.0;
    if (!visualOnlyIsEnough) {
        const semantic_fs::ocr::OcrAttemptRunner ocrRunner;
        const auto ocrResults = ocrRunner.runReadableAttempts(filePath);
        ocrText = joinOcrFragments(ocrResults);
        ocrConfidence = averageConfidence(ocrResults);
    }

    const bool hasReliableOcr = !ocrText.empty()
        && ocrConfidence >= 0.12
        && hasMeaningfulOcrText(ocrText);

    ExtractedSegment ocrSegment {
        .text = ocrText,
        .source = "OcrAttemptRunner:readable_regions",
        .contentType = ContentType::Text,
        .extractionConfidence = ocrConfidence
    };

    const semantic_fs::language::Cld3LanguageDetector languageDetector;
    const auto language = languageDetector.detect(ocrSegment.text);
    ocrSegment.detectedLanguage = language.languageCode;
    ocrSegment.languageConfidence = language.confidence;
    if (hasReliableOcr) {
        result.segments.push_back(ocrSegment);
    }

    // Descriptor visual liviano: genera contexto visual usando OCR + metadata.
    if (hasReliableOcr) {
        visual = visualDescriptor.describe(filePath, ocrSegment.text);
    }

    result.segments.push_back({
        .text = visual.summary,
        .source = "HeuristicVisualDescriptor",
        .contentType = ContentType::VisualDescription,
        .detectedLanguage = hasReliableOcr ? ocrSegment.detectedLanguage : "es",
        .languageConfidence = hasReliableOcr ? ocrSegment.languageConfidence : 0.90,
        .extractionConfidence = visual.confidence
    });

    result.segments.push_back({
        .text = joinConcepts(visual.concepts),
        .source = "HeuristicVisualDescriptor",
        .contentType = ContentType::VisualConcepts,
        .detectedLanguage = "en",
        .languageConfidence = 1.0,
        .extractionConfidence = visual.confidence
    });

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
