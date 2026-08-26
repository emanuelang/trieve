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

std::unique_ptr<IFileExtractor> ExtractorFactory::create(
    const semantic_fs::core::FileDocument& file
) const
{
    return create(file.path());
}

bool ExtractorFactory::supports(const std::filesystem::path& filePath) const
{
    const ImageExtractor imageExtractor;
    return imageExtractor.supports(filePath);
}

bool ExtractorFactory::supports(const semantic_fs::core::FileDocument& file) const
{
    return supports(file.path());
}

} // namespace semantic_fs::extractors
