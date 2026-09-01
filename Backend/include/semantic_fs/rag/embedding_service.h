#pragma once

#include "semantic_fs/rag/i_embedding_model.h"
#include "semantic_fs/rag/rag_types.h"

#include <memory>
#include <vector>

namespace semantic_fs::rag {

class EmbeddingService {
public:
    explicit EmbeddingService(std::shared_ptr<IEmbeddingModel> model);

    std::vector<EmbeddingRecord> embedChunks(const std::vector<ChunkRecord>& chunks) const;
    const IEmbeddingModel& model() const;

private:
    std::shared_ptr<IEmbeddingModel> model_;
};

} // namespace semantic_fs::rag
