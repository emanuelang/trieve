#include "semantic_fs/rag/rag_indexing_pipeline.h"

#include "semantic_fs/core/file_document.h"

#include <stdexcept>
#include <utility>

namespace semantic_fs::rag {

RagIndexingPipeline::RagIndexingPipeline(
    ContextSanitizer sanitizer,
    Chunker chunker,
    EmbeddingService embeddingService,
    std::shared_ptr<IRagRepository> repository
)
    : sanitizer_(std::move(sanitizer)),
      chunker_(std::move(chunker)),
      embeddingService_(std::move(embeddingService)),
      repository_(std::move(repository))
{
    if (!repository_) {
        throw std::invalid_argument("RagIndexingPipeline requires a repository");
    }
}

RagIndexingResult RagIndexingPipeline::index(const semantic_fs::core::FileDocument& document) const
{
    const auto& rawSegments = document.contexts();
    const auto sanitized = sanitizer_.sanitize(rawSegments);
    const auto chunks = chunker_.chunk(document, sanitized);
    const auto embeddings = embeddingService_.embedChunks(chunks);

    repository_->saveDocumentIndex(document, sanitized, chunks, embeddings);

    return {
        .rawSegmentCount = rawSegments.size(),
        .sanitizedSegmentCount = sanitized.size(),
        .discardedSegmentCount = rawSegments.size() - sanitized.size(),
        .chunkCount = chunks.size(),
        .embeddingCount = embeddings.size()
    };
}

} // namespace semantic_fs::rag
