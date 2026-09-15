#pragma once

#include "semantic_fs/rag/rag_indexing_pipeline.h"
#include "semantic_fs/rag/rag_types.h"

namespace semantic_fs::rag {

class RagModule {
public:
    RagModule();
    explicit RagModule(RagIndexingPipeline indexingPipeline);

    RagIndexingResult indexDocument(const semantic_fs::core::FileDocument& document) const;
    std::vector<RetrievedChunk> retrieve(const std::string& question, std::size_t topK) const;

private:
    RagIndexingPipeline indexingPipeline_;
};

} // namespace semantic_fs::rag
