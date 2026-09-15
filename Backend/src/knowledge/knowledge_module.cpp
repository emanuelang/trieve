#include "semantic_fs/knowledge/knowledge_module.h"

#include "semantic_fs/knowledge/in_memory_knowledge_repository.h"

#include <stdexcept>
#include <utility>

namespace semantic_fs::knowledge {

KnowledgeModule::KnowledgeModule()
    : repository_(std::make_shared<InMemoryKnowledgeRepository>())
{
}

KnowledgeModule::KnowledgeModule(std::shared_ptr<IKnowledgeRepository> repository)
    : repository_(std::move(repository))
{
    if (!repository_) {
        throw std::invalid_argument("KnowledgeModule requires a repository");
    }
}

KnowledgeIndexingResult KnowledgeModule::indexDocument(const semantic_fs::core::FileDocument& document) const
{
    return repository_->save(extractor_.extract(document));
}

std::vector<RetrievedKnowledge> KnowledgeModule::retrieveRelated(
    const std::string& query,
    std::size_t maxResults
) const
{
    return repository_->searchRelated(query, maxResults);
}

const IKnowledgeRepository& KnowledgeModule::repository() const
{
    return *repository_;
}

} // namespace semantic_fs::knowledge
