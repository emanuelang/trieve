#pragma once

#include "semantic_fs/extractors/tesseract_ocr_extractor.h"
#include "semantic_fs/ocr/image_preprocessor.h"
#include "semantic_fs/ocr/text_region_detector.h"

#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::ocr {

enum class OcrSearchMode {
    Fast,
    Balanced,
    Exhaustive
};

struct OcrAttemptResult {
    std::string text;
    std::filesystem::path imagePath;
    std::string regionLabel;
    std::string variantName;
    int pageSegmentationMode = -1;
    double confidence = 0.0;
};

class OcrAttemptRunner {
public:
    explicit OcrAttemptRunner(OcrSearchMode mode = OcrSearchMode::Fast);

    OcrAttemptResult runBestAttempt(const std::filesystem::path& imagePath) const;
    std::vector<OcrAttemptResult> runReadableAttempts(const std::filesystem::path& imagePath) const;

private:
    double scoreText(
        const std::string& text,
        const TextRegion& region,
        const std::string& variantName
    ) const;

    TextRegionDetector regionDetector_;
    ImagePreprocessor preprocessor_;
    semantic_fs::extractors::TesseractOcrExtractor ocrExtractor_;
    OcrSearchMode mode_;
};

} // namespace semantic_fs::ocr
