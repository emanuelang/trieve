#pragma once

#include "semantic_fs/answer/answer_types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::answer {

class PromptBuilder {
public:
    PromptRequest build(
        const std::string& question,
        const std::vector<semantic_fs::rag::RetrievedChunk>& chunks,
        const std::vector<semantic_fs::knowledge::RetrievedKnowledge>& knowledge,
        const std::vector<std::filesystem::path>& imagePaths,
        const AnswerOptions& options
    ) const;
};

} // namespace semantic_fs::answer
