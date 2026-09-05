#include "semantic_fs/orchestration/indexing_orchestrator.h"

#include <utility>

namespace semantic_fs::orchestration {

IndexingOrchestrator::IndexingOrchestrator(
    semantic_fs::extractors::ExtractorFactory extractorFactory
)
    : extractorFactory_(std::move(extractorFactory))
{
}

IndexingOrchestrator::IndexingOrchestrator(
    semantic_fs::extractors::ExtractorFactory extractorFactory,
    semantic_fs::rag::RagModule ragModule
)
    : extractorFactory_(std::move(extractorFactory)),
      ragModule_(std::move(ragModule))
{
}

IndexingOrchestrator::FileDocument IndexingOrchestrator::createDocument(
    const std::filesystem::path& filePath
) const
{
    FileDocument document(filePath);
    document.refreshMetadataFromDisk();
    return document;
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

    file.setRagIndexingResult(ragModule_.indexDocument(file));

    return file;
}

IndexingOrchestrator::FileDocument IndexingOrchestrator::indexPath(
    const std::filesystem::path& filePath
) const
{
    return indexFile(createDocument(filePath));
}

const semantic_fs::rag::RagModule& IndexingOrchestrator::ragModule() const
{
    return ragModule_;
}

} // namespace semantic_fs::orchestration
