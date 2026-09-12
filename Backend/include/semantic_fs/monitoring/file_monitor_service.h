#pragma once

#include "semantic_fs/monitoring/safe_startup_coordinator.h"

#include <mutex>

namespace semantic_fs::monitoring {

class FileMonitorService final {
public:
    FileMonitorService(SafeStartupCoordinator& startup, FileMonitorConfig config);

    FileMonitorStartOutcome start(
        const WatchRootConfig& root,
        const ScanOptions& options,
        std::optional<PendingReconciliation> recovered = std::nullopt,
        std::stop_token stopToken = {});
    FileMonitorStepStatus step();
    StopOutcome stop();
    [[nodiscard]] RootStatusSnapshot status(const RootId& rootId) const;

private:
    [[nodiscard]] bool validConfig() const;
    [[nodiscard]] std::string boundedDiagnostic(std::string_view value) const;

    SafeStartupCoordinator& startup_;
    FileMonitorConfig config_;
    mutable std::mutex mutex_;
    std::optional<WatchRootConfig> root_;
    RootHealth health_{RootHealth::Stopped};
    bool acceptingWork_{};
    bool pendingReconciliation_{};
    bool stopped_{};
    GapEpoch epoch_{};
    std::string diagnostic_{"monitoring stopped"};
};

} // namespace semantic_fs::monitoring
