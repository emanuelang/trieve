#pragma once

#include "semantic_fs/core/file_document.h"
#include "semantic_fs/knowledge/i_knowledge_repository.h"
#include "semantic_fs/knowledge/rule_based_knowledge_extractor.h"

#include <memory>

namespace semantic_fs::knowledge {

class KnowledgeModule {
public:
    KnowledgeModule();
    explicit KnowledgeModule(std::shared_ptr<IKnowledgeRepository> repository);

    KnowledgeIndexingResult indexDocument(const semantic_fs::core::FileDocument& document) const;
    std::vector<RetrievedKnowledge> retrieveRelated(const std::string& query, std::size_t maxResults) const;
    const IKnowledgeRepository& repository() const;

private:
    RuleBasedKnowledgeExtractor extractor_;
    std::shared_ptr<IKnowledgeRepository> repository_;
};

} // namespace semantic_fs::knowledge
