#include "semantic_fs/knowledge/knowledge_types.h"

namespace semantic_fs::knowledge {

std::string toString(KnowledgeNodeType type)
{
    switch (type) {
    case KnowledgeNodeType::Area:
        return "Area";
    case KnowledgeNodeType::Role:
        return "Role";
    case KnowledgeNodeType::Process:
        return "Process";
    case KnowledgeNodeType::Action:
        return "Action";
    case KnowledgeNodeType::Rule:
        return "Rule";
    case KnowledgeNodeType::Document:
        return "Document";
    case KnowledgeNodeType::System:
        return "System";
    case KnowledgeNodeType::Concept:
        return "Concept";
    case KnowledgeNodeType::Unknown:
    default:
        return "Unknown";
    }
}

std::string toString(KnowledgeEdgeType type)
{
    switch (type) {
    case KnowledgeEdgeType::ResponsableDe:
        return "responsable_de";
    case KnowledgeEdgeType::Requiere:
        return "requiere";
    case KnowledgeEdgeType::UsaSistema:
        return "usa_sistema";
    case KnowledgeEdgeType::PerteneceA:
        return "pertenece_a";
    case KnowledgeEdgeType::Bloquea:
        return "bloquea";
    case KnowledgeEdgeType::Aprueba:
        return "aprueba";
    case KnowledgeEdgeType::Evidencia:
        return "evidencia";
    case KnowledgeEdgeType::Unknown:
    default:
        return "unknown";
    }
}

std::string toString(KnowledgeStatus status)
{
    switch (status) {
    case KnowledgeStatus::Candidate:
        return "candidate";
    case KnowledgeStatus::Accepted:
        return "accepted";
    case KnowledgeStatus::Conflict:
        return "conflict";
    case KnowledgeStatus::Rejected:
        return "rejected";
    default:
        return "candidate";
    }
}

} // namespace semantic_fs::knowledge
