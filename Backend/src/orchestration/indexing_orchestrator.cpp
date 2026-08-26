#include "semantic_fs/orchestration/indexing_orchestrator.h"

#include <utility>

namespace semantic_fs::orchestration {

IndexingOrchestrator::IndexingOrchestrator(
    semantic_fs::extractors::ExtractorFactory extractorFactory
)
    : extractorFactory_(std::move(extractorFactory))
{
}

bool IndexingOrchestrator::canIndex(const FileDocument& file) const
{
    return extractorFactory_.supports(file);
}

IndexingOrchestrator::ExtractionResult IndexingOrchestrator::extractFile(
    const FileDocument& file
) const
{
    auto extractor = extractorFactory_.create(file);
    return extractor->extract(file.path());
}

IndexingOrchestrator::FileDocument IndexingOrchestrator::indexFile(FileDocument file) const
{
    const auto extraction = extractFile(file);
    file.setFileType(extraction.fileType);

    for (const auto& segment : extraction.segments) {
        file.addContext(segment);
    }

    return file;
}

} // namespace semantic_fs::orchestration
