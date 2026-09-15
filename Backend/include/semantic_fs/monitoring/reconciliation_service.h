#pragma once

#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_clock.h"

#include <map>

namespace semantic_fs::monitoring {
enum class ReconciliationStepStatus { AwaitingBarrier, Dirty, Busy };
using ReconciliationCatalogEntry = CatalogPageEntry;
struct ReconciliationStepResult { ReconciliationStepStatus status; std::size_t applied; };

enum class ReconciliationTrigger { Startup, Interval, DirtyOverflow, PostSaturation };
enum class ReconciliationAdmissionStatus { Admitted, Coalesced, NotDue };

class ReconciliationScheduler final {
public:
    explicit ReconciliationScheduler(const IClock& clock) : clock_(clock) {}

    ReconciliationAdmissionStatus request(const RootId&, GapEpoch, ReconciliationTrigger);
    bool complete(const RootId&, GapEpoch);
    [[nodiscard]] std::size_t activeRunCount() const { return active_.size(); }

private:
    struct Run { GapEpoch epoch; };

    const IClock& clock_;
    std::map<std::string, Run, std::less<>> active_;
    std::map<std::string, MonotonicTimestamp, std::less<>> lastAdmission_;
};

class ReconciliationService {
public:
    ReconciliationStepResult step(ICatalogOutboxWriter&, const RootId&, GapEpoch, const std::vector<ReconciliationCatalogEntry>&, UtcTimestamp);
    ReconciliationStepResult stepPage(ICatalogOutboxWriter&, const RootId&, GapEpoch, const CatalogPage&, UtcTimestamp);
    bool accept(BarrierReached);
    bool accept(ICatalogOutboxWriter&, BarrierReached);
    [[nodiscard]] bool dirtyClearEligible() const { return eligible_; }
private:
    std::optional<RootId> root_; std::optional<EventId> finalOutboxEvent_; std::string runId_; GapEpoch epoch_ = 0; WatcherSequence requiredHighWater_ = 0; bool eligible_ = false;
};
} // namespace semantic_fs::monitoring
