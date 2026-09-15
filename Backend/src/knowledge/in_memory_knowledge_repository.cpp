#include "semantic_fs/knowledge/in_memory_knowledge_repository.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace semantic_fs::knowledge {
namespace {

std::string lowerAscii(std::string text)
{
    for (auto& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    for (const auto character : lowerAscii(text)) {
        if (std::isalnum(static_cast<unsigned char>(character))) {
            current.push_back(character);
            continue;
        }

        if (current.size() >= 3) {
            tokens.push_back(current);
        }
        current.clear();
    }

    if (current.size() >= 3) {
        tokens.push_back(current);
    }

    return tokens;
}

double scoreText(const std::string& query, const std::string& text)
{
    const auto queryTokens = tokenize(query);
    if (queryTokens.empty()) {
        return 0.0;
    }

    const auto candidate = lowerAscii(text);
    std::size_t matches = 0;
    for (const auto& token : queryTokens) {
        if (candidate.find(token) != std::string::npos) {
            ++matches;
        }
    }

    return static_cast<double>(matches) / static_cast<double>(queryTokens.size());
}

std::string edgeSearchText(const KnowledgeEdge& edge, const KnowledgeNode& from, const KnowledgeNode& to)
{
    std::ostringstream output;
    output << from.name << " " << toString(from.type) << " ";
    output << toString(edge.type) << " ";
    output << to.name << " " << toString(to.type) << " ";
    for (const auto& evidence : edge.evidence) {
        output << evidence.text << " ";
    }
    return output.str();
}

} // namespace

KnowledgeIndexingResult InMemoryKnowledgeRepository::save(KnowledgeExtractionResult result)
{
    for (auto& node : result.nodes) {
        auto existing = nodesById_.find(node.id);
        if (existing == nodesById_.end()) {
            nodesById_.emplace(node.id, std::move(node));
            continue;
        }

        existing->second.confidence = std::max(existing->second.confidence, node.confidence);
        existing->second.evidence.insert(
            existing->second.evidence.end(),
            node.evidence.begin(),
            node.evidence.end()
        );
    }

    for (auto& edge : result.edges) {
        auto existing = edgesById_.find(edge.id);
        if (existing == edgesById_.end()) {
            edgesById_.emplace(edge.id, std::move(edge));
            continue;
        }

        existing->second.confidence = std::max(existing->second.confidence, edge.confidence);
        existing->second.evidence.insert(
            existing->second.evidence.end(),
            edge.evidence.begin(),
            edge.evidence.end()
        );
    }

    return {
        .nodeCount = nodesById_.size(),
        .edgeCount = edgesById_.size(),
        .ruleCount = result.rules.size()
    };
}

std::vector<RetrievedKnowledge> InMemoryKnowledgeRepository::searchRelated(
    const std::string& query,
    std::size_t maxResults
) const
{
    std::vector<RetrievedKnowledge> results;
    if (maxResults == 0) {
        return results;
    }

    for (const auto& [edgeId, edge] : edgesById_) {
        const auto from = nodesById_.find(edge.fromNodeId);
        const auto to = nodesById_.find(edge.toNodeId);
        if (from == nodesById_.end() || to == nodesById_.end()) {
            continue;
        }

        const auto score = scoreText(query, edgeSearchText(edge, from->second, to->second));
        if (score <= 0.0) {
            continue;
        }

        results.push_back({
            .edge = edge,
            .from = from->second,
            .to = to->second,
            .score = score
        });
    }

    std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });

    if (results.size() > maxResults) {
        results.resize(maxResults);
    }

    return results;
}

std::vector<KnowledgeNode> InMemoryKnowledgeRepository::nodes() const
{
    std::vector<KnowledgeNode> result;
    result.reserve(nodesById_.size());
    for (const auto& [id, node] : nodesById_) {
        result.push_back(node);
    }
    return result;
}

std::vector<KnowledgeEdge> InMemoryKnowledgeRepository::edges() const
{
    std::vector<KnowledgeEdge> result;
    result.reserve(edgesById_.size());
    for (const auto& [id, edge] : edgesById_) {
        result.push_back(edge);
    }
    return result;
}

} // namespace semantic_fs::knowledge
