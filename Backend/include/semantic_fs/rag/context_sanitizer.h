#pragma once

#include "semantic_fs/rag/rag_types.h"

#include <vector>

namespace semantic_fs::rag {

class ContextSanitizer {
public:
    ContextSanitizer() = default;
    explicit ContextSanitizer(SanitizationPolicy policy);

    std::vector<SanitizedSegment> sanitize(const std::vector<ExtractedSegment>& segments) const;
    SanitizedSegment sanitizeOne(const ExtractedSegment& segment) const;

private:
    SanitizationPolicy policy_;
};

} // namespace semantic_fs::rag
