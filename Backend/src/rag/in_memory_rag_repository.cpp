#include "semantic_fs/rag/in_memory_rag_repository.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace semantic_fs::rag {
namespace {

double cosineSimilarity(const std::vector<float>& left, const std::vector<float>& right)
{
    const auto size = std::min(left.size(), right.size());
    if (size == 0) {
        return 0.0;
    }

    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t index = 0; index < size; ++index) {
        dot += static_cast<double>(left[index]) * static_cast<double>(right[index]);
        leftNorm += static_cast<double>(left[index]) * static_cast<double>(left[index]);
        rightNorm += static_cast<double>(right[index]) * static_cast<double>(right[index]);
    }

    if (leftNorm == 0.0 || rightNorm == 0.0) {
        return 0.0;
    }

    return dot / (std::sqrt(leftNorm) * std::sqrt(rightNorm));
}

} // namespace

void InMemoryRagRepository::saveDocumentIndex(
    const semantic_fs::core::FileDocument&,
    const std::vector<SanitizedSegment>&,
    const std::vector<ChunkRecord>& chunks,
    const std::vector<EmbeddingRecord>& embeddings
)
{
    chunks_.insert(chunks_.end(), chunks.begin(), chunks.end());
    embeddings_.insert(embeddings_.end(), embeddings.begin(), embeddings.end());
}

std::vector<RetrievedChunk> InMemoryRagRepository::searchSimilar(
    const std::vector<float>& queryEmbedding,
    std::size_t topK
) const
{
    if (topK == 0 || queryEmbedding.empty()) {
        return {};
    }

    std::unordered_map<std::string, const ChunkRecord*> chunksById;
    chunksById.reserve(chunks_.size());
    for (const auto& chunk : chunks_) {
        chunksById.emplace(chunk.id, &chunk);
    }

    std::vector<RetrievedChunk> results;
    results.reserve(std::min(topK, embeddings_.size()));

    for (const auto& embedding : embeddings_) {
        const auto chunk = chunksById.find(embedding.chunkId);
        if (chunk == chunksById.end()) {
            continue;
        }

        results.push_back({
            .chunk = *chunk->second,
            .score = cosineSimilarity(queryEmbedding, embedding.vector)
        });
    }

    std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });

    if (results.size() > topK) {
        results.resize(topK);
    }

    return results;
}

const std::vector<ChunkRecord>& InMemoryRagRepository::chunks() const
{
    return chunks_;
}

const std::vector<EmbeddingRecord>& InMemoryRagRepository::embeddings() const
{
    return embeddings_;
}

} // namespace semantic_fs::rag
