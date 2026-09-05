#pragma once

#include "semantic_fs/answer/answer_types.h"
#include "semantic_fs/answer/context_ranker.h"
#include "semantic_fs/answer/i_llm_client.h"
#include "semantic_fs/answer/prompt_builder.h"
#include "semantic_fs/answer/rag_query_service.h"

#include <memory>

namespace semantic_fs::answer {

class AnswerModule {
public:
    AnswerModule(
        RagQueryService ragQueryService,
        ContextRanker contextRanker,
        PromptBuilder promptBuilder,
        std::shared_ptr<ILlmClient> llmClient
    );

    AnswerResult answer(const AnswerRequest& request) const;

private:
    RagQueryService ragQueryService_;
    ContextRanker contextRanker_;
    PromptBuilder promptBuilder_;
    std::shared_ptr<ILlmClient> llmClient_;
};

} // namespace semantic_fs::answer
