#pragma once

#include "semantic_fs/extractors/extraction_types.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::core {

class FileDocument;

} // namespace semantic_fs::core

namespace semantic_fs::rag {

enum class SanitizationStatus {
    Kept,
    Repaired,
    Discarded
};

enum class DiscardReason {
    None,
    EmptyText,
    TooShort,
    LowExtractionConfidence,
    SymbolNoise,
    NoSemanticContent
};

struct SanitizationPolicy {
    std::size_t minCharacters = 24;
    std::size_t minWords = 3;
    double minExtractionConfidence = 0.10;
    double maxSymbolRatio = 0.45;
};

struct ChunkingPolicy {
    std::size_t targetWords = 90;
    std::size_t overlapWords = 18;
    std::size_t minChunkWords = 8;
};

struct SanitizedSegment {
    std::string text;
    std::string originalText;
    std::string source;
    semantic_fs::extractors::ContentType contentType = semantic_fs::extractors::ContentType::Unknown;
    std::string detectedLanguage = "unknown";
    double languageConfidence = 0.0;
    double extractionConfidence = 0.0;
    double qualityScore = 0.0;
    SanitizationStatus status = SanitizationStatus::Kept;
    DiscardReason discardReason = DiscardReason::None;
    int page = -1;
    double timestamp = -1.0;
};

struct ChunkRecord {
    std::string id;
    std::filesystem::path filePath;
    std::string fileName;
    std::string source;
    std::size_t segmentIndex = 0;
    std::size_t chunkIndex = 0;
    std::string text;
    std::size_t wordCount = 0;
    semantic_fs::extractors::ContentType contentType = semantic_fs::extractors::ContentType::Unknown;
    std::string detectedLanguage = "unknown";
    double extractionConfidence = 0.0;
    double qualityScore = 0.0;
    int page = -1;
    double timestamp = -1.0;
};

struct EmbeddingRecord {
    std::string chunkId;
    std::vector<float> vector;
};

struct RetrievedChunk {
    ChunkRecord chunk;
    double score = 0.0;
};

struct RagIndexingResult {
    std::size_t rawSegmentCount = 0;
    std::size_t sanitizedSegmentCount = 0;
    std::size_t discardedSegmentCount = 0;
    std::size_t chunkCount = 0;
    std::size_t embeddingCount = 0;
};

using ExtractedSegment = semantic_fs::extractors::ExtractedSegment;

} // namespace semantic_fs::rag
