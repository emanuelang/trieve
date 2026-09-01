#pragma once

#include "semantic_fs/rag/i_rag_repository.h"

#include <vector>

namespace semantic_fs::rag {

class InMemoryRagRepository final : public IRagRepository {
public:
    void saveDocumentIndex(
        const semantic_fs::core::FileDocument& document,
        const std::vector<SanitizedSegment>& segments,
        const std::vector<ChunkRecord>& chunks,
        const std::vector<EmbeddingRecord>& embeddings
    ) override;

    const std::vector<ChunkRecord>& chunks() const;
    const std::vector<EmbeddingRecord>& embeddings() const;

private:
    std::vector<ChunkRecord> chunks_;
    std::vector<EmbeddingRecord> embeddings_;
};

} // namespace semantic_fs::rag
