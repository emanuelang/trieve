#pragma once

#include "semantic_fs/rag/rag_types.h"

#include <vector>

namespace semantic_fs::rag {

class Chunker {
public:
    Chunker() = default;
    explicit Chunker(ChunkingPolicy policy);

    std::vector<ChunkRecord> chunk(
        const semantic_fs::core::FileDocument& document,
        const std::vector<SanitizedSegment>& segments
    ) const;

private:
    ChunkingPolicy policy_;
};

} // namespace semantic_fs::rag
