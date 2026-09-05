#include "semantic_fs/answer/answer_module.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace semantic_fs::answer {
namespace {

std::string pathToUtf8String(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

AnswerSource toSource(const semantic_fs::rag::RetrievedChunk& retrieved)
{
    const auto& chunk = retrieved.chunk;
    return {
        .chunkId = chunk.id,
        .fileName = chunk.fileName,
        .filePath = pathToUtf8String(chunk.filePath),
        .source = chunk.source,
        .score = retrieved.score,
        .page = chunk.page
    };
}

AnswerSource imageToSource(const std::filesystem::path& imagePath, std::size_t index)
{
    return {
        .chunkId = "image:" + std::to_string(index + 1),
        .fileName = pathToUtf8String(imagePath.filename()),
        .filePath = pathToUtf8String(imagePath),
        .source = "UserImage",
        .score = 1.0F,
        .page = -1
    };
}

std::string appendImagePaths(std::string answer, const std::vector<std::filesystem::path>& imagePaths)
{
    if (imagePaths.empty()) {
        return answer;
    }

    std::ostringstream output;
    output << answer;
    if (!answer.empty() && answer.back() != '\n') {
        output << "\n";
    }

    output << "\nDirecciones de imagen:\n";
    for (std::size_t index = 0; index < imagePaths.size(); ++index) {
        output << "- imagen " << index + 1 << ": " << pathToUtf8String(imagePaths[index]) << "\n";
    }
    return output.str();
}

std::vector<std::filesystem::path> selectRelevantImagePaths(
    const std::vector<std::filesystem::path>& candidateImagePaths,
    const std::vector<semantic_fs::rag::RetrievedChunk>& chunks
)
{
    if (candidateImagePaths.empty() || chunks.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> selected;
    std::unordered_set<std::string> selectedTexts;

    for (const auto& retrieved : chunks) {
        const auto chunkPathText = pathToUtf8String(std::filesystem::absolute(retrieved.chunk.filePath));
        const auto found = std::find_if(
            candidateImagePaths.begin(),
            candidateImagePaths.end(),
            [&](const std::filesystem::path& candidate) {
                return pathToUtf8String(std::filesystem::absolute(candidate)) == chunkPathText;
            }
        );

        if (found == candidateImagePaths.end()) {
            continue;
        }

        const auto selectedText = pathToUtf8String(std::filesystem::absolute(*found));
        if (selectedTexts.insert(selectedText).second) {
            selected.push_back(*found);
        }
    }

    return selected;
}

} // namespace

AnswerModule::AnswerModule(
    RagQueryService ragQueryService,
    ContextRanker contextRanker,
    PromptBuilder promptBuilder,
    std::shared_ptr<ILlmClient> llmClient
)
    : ragQueryService_(std::move(ragQueryService)),
      contextRanker_(std::move(contextRanker)),
      promptBuilder_(std::move(promptBuilder)),
      llmClient_(std::move(llmClient))
{
    if (!llmClient_) {
        throw std::invalid_argument("AnswerModule requires an LLM client");
    }
}

AnswerResult AnswerModule::answer(const AnswerRequest& request) const
{
    const auto start = std::chrono::steady_clock::now();

    const auto retrieved = ragQueryService_.retrieve(request.question, request.options.topK);
    const auto limited = contextRanker_.rankAndLimit(
        request.question,
        retrieved,
        request.options.maxContextChunks,
        request.options.maxContextCharacters,
        request.options.singleBestSource
    );
    const auto relevantImagePaths = selectRelevantImagePaths(request.imagePaths, limited);
    const auto attachedImagePaths = request.options.attachImagesToLlm
        ? relevantImagePaths
        : std::vector<std::filesystem::path> {};
    const auto prompt = promptBuilder_.build(
        request.question,
        limited,
        attachedImagePaths,
        request.options
    );
    const auto llmResponse = llmClient_->generate(prompt);

    std::vector<AnswerSource> sources;
    if (request.options.includeSources) {
        sources.reserve(limited.size() + relevantImagePaths.size());
        for (const auto& chunk : limited) {
            sources.push_back(toSource(chunk));
        }
        for (std::size_t index = 0; index < relevantImagePaths.size(); ++index) {
            sources.push_back(imageToSource(relevantImagePaths[index], index));
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    );

    return {
        .answer = appendImagePaths(llmResponse.text, relevantImagePaths),
        .sources = sources,
        .debug = {
            .retrievedChunks = retrieved.size(),
            .usedChunks = limited.size(),
            .promptCharacters = prompt.prompt.size(),
            .modelName = llmResponse.modelName,
            .usedFallback = llmResponse.usedFallback,
            .latencyMs = elapsed.count()
        }
    };
}

} // namespace semantic_fs::answer
