#pragma once

#include "semantic_fs/core/file_document.h"
#include "semantic_fs/extractors/i_file_extractor.h"

#include <memory>

namespace semantic_fs::extractors {

class ExtractorFactory {
public:
    std::unique_ptr<IFileExtractor> create(const std::filesystem::path& filePath) const;
    std::unique_ptr<IFileExtractor> create(const semantic_fs::core::FileDocument& file) const;

    bool supports(const std::filesystem::path& filePath) const;
    bool supports(const semantic_fs::core::FileDocument& file) const;
};

} // namespace semantic_fs::extractors
