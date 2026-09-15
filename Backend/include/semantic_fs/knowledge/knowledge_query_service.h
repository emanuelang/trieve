#pragma once

#include "semantic_fs/knowledge/knowledge_module.h"
#include "semantic_fs/knowledge/knowledge_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace semantic_fs::knowledge {

class KnowledgeQueryService {
public:
    explicit KnowledgeQueryService(const KnowledgeModule& knowledgeModule);

    std::vector<RetrievedKnowledge> retrieveRelated(const std::string& query, std::size_t maxResults) const;

private:
    const KnowledgeModule& knowledgeModule_;
};

} // namespace semantic_fs::knowledge
