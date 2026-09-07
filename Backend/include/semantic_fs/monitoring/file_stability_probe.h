#pragma once

#include "semantic_fs/monitoring/monitoring_types.h"

namespace semantic_fs::monitoring {

enum class StabilitySampleKind { Metadata, NotFound, AccessError, TempSaveRenameAmbiguity, Cancelled };
enum class StabilityStatus { Retry, Stable, Reconcile, Cancelled };
struct StabilitySample { StabilitySampleKind kind{StabilitySampleKind::Metadata}; std::optional<FileMetadata> metadata; MonotonicTimestamp observedAt; };
struct StabilityOutcome { StabilityStatus status; std::optional<FileMetadata> metadata; };

class FileStabilityProbe {
public:
    StabilityOutcome step(StabilitySample sample);
private:
    std::optional<FileMetadata> previous_;
    std::optional<MonotonicTimestamp> previousAt_;
    std::optional<MonotonicTimestamp> startedAt_;
    std::uint32_t attempts_{};
};
} // namespace semantic_fs::monitoring
