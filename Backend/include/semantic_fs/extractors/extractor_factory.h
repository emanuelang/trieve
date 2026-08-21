#pragma once

#include "semantic_fs/extractors/i_file_extractor.h"

#include <memory>

namespace semantic_fs::extractors {

class ExtractorFactory {
public:
    std::unique_ptr<IFileExtractor> create(const std::filesystem::path& filePath) const;
};

} // namespace semantic_fs::extractors
