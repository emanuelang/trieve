#include "semantic_fs/rag/chunker.h"

#include "semantic_fs/core/file_document.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace semantic_fs::rag {
namespace {

std::vector<std::string> splitWords(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::string joinWords(
    const std::vector<std::string>& words,
    std::size_t begin,
    std::size_t end
)
{
    std::ostringstream output;
    for (std::size_t index = begin; index < end; ++index) {
        if (index > begin) {
            output << ' ';
        }
        output << words[index];
    }
    return output.str();
}

std::string makeChunkId(
    const std::filesystem::path& path,
    std::size_t segmentIndex,
    std::size_t chunkIndex
)
{
    return path.string() + "#" + std::to_string(segmentIndex) + ":" + std::to_string(chunkIndex);
}

} // namespace

Chunker::Chunker(ChunkingPolicy policy)
    : policy_(policy)
{
}

std::vector<ChunkRecord> Chunker::chunk(
    const semantic_fs::core::FileDocument& document,
    const std::vector<SanitizedSegment>& segments
) const
{
    std::vector<ChunkRecord> chunks;
    const auto step = policy_.targetWords > policy_.overlapWords
        ? policy_.targetWords - policy_.overlapWords
        : policy_.targetWords;

    for (std::size_t segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
        const auto& segment = segments[segmentIndex];
        const auto words = splitWords(segment.text);
        if (words.size() < policy_.minChunkWords) {
            continue;
        }

        std::size_t chunkIndex = 0;
        for (std::size_t begin = 0; begin < words.size(); begin += step) {
            const auto end = std::min(begin + policy_.targetWords, words.size());
            if (end - begin < policy_.minChunkWords) {
                break;
            }

            chunks.push_back({
                .id = makeChunkId(document.path(), segmentIndex, chunkIndex),
                .filePath = document.path(),
                .fileName = document.fileName(),
                .source = segment.source,
                .segmentIndex = segmentIndex,
                .chunkIndex = chunkIndex,
                .text = joinWords(words, begin, end),
                .wordCount = end - begin,
                .contentType = segment.contentType,
                .detectedLanguage = segment.detectedLanguage,
                .extractionConfidence = segment.extractionConfidence,
                .qualityScore = segment.qualityScore,
                .page = segment.page,
                .timestamp = segment.timestamp
            });

            ++chunkIndex;
            if (end == words.size()) {
                break;
            }
        }
    }

    return chunks;
}

} // namespace semantic_fs::rag
