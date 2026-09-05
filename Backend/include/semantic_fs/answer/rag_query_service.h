#pragma once

#include "semantic_fs/rag/rag_module.h"
#include "semantic_fs/rag/rag_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace semantic_fs::answer {

class RagQueryService {
public:
    explicit RagQueryService(const semantic_fs::rag::RagModule& ragModule);

    std::vector<semantic_fs::rag::RetrievedChunk> retrieve(
        const std::string& question,
        std::size_t topK
    ) const;

private:
    const semantic_fs::rag::RagModule& ragModule_;
};

} // namespace semantic_fs::answer
