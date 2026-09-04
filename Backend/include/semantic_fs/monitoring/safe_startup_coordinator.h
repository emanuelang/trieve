#pragma once

#include "semantic_fs/monitoring/i_file_watcher.h"
#include "semantic_fs/monitoring/i_startup_coverage_sink.h"
#include "semantic_fs/monitoring/initial_scanner.h"

#include <mutex>
#include <stop_token>

namespace semantic_fs::monitoring {
class SafeStartupCoordinator final : public IWatcherIngressSink, public IFileObservationSink {
public:
    SafeStartupCoordinator(const IFileSystemView& fileSystem, const IPathSemantics& paths, const IClock& clock, IFileWatcher& watcher, IStartupCoverageSink& coverage);
    StartupOutcome start(const WatchRootConfig& root, const ScanOptions& options, std::stop_token stopToken);
    IngressDelivery accept(WatcherIngress ingress) override;
    ObservationDelivery observe(const FileObservation& observation) override;

private:
    StartupOutcome leaveNonHealthy(StartupStatus status, DirtyReason reason);
    bool deliver(const StartupCoverage& coverage);

    const IFileSystemView& fileSystem_;
    const IPathSemantics& paths_;
    const IClock& clock_;
    IFileWatcher& watcher_;
    IStartupCoverageSink& coverage_;
    std::mutex stateMutex_;
    std::optional<WatchRootConfig> root_;
    std::shared_ptr<IWatcherSession> session_;
    RootHealth health_{RootHealth::Stopped};
    GapEpoch dirtyEpoch_{};
    std::uint32_t dirtyReasons_{};
    std::optional<BarrierReached> barrier_;
    bool reconciling_{};
    bool coverageRefused_{};
    bool pendingReconciliation_{};
    std::optional<WatcherSequence> lastWatcherSequence_;
    InitialScanner scanner_;
};
} // namespace semantic_fs::monitoring
