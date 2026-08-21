#pragma once

#include "semantic_fs/extractors/i_file_extractor.h"

namespace semantic_fs::extractors {

class ImageExtractor final : public IFileExtractor {
public:
    ExtractionResult extract(const std::filesystem::path& filePath) const override;
    bool supports(const std::filesystem::path& filePath) const override;
    std::string name() const override;
};

} // namespace semantic_fs::extractors
