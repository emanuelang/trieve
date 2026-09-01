#include "semantic_fs/rag/rag_module.h"

#include "semantic_fs/rag/in_memory_rag_repository.h"
#include "semantic_fs/rag/local_hash_embedding_model.h"

#include <memory>
#include <utility>

namespace semantic_fs::rag {

namespace {

RagIndexingPipeline createDefaultPipeline()
{
    return RagIndexingPipeline(
        ContextSanitizer {},
        Chunker {},
        EmbeddingService(std::make_shared<LocalHashEmbeddingModel>()),
        std::make_shared<InMemoryRagRepository>()
    );
}

} // namespace

RagModule::RagModule()
    : indexingPipeline_(createDefaultPipeline())
{
}

RagModule::RagModule(RagIndexingPipeline indexingPipeline)
    : indexingPipeline_(std::move(indexingPipeline))
{
}

RagIndexingResult RagModule::indexDocument(const semantic_fs::core::FileDocument& document) const
{
    return indexingPipeline_.index(document);
}

} // namespace semantic_fs::rag
