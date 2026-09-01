#pragma once

#include <string>
#include <vector>

namespace semantic_fs::rag {

class IEmbeddingModel {
public:
    virtual ~IEmbeddingModel() = default;

    virtual std::vector<float> embed(const std::string& text) const = 0;
    virtual std::size_t dimensions() const = 0;
    virtual std::string name() const = 0;
};

} // namespace semantic_fs::rag
