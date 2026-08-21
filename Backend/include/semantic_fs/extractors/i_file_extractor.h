#pragma once

#include "semantic_fs/extractors/extraction_types.h"

#include <filesystem>
#include <string>

namespace semantic_fs::extractors {

class IFileExtractor {
public:
    virtual ~IFileExtractor() = default;

    virtual ExtractionResult extract(const std::filesystem::path& filePath) const = 0;
    virtual bool supports(const std::filesystem::path& filePath) const = 0;
    virtual std::string name() const = 0;
};

} // namespace semantic_fs::extractors
