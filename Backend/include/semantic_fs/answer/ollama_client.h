#pragma once

#include "semantic_fs/answer/i_llm_client.h"

#include <string>

namespace semantic_fs::answer {

struct OllamaClientOptions {
    std::string baseUrl = "http://localhost:11434";
    std::string modelName = "qwen2.5vl:3b";
    int timeoutSeconds = 300;
    int numContext = 1024;
    int numPredict = 64;
    double temperature = 0.2;
    std::string keepAlive = "0";
    bool autoStart = true;
    int startupWaitSeconds = 30;
};

class OllamaClient final : public ILlmClient {
public:
    OllamaClient();
    explicit OllamaClient(OllamaClientOptions options);

    LlmResponse generate(const PromptRequest& request) const override;

private:
    OllamaClientOptions options_;
};

} // namespace semantic_fs::answer
