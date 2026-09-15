#include "semantic_fs/answer/fallback_llm_client.h"

#include <sstream>

namespace semantic_fs::answer {

LlmResponse FallbackLlmClient::generate(const PromptRequest& request) const
{
    std::ostringstream answer;

    if (request.chunks.empty()) {
        answer << "No encontre contexto suficiente en el indice para responder esa pregunta.";
    } else {
        answer << "Respuesta basada en el contexto recuperado:\n";
        const auto& best = request.chunks.front().chunk;
        answer << best.text;

        if (request.chunks.size() > 1) {
            answer << "\n\nTambien encontre contexto relacionado en ";
            answer << request.chunks.size() - 1 << " chunk(s) adicional(es).";
        }
    }

    return {
        .text = answer.str(),
        .modelName = "FallbackLlmClient",
        .usedFallback = true
    };
}

} // namespace semantic_fs::answer
