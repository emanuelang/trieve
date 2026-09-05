#pragma once

#include "semantic_fs/rag/rag_types.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::answer {

struct AnswerOptions {
    std::size_t topK = 5;
    std::size_t maxContextChunks = 2;
    std::size_t maxContextCharacters = 2500;
    bool includeSources = true;
    bool debugEnabled = true;
    bool attachImagesToLlm = true;
    bool singleBestSource = false;
};

struct AnswerRequest {
    std::string question;
    std::vector<std::filesystem::path> imagePaths;
    AnswerOptions options;
};

struct PromptRequest {
    std::string prompt;
    std::vector<semantic_fs::rag::RetrievedChunk> chunks;
    std::vector<std::filesystem::path> imagePaths;
};

struct LlmResponse {
    std::string text;
    std::string modelName;
    bool usedFallback = false;
};

struct AnswerSource {
    std::string chunkId;
    std::string fileName;
    std::string filePath;
    std::string source;
    double score = 0.0;
    int page = -1;
};

struct AnswerDebugInfo {
    std::size_t retrievedChunks = 0;
    std::size_t usedChunks = 0;
    std::size_t promptCharacters = 0;
    std::string modelName;
    bool usedFallback = false;
    long long latencyMs = 0;
};

struct AnswerResult {
    std::string answer;
    std::vector<AnswerSource> sources;
    AnswerDebugInfo debug;
};

} // namespace semantic_fs::answer
