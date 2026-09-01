#include "semantic_fs/rag/context_sanitizer.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace semantic_fs::rag {
namespace {

std::string trim(const std::string& text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), [](unsigned char value) {
        return std::isspace(value) != 0;
    });

    if (first == text.end()) {
        return {};
    }

    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char value) {
        return std::isspace(value) != 0;
    }).base();

    return std::string(first, last);
}

std::string collapseWhitespace(const std::string& text)
{
    std::string output;
    output.reserve(text.size());

    bool previousWasSpace = false;
    for (unsigned char value : text) {
        if (std::isspace(value) != 0) {
            if (!previousWasSpace) {
                output.push_back(' ');
                previousWasSpace = true;
            }
            continue;
        }

        output.push_back(static_cast<char>(value));
        previousWasSpace = false;
    }

    return trim(output);
}

std::size_t wordCount(const std::string& text)
{
    std::istringstream stream(text);
    std::size_t count = 0;
    std::string word;
    while (stream >> word) {
        ++count;
    }
    return count;
}

bool hasSemanticContent(const std::string& text)
{
    std::size_t letters = 0;
    std::size_t longWords = 0;
    std::istringstream stream(text);
    std::string word;

    while (stream >> word) {
        const auto alphaCount = static_cast<std::size_t>(std::count_if(
            word.begin(),
            word.end(),
            [](unsigned char value) {
                return std::isalpha(value) != 0;
            }
        ));

        letters += alphaCount;
        if (alphaCount >= 3) {
            ++longWords;
        }
    }

    return letters >= 12 && longWords >= 2;
}

double symbolRatio(const std::string& text)
{
    if (text.empty()) {
        return 1.0;
    }

    std::size_t symbols = 0;
    std::size_t visible = 0;
    for (unsigned char value : text) {
        if (std::isspace(value) != 0) {
            continue;
        }

        ++visible;
        if (std::isalnum(value) == 0) {
            ++symbols;
        }
    }

    if (visible == 0) {
        return 1.0;
    }

    return static_cast<double>(symbols) / static_cast<double>(visible);
}

double qualityScore(
    const std::string& text,
    double extractionConfidence,
    double maxSymbolRatio
)
{
    const auto words = wordCount(text);
    const auto lengthScore = std::min(1.0, static_cast<double>(text.size()) / 240.0);
    const auto wordScore = std::min(1.0, static_cast<double>(words) / 40.0);
    const auto confidenceScore = std::clamp(extractionConfidence, 0.0, 1.0);
    const auto noiseScore = 1.0 - std::min(1.0, symbolRatio(text) / maxSymbolRatio);

    return std::clamp(
        (lengthScore * 0.25) + (wordScore * 0.25) + (confidenceScore * 0.30) + (noiseScore * 0.20),
        0.0,
        1.0
    );
}

} // namespace

ContextSanitizer::ContextSanitizer(SanitizationPolicy policy)
    : policy_(policy)
{
}

std::vector<SanitizedSegment> ContextSanitizer::sanitize(
    const std::vector<ExtractedSegment>& segments
) const
{
    std::vector<SanitizedSegment> sanitized;
    sanitized.reserve(segments.size());

    for (const auto& segment : segments) {
        auto result = sanitizeOne(segment);
        if (result.status != SanitizationStatus::Discarded) {
            sanitized.push_back(std::move(result));
        }
    }

    return sanitized;
}

SanitizedSegment ContextSanitizer::sanitizeOne(const ExtractedSegment& segment) const
{
    const auto cleanText = collapseWhitespace(segment.text);
    SanitizedSegment sanitized {
        .text = cleanText,
        .originalText = segment.text,
        .source = segment.source,
        .contentType = segment.contentType,
        .detectedLanguage = segment.detectedLanguage,
        .languageConfidence = segment.languageConfidence,
        .extractionConfidence = segment.extractionConfidence,
        .qualityScore = qualityScore(cleanText, segment.extractionConfidence, policy_.maxSymbolRatio),
        .status = cleanText == segment.text ? SanitizationStatus::Kept : SanitizationStatus::Repaired,
        .discardReason = DiscardReason::None,
        .page = segment.page,
        .timestamp = segment.timestamp
    };

    if (cleanText.empty()) {
        sanitized.status = SanitizationStatus::Discarded;
        sanitized.discardReason = DiscardReason::EmptyText;
    } else if (cleanText.size() < policy_.minCharacters || wordCount(cleanText) < policy_.minWords) {
        sanitized.status = SanitizationStatus::Discarded;
        sanitized.discardReason = DiscardReason::TooShort;
    } else if (segment.extractionConfidence < policy_.minExtractionConfidence) {
        sanitized.status = SanitizationStatus::Discarded;
        sanitized.discardReason = DiscardReason::LowExtractionConfidence;
    } else if (symbolRatio(cleanText) > policy_.maxSymbolRatio) {
        sanitized.status = SanitizationStatus::Discarded;
        sanitized.discardReason = DiscardReason::SymbolNoise;
    } else if (!hasSemanticContent(cleanText)) {
        sanitized.status = SanitizationStatus::Discarded;
        sanitized.discardReason = DiscardReason::NoSemanticContent;
    }

    return sanitized;
}

} // namespace semantic_fs::rag
