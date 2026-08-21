#include "semantic_fs/extractors/extractor_factory.h"

#include "semantic_fs/extractors/image_extractor.h"

#include <stdexcept>

namespace semantic_fs::extractors {

std::unique_ptr<IFileExtractor> ExtractorFactory::create(
    const std::filesystem::path& filePath
) const
{
    auto imageExtractor = std::make_unique<ImageExtractor>();
    if (imageExtractor->supports(filePath)) {
        return imageExtractor;
    }

    throw std::runtime_error("No file extractor available for: " + filePath.string());
}

} // namespace semantic_fs::extractors
