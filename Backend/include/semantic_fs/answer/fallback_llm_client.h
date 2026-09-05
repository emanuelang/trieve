#pragma once

#include "semantic_fs/answer/i_llm_client.h"

namespace semantic_fs::answer {

class FallbackLlmClient final : public ILlmClient {
public:
    LlmResponse generate(const PromptRequest& request) const override;
};

} // namespace semantic_fs::answer
