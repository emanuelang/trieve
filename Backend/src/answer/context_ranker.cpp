#include "semantic_fs/answer/context_ranker.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace semantic_fs::answer {
namespace {

std::string lowerAscii(std::string text)
{
    for (auto& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::vector<std::string> tokenizeQuery(const std::string& question)
{
    std::vector<std::string> tokens;
    std::string current;

    for (const auto character : lowerAscii(question)) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            current.push_back(character);
            continue;
        }

        if (current.size() >= 4) {
            tokens.push_back(current);
        }
        current.clear();
    }

    if (current.size() >= 4) {
        tokens.push_back(current);
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

double lexicalBoost(const semantic_fs::rag::RetrievedChunk& chunk, const std::vector<std::string>& queryTokens)
{
    if (queryTokens.empty()) {
        return 0.0;
    }

    const auto text = lowerAscii(chunk.chunk.text + " " + chunk.chunk.fileName + " " + chunk.chunk.source);
    double boost = 0.0;
    for (const auto& token : queryTokens) {
        if (text.find(token) != std::string::npos) {
            boost += 0.18;
        }
    }
    return std::min(boost, 0.9);
}

std::string absolutePathText(const std::filesystem::path& path)
{
    const auto text = std::filesystem::absolute(path).u8string();
    return std::string(text.begin(), text.end());
}

std::string bestSourcePath(const std::vector<semantic_fs::rag::RetrievedChunk>& chunks)
{
    std::unordered_map<std::string, double> sourceScores;
    for (const auto& chunk : chunks) {
        sourceScores[absolutePathText(chunk.chunk.filePath)] += chunk.score;
    }

    auto best = std::max_element(sourceScores.begin(), sourceScores.end(), [](const auto& left, const auto& right) {
        return left.second < right.second;
    });

    return best == sourceScores.end() ? std::string {} : best->first;
}

} // namespace

std::vector<semantic_fs::rag::RetrievedChunk> ContextRanker::rankAndLimit(
    const std::string& question,
    std::vector<semantic_fs::rag::RetrievedChunk> chunks,
    std::size_t maxChunks,
    std::size_t maxCharacters,
    bool singleBestSource
) const
{
    if (maxChunks == 0 || maxCharacters == 0) {
        return {};
    }

    const auto queryTokens = tokenizeQuery(question);
    for (auto& chunk : chunks) {
        chunk.score += lexicalBoost(chunk, queryTokens);
    }

    std::sort(chunks.begin(), chunks.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });

    const auto selectedSourcePath = singleBestSource ? bestSourcePath(chunks) : std::string {};

    std::vector<semantic_fs::rag::RetrievedChunk> limited;
    limited.reserve(std::min(maxChunks, chunks.size()));

    std::unordered_set<std::string> usedTexts;
    std::size_t usedCharacters = 0;

    for (const auto& chunk : chunks) {
        if (limited.size() >= maxChunks) {
            break;
        }

        if (!selectedSourcePath.empty() && absolutePathText(chunk.chunk.filePath) != selectedSourcePath) {
            continue;
        }

        if (chunk.chunk.text.empty() || usedTexts.contains(chunk.chunk.text)) {
            continue;
        }

        const auto nextSize = usedCharacters + chunk.chunk.text.size();
        if (!limited.empty() && nextSize > maxCharacters) {
            continue;
        }

        usedCharacters = nextSize;
        usedTexts.insert(chunk.chunk.text);
        limited.push_back(chunk);
    }

    return limited;
}

} // namespace semantic_fs::answer
