#include "semantic_fs/monitoring/safe_startup_coordinator.h"

namespace semantic_fs::monitoring {
SafeStartupCoordinator::SafeStartupCoordinator(const IFileSystemView& fileSystem, const IPathSemantics& paths, const IClock& clock, IFileWatcher& watcher, IStartupCoverageSink& coverage)
    : fileSystem_(fileSystem), paths_(paths), clock_(clock), watcher_(watcher), coverage_(coverage), scanner_(fileSystem, paths, clock, *this) {}

StartupOutcome SafeStartupCoordinator::leaveNonHealthy(StartupStatus status, DirtyReason reason)
{
    std::lock_guard lock(stateMutex_);
    ++dirtyEpoch_;
    dirtyReasons_ |= static_cast<std::uint32_t>(reason);
    health_ = status == StartupStatus::Cancelled ? RootHealth::Cancelled : RootHealth::Degraded;
    const PendingReconciliation pending{root_->rootId, dirtyEpoch_, dirtyReasons_};
    coverage_.accept(pending);
    return {status, health_, pending};
}

bool SafeStartupCoordinator::deliver(const StartupCoverage& item)
{
    return coverage_.accept(item) == CoverageDelivery::Accepted;
}

IngressDelivery SafeStartupCoordinator::accept(WatcherIngress ingress)
{
    std::lock_guard lock(stateMutex_);
    if (health_ == RootHealth::Stopped) return IngressDelivery::Stopped;
    if (const auto* dirty = std::get_if<RootDirty>(&ingress)) {
        dirtyEpoch_ = std::max(dirtyEpoch_, dirty->epoch);
        dirtyReasons_ |= dirty->reasons;
        health_ = RootHealth::Dirty;
    }
    if (const auto* reached = std::get_if<BarrierReached>(&ingress)) barrier_ = *reached;
    if (!deliver(ingress)) {
        ++dirtyEpoch_;
        dirtyReasons_ |= static_cast<std::uint32_t>(DirtyReason::SinkRefusal);
        health_ = RootHealth::Dirty;
        return IngressDelivery::Stopped;
    }
    return IngressDelivery::Accepted;
}

ObservationDelivery SafeStartupCoordinator::observe(const FileObservation& observation)
{
    std::lock_guard lock(stateMutex_);
    auto accepted = observation;
    if (reconciling_) accepted.source = ObservationSource::Reconciliation;
    if (health_ == RootHealth::Stopped || !deliver(accepted)) return ObservationDelivery::Rejected;
    return ObservationDelivery::Accepted;
}

StartupOutcome SafeStartupCoordinator::start(const WatchRootConfig& root, const ScanOptions& options, std::stop_token stopToken)
{
    {
        std::lock_guard lock(stateMutex_);
        root_ = root; health_ = RootHealth::Starting; barrier_.reset(); dirtyEpoch_ = 0; dirtyReasons_ = 0;
    }
    const auto started = watcher_.start(root, *this);
    if (!started.session || (started.status != WatcherStartStatus::Started && started.status != WatcherStartStatus::AlreadyStarted)) return leaveNonHealthy(StartupStatus::Degraded, DirtyReason::NativeFailure);
    session_ = started.session;
    if (stopToken.stop_requested()) {
        session_->requestCancellation(); session_->stopAndJoin();
        return leaveNonHealthy(StartupStatus::Cancelled, DirtyReason::Cancellation);
    }
    const auto scan = [&] {
        ScanOptions scanOptions = options;
        const auto callerCheckpoint = scanOptions.checkpointHook;
        scanOptions.checkpointHook = [stopToken, callerCheckpoint](ScanCheckpoint checkpoint, const RelativePath& path) {
            return !stopToken.stop_requested() && (!callerCheckpoint || callerCheckpoint(checkpoint, path));
        };
        return scanner_.scan(root, scanOptions);
    };
    const auto initial = scan();
    if (initial.status != ScanStatus::Completed) {
        session_->requestCancellation(); session_->stopAndJoin();
        return leaveNonHealthy(initial.status == ScanStatus::Cancelled ? StartupStatus::Cancelled : StartupStatus::Degraded, initial.status == ScanStatus::Cancelled ? DirtyReason::Cancellation : DirtyReason::ScanFailure);
    }
    std::optional<GapEpoch> reconciliationEpoch;
    {
        std::lock_guard lock(stateMutex_);
        if (dirtyReasons_ != 0) { reconciliationEpoch = dirtyEpoch_; reconciling_ = true; }
    }
    const auto reconciliation = reconciliationEpoch ? scan() : ScanResult{};
    {
        std::lock_guard lock(stateMutex_);
        reconciling_ = false;
    }
    if (reconciliationEpoch && reconciliation.status != ScanStatus::Completed) {
        session_->requestCancellation(); session_->stopAndJoin();
        return leaveNonHealthy(reconciliation.status == ScanStatus::Cancelled ? StartupStatus::Cancelled : StartupStatus::Degraded, reconciliation.status == ScanStatus::Cancelled ? DirtyReason::Cancellation : DirtyReason::ScanFailure);
    }
    if (stopToken.stop_requested()) {
        session_->requestCancellation(); session_->stopAndJoin();
        return leaveNonHealthy(StartupStatus::Cancelled, DirtyReason::Cancellation);
    }
    const auto request = session_->requestBarrier();
    {
        std::lock_guard lock(stateMutex_);
        const bool barrierFailed = request.status != BarrierRequestStatus::Accepted || !request.id || !barrier_ || barrier_->id != *request.id || (reconciliationEpoch && (barrier_->epoch < *reconciliationEpoch || dirtyEpoch_ != *reconciliationEpoch)) || (!reconciliationEpoch && dirtyReasons_ != 0);
        if (!barrierFailed) {
            if (reconciliationEpoch) dirtyReasons_ = 0;
            health_ = RootHealth::Healthy;
            return {StartupStatus::Healthy, health_, std::nullopt};
        }
    }
    session_->requestCancellation(); session_->stopAndJoin();
    return leaveNonHealthy(StartupStatus::Degraded, DirtyReason::BarrierFailure);
}
} // namespace semantic_fs::monitoring
