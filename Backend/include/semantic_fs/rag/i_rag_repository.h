#pragma once

#include "semantic_fs/rag/rag_types.h"

#include <vector>

namespace semantic_fs::rag {

class IRagRepository {
public:
    virtual ~IRagRepository() = default;

    virtual void saveDocumentIndex(
        const semantic_fs::core::FileDocument& document,
        const std::vector<SanitizedSegment>& segments,
        const std::vector<ChunkRecord>& chunks,
        const std::vector<EmbeddingRecord>& embeddings
    ) = 0;
};

} // namespace semantic_fs::rag
