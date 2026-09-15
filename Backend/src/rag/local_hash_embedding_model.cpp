#include "semantic_fs/rag/local_hash_embedding_model.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace semantic_fs::rag {
namespace {

std::string normalizeToken(std::string token)
{
    token.erase(
        std::remove_if(
            token.begin(),
            token.end(),
            [](unsigned char value) {
                return std::isalnum(value) == 0;
            }
        ),
        token.end()
    );

    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    return token;
}

void l2Normalize(std::vector<float>& vector)
{
    double sum = 0.0;
    for (const auto value : vector) {
        sum += static_cast<double>(value) * static_cast<double>(value);
    }

    const auto norm = std::sqrt(sum);
    if (norm == 0.0) {
        return;
    }

    for (auto& value : vector) {
        value = static_cast<float>(static_cast<double>(value) / norm);
    }
}

} // namespace

LocalHashEmbeddingModel::LocalHashEmbeddingModel(std::size_t dimensions)
    : dimensions_(dimensions)
{
    if (dimensions_ == 0) {
        throw std::invalid_argument("Embedding dimensions must be greater than zero");
    }
}

std::vector<float> LocalHashEmbeddingModel::embed(const std::string& text) const
{
    std::vector<float> vector(dimensions_, 0.0F);
    std::hash<std::string> hasher;

    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        token = normalizeToken(token);
        if (token.empty()) {
            continue;
        }

        const auto hash = hasher(token);
        const auto index = hash % dimensions_;
        const auto sign = ((hash / dimensions_) % 2) == 0 ? 1.0F : -1.0F;
        vector[index] += sign;
    }

    l2Normalize(vector);
    return vector;
}

std::size_t LocalHashEmbeddingModel::dimensions() const
{
    return dimensions_;
}

std::string LocalHashEmbeddingModel::name() const
{
    return "LocalHashEmbeddingModel";
}

} // namespace semantic_fs::rag
