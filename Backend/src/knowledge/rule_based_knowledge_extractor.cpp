#include "semantic_fs/knowledge/rule_based_knowledge_extractor.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace semantic_fs::knowledge {
namespace {

struct KnowledgeRulePattern {
    std::string id;
    std::string language;
    std::regex pattern;
    KnowledgeNodeType fromType = KnowledgeNodeType::Unknown;
    KnowledgeEdgeType edgeType = KnowledgeEdgeType::Unknown;
    KnowledgeNodeType toType = KnowledgeNodeType::Unknown;
    double confidence = 0.0;
    bool createsRule = false;
    std::string ruleTitlePrefix;
};

std::string trim(const std::string& text)
{
    const auto begin = std::find_if_not(text.begin(), text.end(), [](unsigned char character) {
        return std::isspace(character);
    });
    const auto end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char character) {
        return std::isspace(character);
    }).base();

    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string lowerAscii(std::string text)
{
    for (auto& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string normalizeName(const std::string& text)
{
    auto normalized = trim(text);
    while (!normalized.empty() && std::ispunct(static_cast<unsigned char>(normalized.back()))) {
        normalized.pop_back();
    }
    return trim(normalized);
}

std::string slug(const std::string& text)
{
    std::string result;
    bool previousDash = false;
    for (const auto character : lowerAscii(text)) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            result.push_back(character);
            previousDash = false;
        } else if (!previousDash) {
            result.push_back('-');
            previousDash = true;
        }
    }

    while (!result.empty() && result.front() == '-') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "unknown" : result;
}

std::string nodeId(KnowledgeNodeType type, const std::string& name)
{
    return lowerAscii(toString(type)) + ":" + slug(name);
}

std::string edgeId(const std::string& from, KnowledgeEdgeType type, const std::string& to)
{
    return from + ":" + toString(type) + ":" + to;
}

std::vector<std::string> splitSentences(const std::string& text)
{
    std::vector<std::string> sentences;
    std::string current;
    for (const auto character : text) {
        current.push_back(character);
        if (character == '.' || character == '\n' || character == ';') {
            const auto sentence = trim(current);
            if (!sentence.empty()) {
                sentences.push_back(sentence);
            }
            current.clear();
        }
    }

    const auto sentence = trim(current);
    if (!sentence.empty()) {
        sentences.push_back(sentence);
    }

    return sentences;
}

KnowledgeEvidence makeEvidence(
    const semantic_fs::core::FileDocument& document,
    const semantic_fs::extractors::ExtractedSegment& segment,
    std::size_t contextIndex,
    const std::string& text
)
{
    return {
        .text = normalizeName(text),
        .sourceFile = document.path(),
        .source = segment.source,
        .detectedLanguage = segment.detectedLanguage,
        .languageConfidence = segment.languageConfidence,
        .contextIndex = contextIndex,
        .authority = 0.80
    };
}

KnowledgeNode makeNode(
    KnowledgeNodeType type,
    const std::string& name,
    const KnowledgeEvidence& evidence,
    double confidence
)
{
    const auto cleanName = normalizeName(name);
    return {
        .id = nodeId(type, cleanName),
        .type = type,
        .name = cleanName,
        .confidence = confidence,
        .status = KnowledgeStatus::Accepted,
        .evidence = { evidence }
    };
}

KnowledgeEdge makeEdge(
    const KnowledgeNode& from,
    KnowledgeEdgeType type,
    const KnowledgeNode& to,
    const KnowledgeEvidence& evidence,
    double confidence
)
{
    return {
        .id = edgeId(from.id, type, to.id),
        .fromNodeId = from.id,
        .toNodeId = to.id,
        .type = type,
        .confidence = confidence,
        .status = KnowledgeStatus::Accepted,
        .evidence = { evidence }
    };
}

std::vector<KnowledgeRulePattern> buildPatterns()
{
    const auto flags = std::regex::icase;
    return {
        { "responsibility_es", "es", std::regex(R"(([A-Z][A-Za-z ]{1,48})\s+(?:es responsable de|debe)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Area, KnowledgeEdgeType::ResponsableDe, KnowledgeNodeType::Action, 0.92, false, {} },
        { "requirement_before_es", "es", std::regex(R"((?:Antes de|Antes de poder)\s+([^,\.:\n;]{3,100}),?\s+(?:se debe|debe|es obligatorio)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Action, 0.90, true, "Requisito para " },
        { "requirement_es", "es", std::regex(R"(([^\.:\n;]{3,100})\s+(?:requiere|necesita)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Action, 0.88, true, "Requisito para " },
        { "prohibition_es", "es", std::regex(R"(No se puede\s+([^\.:\n;]{3,100})\s+sin\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Rule, 0.94, true, "Restriccion para " },
        { "system_use_es", "es", std::regex(R"(([^\.:\n;]{3,100})\s+(?:usa|utiliza|se registra en|se carga en)\s+(?:el |la )?([A-Z][A-Za-z0-9 ]{1,40}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::UsaSistema, KnowledgeNodeType::System, 0.82, false, {} },
        { "approval_es", "es", std::regex(R"(([^\.:\n;]{3,100})\s+(?:requiere aprobacion de|debe ser aprobado por|es aprobado por)\s+([^\.:\n;]{3,80}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Aprueba, KnowledgeNodeType::Role, 0.88, true, "Aprobacion para " },

        { "responsibility_en", "en", std::regex(R"(([A-Z][A-Za-z ]{1,48})\s+(?:is responsible for|must)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Area, KnowledgeEdgeType::ResponsableDe, KnowledgeNodeType::Action, 0.92, false, {} },
        { "requirement_before_en", "en", std::regex(R"(Before\s+([^,\.:\n;]{3,100}),?\s+([^\.:\n;]{3,120})\s+must\s+(?:be done|be completed|be validated|be confirmed))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Action, 0.88, true, "Requirement for " },
        { "requirement_en", "en", std::regex(R"(([^\.:\n;]{3,100})\s+(?:requires|needs)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Action, 0.88, true, "Requirement for " },
        { "prohibition_en", "en", std::regex(R"(Cannot\s+([^\.:\n;]{3,100})\s+without\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Rule, 0.92, true, "Restriction for " },
        { "system_use_en", "en", std::regex(R"(([^\.:\n;]{3,100})\s+(?:uses|is registered in|is entered in)\s+(?:the )?([A-Z][A-Za-z0-9 ]{1,40}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::UsaSistema, KnowledgeNodeType::System, 0.82, false, {} },
        { "approval_en", "en", std::regex(R"(([^\.:\n;]{3,100})\s+(?:requires approval from|must be approved by|is approved by)\s+([^\.:\n;]{3,80}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Aprueba, KnowledgeNodeType::Role, 0.88, true, "Approval for " },

        { "responsibility_pt", "pt", std::regex(R"(([A-Z][A-Za-z ]{1,48})\s+(?:e responsavel por|deve)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Area, KnowledgeEdgeType::ResponsableDe, KnowledgeNodeType::Action, 0.90, false, {} },
        { "requirement_before_pt", "pt", std::regex(R"(Antes de\s+([^,\.:\n;]{3,100}),?\s+(?:deve-se|deve|e obrigatorio)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Action, 0.88, true, "Requisito para " },
        { "requirement_pt", "pt", std::regex(R"(([^\.:\n;]{3,100})\s+(?:requer|precisa de)\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Action, 0.86, true, "Requisito para " },
        { "prohibition_pt", "pt", std::regex(R"(Nao se pode\s+([^\.:\n;]{3,100})\s+sem\s+([^\.:\n;]{3,120}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Requiere, KnowledgeNodeType::Rule, 0.92, true, "Restricao para " },
        { "system_use_pt", "pt", std::regex(R"(([^\.:\n;]{3,100})\s+(?:usa|utiliza|e registrado em|e carregado em)\s+(?:o |a )?([A-Z][A-Za-z0-9 ]{1,40}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::UsaSistema, KnowledgeNodeType::System, 0.80, false, {} },
        { "approval_pt", "pt", std::regex(R"(([^\.:\n;]{3,100})\s+(?:requer aprovacao de|deve ser aprovado por|e aprovado por)\s+([^\.:\n;]{3,80}))", flags), KnowledgeNodeType::Action, KnowledgeEdgeType::Aprueba, KnowledgeNodeType::Role, 0.86, true, "Aprovacao para " }
    };
}

bool shouldApplyPattern(const KnowledgeRulePattern& pattern, const std::string& detectedLanguage, double confidence)
{
    if (confidence < 0.60 || detectedLanguage == "unknown" || detectedLanguage.empty()) {
        return true;
    }

    return lowerAscii(detectedLanguage).find(pattern.language) == 0;
}

void applyPattern(
    KnowledgeExtractionResult& result,
    const KnowledgeRulePattern& pattern,
    const semantic_fs::core::FileDocument& document,
    const semantic_fs::extractors::ExtractedSegment& segment,
    std::size_t contextIndex,
    const std::string& sentence
)
{
    std::smatch match;
    if (!std::regex_search(sentence, match, pattern.pattern) || match.size() < 3) {
        return;
    }

    const auto evidence = makeEvidence(document, segment, contextIndex, match.str(0));
    const auto from = makeNode(pattern.fromType, match.str(1), evidence, pattern.confidence);
    const auto to = makeNode(pattern.toType, match.str(2), evidence, pattern.confidence);
    result.nodes.push_back(from);
    result.nodes.push_back(to);
    result.edges.push_back(makeEdge(from, pattern.edgeType, to, evidence, pattern.confidence));

    if (pattern.createsRule) {
        result.rules.push_back({
            .id = "rule:" + slug(pattern.id + ":" + match.str(0)),
            .title = pattern.ruleTitlePrefix + from.name,
            .subject = from.name,
            .requiredAction = to.name,
            .confidence = pattern.confidence,
            .status = KnowledgeStatus::Accepted,
            .evidence = { evidence }
        });
    }
}

} // namespace

KnowledgeExtractionResult RuleBasedKnowledgeExtractor::extract(
    const semantic_fs::core::FileDocument& document
) const
{
    KnowledgeExtractionResult result;
    const auto patterns = buildPatterns();

    for (std::size_t contextIndex = 0; contextIndex < document.contexts().size(); ++contextIndex) {
        const auto& segment = document.contexts()[contextIndex];
        for (const auto& sentence : splitSentences(segment.text)) {
            for (const auto& pattern : patterns) {
                if (shouldApplyPattern(pattern, segment.detectedLanguage, segment.languageConfidence)) {
                    applyPattern(result, pattern, document, segment, contextIndex, sentence);
                }
            }
        }
    }

    return result;
}

} // namespace semantic_fs::knowledge
