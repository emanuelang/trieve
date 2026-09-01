#pragma once

#include "semantic_fs/rag/i_embedding_model.h"

namespace semantic_fs::rag {

class LocalHashEmbeddingModel final : public IEmbeddingModel {
public:
    explicit LocalHashEmbeddingModel(std::size_t dimensions = 64);

    std::vector<float> embed(const std::string& text) const override;
    std::size_t dimensions() const override;
    std::string name() const override;

private:
    std::size_t dimensions_;
};

} // namespace semantic_fs::rag
