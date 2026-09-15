#include "semantic_fs/answer/rag_query_service.h"

namespace semantic_fs::answer {

RagQueryService::RagQueryService(const semantic_fs::rag::RagModule& ragModule)
    : ragModule_(ragModule)
{
}

std::vector<semantic_fs::rag::RetrievedChunk> RagQueryService::retrieve(
    const std::string& question,
    std::size_t topK
) const
{
    return ragModule_.retrieve(question, topK);
}

} // namespace semantic_fs::answer
