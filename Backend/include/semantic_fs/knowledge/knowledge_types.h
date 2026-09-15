#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace semantic_fs::knowledge {

enum class KnowledgeNodeType {
    Unknown,
    Area,
    Role,
    Process,
    Action,
    Rule,
    Document,
    System,
    Concept
};

enum class KnowledgeEdgeType {
    Unknown,
    ResponsableDe,
    Requiere,
    UsaSistema,
    PerteneceA,
    Bloquea,
    Aprueba,
    Evidencia
};

enum class KnowledgeStatus {
    Candidate,
    Accepted,
    Conflict,
    Rejected
};

struct KnowledgeEvidence {
    std::string text;
    std::filesystem::path sourceFile;
    std::string source;
    std::string detectedLanguage = "unknown";
    double languageConfidence = 0.0;
    std::size_t contextIndex = 0;
    double authority = 0.75;
};

struct KnowledgeNode {
    std::string id;
    KnowledgeNodeType type = KnowledgeNodeType::Unknown;
    std::string name;
    double confidence = 0.0;
    KnowledgeStatus status = KnowledgeStatus::Candidate;
    std::vector<KnowledgeEvidence> evidence;
};

struct KnowledgeEdge {
    std::string id;
    std::string fromNodeId;
    std::string toNodeId;
    KnowledgeEdgeType type = KnowledgeEdgeType::Unknown;
    double confidence = 0.0;
    KnowledgeStatus status = KnowledgeStatus::Candidate;
    std::vector<KnowledgeEvidence> evidence;
};

struct KnowledgeRule {
    std::string id;
    std::string title;
    std::string subject;
    std::string requiredAction;
    std::string responsible;
    double confidence = 0.0;
    KnowledgeStatus status = KnowledgeStatus::Candidate;
    std::vector<KnowledgeEvidence> evidence;
};

struct KnowledgeExtractionResult {
    std::vector<KnowledgeNode> nodes;
    std::vector<KnowledgeEdge> edges;
    std::vector<KnowledgeRule> rules;
};

struct KnowledgeIndexingResult {
    std::size_t nodeCount = 0;
    std::size_t edgeCount = 0;
    std::size_t ruleCount = 0;
};

struct RetrievedKnowledge {
    KnowledgeEdge edge;
    KnowledgeNode from;
    KnowledgeNode to;
    double score = 0.0;
};

std::string toString(KnowledgeNodeType type);
std::string toString(KnowledgeEdgeType type);
std::string toString(KnowledgeStatus status);

} // namespace semantic_fs::knowledge
