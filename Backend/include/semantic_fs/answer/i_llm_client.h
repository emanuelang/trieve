#pragma once

#include "semantic_fs/answer/answer_types.h"

namespace semantic_fs::answer {

class ILlmClient {
public:
    virtual ~ILlmClient() = default;

    virtual LlmResponse generate(const PromptRequest& request) const = 0;
};

} // namespace semantic_fs::answer
