#pragma once

#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"

namespace semantic_fs::monitoring {
enum class ReconciliationStepStatus { AwaitingBarrier, Dirty, Busy };
struct ReconciliationCatalogEntry { NormalizedPath path; FileMetadata metadata; };
struct ReconciliationStepResult { ReconciliationStepStatus status; std::size_t applied; };
class ReconciliationService {
public:
    ReconciliationStepResult step(ICatalogOutboxWriter&, const RootId&, GapEpoch, const std::vector<ReconciliationCatalogEntry>&, UtcTimestamp);
    bool accept(BarrierReached);
    [[nodiscard]] bool dirtyClearEligible() const { return eligible_; }
private:
    std::optional<RootId> root_; GapEpoch epoch_ = 0; bool eligible_ = false;
};
} // namespace semantic_fs::monitoring
