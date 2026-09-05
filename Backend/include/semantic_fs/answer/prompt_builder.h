#pragma once

#include "semantic_fs/answer/answer_types.h"

#include <string>
#include <vector>

namespace semantic_fs::answer {

class PromptBuilder {
public:
    PromptRequest build(
        const std::string& question,
        const std::vector<semantic_fs::rag::RetrievedChunk>& chunks,
        const std::vector<std::filesystem::path>& imagePaths,
        const AnswerOptions& options
    ) const;
};

} // namespace semantic_fs::answer
