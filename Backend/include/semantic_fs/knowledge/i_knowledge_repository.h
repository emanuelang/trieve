#pragma once

#include "semantic_fs/knowledge/knowledge_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace semantic_fs::knowledge {

class IKnowledgeRepository {
public:
    virtual ~IKnowledgeRepository() = default;

    virtual KnowledgeIndexingResult save(KnowledgeExtractionResult result) = 0;
    virtual std::vector<RetrievedKnowledge> searchRelated(const std::string& query, std::size_t maxResults) const = 0;
    virtual std::vector<KnowledgeNode> nodes() const = 0;
    virtual std::vector<KnowledgeEdge> edges() const = 0;
};

} // namespace semantic_fs::knowledge
