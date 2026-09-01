#include "semantic_fs/rag/embedding_service.h"

#include <stdexcept>
#include <utility>

namespace semantic_fs::rag {

EmbeddingService::EmbeddingService(std::shared_ptr<IEmbeddingModel> model)
    : model_(std::move(model))
{
    if (!model_) {
        throw std::invalid_argument("EmbeddingService requires a model");
    }
}

std::vector<EmbeddingRecord> EmbeddingService::embedChunks(
    const std::vector<ChunkRecord>& chunks
) const
{
    std::vector<EmbeddingRecord> embeddings;
    embeddings.reserve(chunks.size());

    for (const auto& chunk : chunks) {
        embeddings.push_back({
            .chunkId = chunk.id,
            .vector = model_->embed(chunk.text)
        });
    }

    return embeddings;
}

const IEmbeddingModel& EmbeddingService::model() const
{
    return *model_;
}

} // namespace semantic_fs::rag
