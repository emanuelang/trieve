#include "semantic_fs/extractors/tesseract_ocr_extractor.h"

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <exception>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        spdlog::info("Usage: semantic_fs_backend <image_path>");
        return 0;
    }

    try {
        const semantic_fs::extractors::TesseractOcrExtractor extractor("eng", "tessdata");
        const std::string text = extractor.extractText(argv[1]);

        fmt::print("{}\n", text);
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("OCR failed: {}", error.what());
        return 1;
    }
}
