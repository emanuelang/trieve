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

std::string normalizeSpanishText(const std::string& text)
{
    std::string normalized;
    normalized.reserve(text.size());

    for (std::size_t index = 0; index < text.size();) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte < 0x80) {
            normalized.push_back(static_cast<char>(std::tolower(byte)));
            ++index;
            continue;
        }

        if (index + 1 >= text.size()) {
            ++index;
            continue;
        }

        const auto next = static_cast<unsigned char>(text[index + 1]);
        if (byte == 0xC3) {
            switch (next) {
            case 0xA1:
            case 0x81:
                normalized.push_back('a');
                break;
            case 0xA9:
            case 0x89:
                normalized.push_back('e');
                break;
            case 0xAD:
            case 0x8D:
                normalized.push_back('i');
                break;
            case 0xB3:
            case 0x93:
                normalized.push_back('o');
                break;
            case 0xBA:
            case 0x9A:
            case 0xBC:
            case 0x9C:
                normalized.push_back('u');
                break;
            case 0xB1:
            case 0x91:
                normalized.push_back('n');
                break;
            default:
                normalized.push_back(' ');
                break;
            }
            index += 2;
            continue;
        }

        normalized.push_back(' ');
        ++index;
    }

    return normalized;
}

bool isStopword(const std::string& token)
{
    static const std::unordered_set<std::string> stopwords {
        "archivo",
        "archivos",
        "cual",
        "cuales",
        "dentro",
        "direccion",
        "documento",
        "documentos",
        "empresa",
        "esta",
        "estan",
        "imagen",
        "imagenes",
        "para",
        "sobre",
        "trata"
    };

    return stopwords.contains(token);
}

std::vector<std::string> tokenizeQuery(const std::string& question)
{
    std::vector<std::string> tokens;
    std::string current;

    for (const auto character : normalizeSpanishText(question)) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            current.push_back(character);
            continue;
        }

        if (current.size() >= 4 && !isStopword(current)) {
            tokens.push_back(current);
        }
        current.clear();
    }

    if (current.size() >= 4 && !isStopword(current)) {
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

    const auto text = normalizeSpanishText(chunk.chunk.text + " " + chunk.chunk.fileName + " " + chunk.chunk.source);
    double boost = 0.0;
    std::size_t matches = 0;
    for (const auto& token : queryTokens) {
        if (text.find(token) != std::string::npos) {
            ++matches;
            boost += 0.42;
        }
    }

    if (matches >= 2) {
        boost += 0.5;
    }

    return std::min(boost, 1.8);
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
