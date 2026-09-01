#pragma once

#include "semantic_fs/rag/chunker.h"
#include "semantic_fs/rag/context_sanitizer.h"
#include "semantic_fs/rag/embedding_service.h"
#include "semantic_fs/rag/i_rag_repository.h"
#include "semantic_fs/rag/rag_types.h"

#include <memory>

namespace semantic_fs::rag {

class RagIndexingPipeline {
public:
    RagIndexingPipeline(
        ContextSanitizer sanitizer,
        Chunker chunker,
        EmbeddingService embeddingService,
        std::shared_ptr<IRagRepository> repository
    );

    RagIndexingResult index(const semantic_fs::core::FileDocument& document) const;

private:
    ContextSanitizer sanitizer_;
    Chunker chunker_;
    EmbeddingService embeddingService_;
    std::shared_ptr<IRagRepository> repository_;
};

} // namespace semantic_fs::rag
