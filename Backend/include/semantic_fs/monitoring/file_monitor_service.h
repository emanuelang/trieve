#pragma once

#include "semantic_fs/monitoring/outbox_publisher.h"
#include "semantic_fs/monitoring/reconciliation_service.h"
#include "semantic_fs/monitoring/safe_startup_coordinator.h"

#include <mutex>

namespace semantic_fs::monitoring {

struct ReconciliationExecution { RootId rootId; GapEpoch epoch; ReconciliationStepResult step; bool fencedComplete; };
class IReconciliationExecutor { public: virtual ~IReconciliationExecutor() = default; virtual ReconciliationExecution step(const RootId&, GapEpoch) = 0; };

class FileMonitorService final {
public:
    FileMonitorService(SafeStartupCoordinator& startup, FileMonitorConfig config, IShutdownDrain* drain = nullptr, IReconciliationExecutor* reconciliation = nullptr);

    FileMonitorStartOutcome start(
        const WatchRootConfig& root,
        const ScanOptions& options,
        std::optional<PendingReconciliation> recovered = std::nullopt,
        std::stop_token stopToken = {});
    FileMonitorStepStatus step();
    StopOutcome stop();
    FileMonitorStopOutcome stop(DrainBudget budget); bool updateQuota(OutboxQuotaStatus quota);
    [[nodiscard]] RootStatusSnapshot status(const RootId& rootId) const;

private:
    [[nodiscard]] bool validConfig() const;
    [[nodiscard]] std::string boundedDiagnostic(std::string_view value) const;
    bool scheduleReconciliation(ReconciliationTrigger trigger);

    SafeStartupCoordinator& startup_;
    FileMonitorConfig config_;
    IShutdownDrain* drain_;
    IReconciliationExecutor* reconciliation_;
    mutable std::mutex mutex_;
    ReconciliationScheduler scheduler_;
    std::optional<WatchRootConfig> root_;
    RootHealth health_{RootHealth::Stopped};
    bool acceptingWork_{};
    bool pendingReconciliation_{};
    bool scheduledReconciliation_{};
    bool reconciliationExecutionInFlight_{};
    bool dirtyAdmissionPending_{}, dirtyAdmissionInFlight_{};
    bool stopped_{};
    OutboxQuotaStatus quota_{};
    GapEpoch epoch_{};
    std::string diagnostic_{"monitoring stopped"};
};

} // namespace semantic_fs::monitoring
