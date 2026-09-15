#pragma once

#include "semantic_fs/rag/rag_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace semantic_fs::answer {

class ContextRanker {
public:
    std::vector<semantic_fs::rag::RetrievedChunk> rankAndLimit(
        const std::string& question,
        std::vector<semantic_fs::rag::RetrievedChunk> chunks,
        std::size_t maxChunks,
        std::size_t maxCharacters,
        bool singleBestSource
    ) const;
};

} // namespace semantic_fs::answer
