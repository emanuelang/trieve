#pragma once

#include <string>

namespace semantic_fs::extractors {

class TesseractOcrExtractor {
public:
    explicit TesseractOcrExtractor(
        std::string language = "eng",
        std::string tessdataPath = ""
    );

    std::string extractText(const std::string& imagePath) const;

private:
    std::string language_;
    std::string tessdataPath_;
};

} // namespace semantic_fs::extractors
