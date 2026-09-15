#pragma once

#include "semantic_fs/core/file_document.h"
#include "semantic_fs/knowledge/knowledge_types.h"

namespace semantic_fs::knowledge {

class RuleBasedKnowledgeExtractor {
public:
    KnowledgeExtractionResult extract(const semantic_fs::core::FileDocument& document) const;
};

} // namespace semantic_fs::knowledge
