#include "semantic_fs/knowledge/knowledge_query_service.h"

#include <utility>

namespace semantic_fs::knowledge {

KnowledgeQueryService::KnowledgeQueryService(const KnowledgeModule& knowledgeModule)
    : knowledgeModule_(knowledgeModule)
{
}

std::vector<RetrievedKnowledge> KnowledgeQueryService::retrieveRelated(
    const std::string& query,
    std::size_t maxResults
) const
{
    return knowledgeModule_.retrieveRelated(query, maxResults);
}

} // namespace semantic_fs::knowledge
