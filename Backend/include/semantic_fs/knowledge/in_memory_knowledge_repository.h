#pragma once

#include "semantic_fs/knowledge/i_knowledge_repository.h"

#include <unordered_map>

namespace semantic_fs::knowledge {

class InMemoryKnowledgeRepository final : public IKnowledgeRepository {
public:
    KnowledgeIndexingResult save(KnowledgeExtractionResult result) override;
    std::vector<RetrievedKnowledge> searchRelated(const std::string& query, std::size_t maxResults) const override;
    std::vector<KnowledgeNode> nodes() const override;
    std::vector<KnowledgeEdge> edges() const override;

private:
    std::unordered_map<std::string, KnowledgeNode> nodesById_;
    std::unordered_map<std::string, KnowledgeEdge> edgesById_;
};

} // namespace semantic_fs::knowledge
