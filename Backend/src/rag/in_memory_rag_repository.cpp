#include "semantic_fs/rag/in_memory_rag_repository.h"

namespace semantic_fs::rag {

void InMemoryRagRepository::saveDocumentIndex(
    const semantic_fs::core::FileDocument&,
    const std::vector<SanitizedSegment>&,
    const std::vector<ChunkRecord>& chunks,
    const std::vector<EmbeddingRecord>& embeddings
)
{
    chunks_.insert(chunks_.end(), chunks.begin(), chunks.end());
    embeddings_.insert(embeddings_.end(), embeddings.begin(), embeddings.end());
}

const std::vector<ChunkRecord>& InMemoryRagRepository::chunks() const
{
    return chunks_;
}

const std::vector<EmbeddingRecord>& InMemoryRagRepository::embeddings() const
{
    return embeddings_;
}

} // namespace semantic_fs::rag
