#include "semantic_fs/monitoring/file_monitor_service.h"

namespace semantic_fs::monitoring {

FileMonitorService::FileMonitorService(SafeStartupCoordinator& startup, FileMonitorConfig config, IShutdownDrain* drain, IReconciliationExecutor* reconciliation)
    : startup_(startup), config_(std::move(config)), drain_(drain), reconciliation_(reconciliation)
{
}

bool FileMonitorService::validConfig() const
{
    return config_.maximumRoots && *config_.maximumRoots > 0
        && config_.maximumStatusBytes && *config_.maximumStatusBytes > 0;
}

std::string FileMonitorService::boundedDiagnostic(std::string_view value) const
{
    return std::string(value.substr(0, *config_.maximumStatusBytes));
}

FileMonitorStartOutcome FileMonitorService::start(
    const WatchRootConfig& root,
    const ScanOptions& options,
    std::optional<PendingReconciliation> recovered,
    std::stop_token stopToken)
{
    {
        std::lock_guard lock(mutex_);
        if (!validConfig()) return {FileMonitorStartStatus::InvalidConfig, RootHealth::Stopped};
        if (stopped_) return {FileMonitorStartStatus::Stopped, RootHealth::Stopped};
        if (root_) return {FileMonitorStartStatus::AlreadyStarted, health_};

        if (recovered && recovered->rootId.value() != root.rootId.value()) {
            root_ = root;
            health_ = RootHealth::Dirty;
            pendingReconciliation_ = true;
            epoch_ = recovered->epoch;
            diagnostic_ = boundedDiagnostic("foreign reconciliation obligation");
            return {FileMonitorStartStatus::Degraded, health_};
        }

        root_ = root;
        health_ = RootHealth::Starting;
        acceptingWork_ = false;
        pendingReconciliation_ = recovered.has_value();
        epoch_ = recovered ? recovered->epoch : GapEpoch{};
        diagnostic_ = boundedDiagnostic("monitoring starting");
    }
    ScanOptions startOptions = options;
    if (recovered) {
        const auto checkpoint = startOptions.checkpointHook;
        bool restored = false;
        startOptions.checkpointHook = [this, recovered, checkpoint, &restored](ScanCheckpoint point, const RelativePath& path) {
            if (!restored) {
                startup_.accept(RootDirty{recovered->rootId, recovered->epoch, recovered->reasons});
                restored = true;
            }
            return !checkpoint || checkpoint(point, path);
        };
    }
    const auto outcome = startup_.start(root, startOptions, stopToken);
    {
        std::lock_guard lock(mutex_);
        if (stopped_) return {FileMonitorStartStatus::Stopped, RootHealth::Stopped};
        health_ = outcome.health;
        pendingReconciliation_ = outcome.pending.has_value();
        acceptingWork_ = outcome.status == StartupStatus::Healthy;
        diagnostic_ = boundedDiagnostic(outcome.status == StartupStatus::Healthy ? "monitoring healthy" : "monitoring reconciliation required");
    }
    return {outcome.status == StartupStatus::Healthy ? FileMonitorStartStatus::Started : FileMonitorStartStatus::Degraded, health_};
}

FileMonitorStepStatus FileMonitorService::step()
{
    std::optional<std::pair<RootId, GapEpoch>> pending;
    {
        std::lock_guard lock(mutex_);
        if (stopped_) return FileMonitorStepStatus::Stopped;
        if (!quota_.reconciliationAdmissionAllowed || !acceptingWork_ || dirtyAdmissionPending_) return FileMonitorStepStatus::QuotaPaused;
        if (pendingReconciliation_ && (!reconciliation_ || !root_)) return FileMonitorStepStatus::ReconciliationUnavailable;
        if (pendingReconciliation_) pending = {{root_->rootId, epoch_}};
    }
    if (pending) {
        const auto execution = reconciliation_->step(pending->first, pending->second);
        const bool fencedCompletion = execution.fencedComplete;
        std::lock_guard lock(mutex_);
        const bool capturedFenceMatches = execution.rootId.value() == pending->first.value() && execution.epoch == pending->second;
        const bool currentFenceMatches = root_ && root_->rootId.value() == pending->first.value() && epoch_ == pending->second;
        if (!stopped_ && pendingReconciliation_ && fencedCompletion && capturedFenceMatches && currentFenceMatches) {
            pendingReconciliation_ = false;
            health_ = RootHealth::Healthy;
            diagnostic_ = boundedDiagnostic("monitoring healthy");
        }
    }
    return FileMonitorStepStatus::Admitted;
}
StopOutcome FileMonitorService::stop()
{
    const auto outcome = stop(DrainBudget{1});
    return outcome.status == FileMonitorStopStatus::Stopped ? StopOutcome::Stopped : outcome.watcher;
}
FileMonitorStopOutcome FileMonitorService::stop(DrainBudget budget)
{
    if (!budget.isFinite()) return {FileMonitorStopStatus::InvalidBudget, StopOutcome::NotAttempted, 0, 0, false};
    const auto maximumSteps = *budget.maximumSteps;
    std::optional<RootDirty> cancellation;
    {
        std::lock_guard lock(mutex_);
        if (stopped_) return {FileMonitorStopStatus::AlreadyStopped, StopOutcome::AlreadyStopped, 0, 0, false};
        if (dirtyAdmissionInFlight_) return {FileMonitorStopStatus::NotAttempted, StopOutcome::NotAttempted, 0, 0, false};
        acceptingWork_ = false;
        if (root_) {
            ++epoch_;
            cancellation = RootDirty{root_->rootId, epoch_, static_cast<std::uint32_t>(DirtyReason::Cancellation)};
        }
        pendingReconciliation_ = root_.has_value();
        health_ = RootHealth::Stopped;
        stopped_ = true;
        diagnostic_ = boundedDiagnostic("monitoring stopped with pending reconciliation");
    }
    if (cancellation) startup_.accept(*cancellation);
    const auto watcher = cancellation ? startup_.stop() : StopOutcome::AlreadyStopped;
    std::size_t attempts = 0;
    std::size_t completed = 0;
    bool remaining = false;
    if (drain_) {
        do {
            const auto progress = drain_->drainOne();
            ++attempts;
            completed += progress.completed ? 1 : 0;
            remaining = progress.remaining;
        } while (remaining && attempts < maximumSteps);
        drain_->releaseLease();
    }
    return {FileMonitorStopStatus::Stopped, watcher, attempts, completed,
        remaining && attempts == maximumSteps};
}
bool FileMonitorService::updateQuota(OutboxQuotaStatus quota)
{
    std::optional<RootDirty> saturation;
    {
        std::lock_guard lock(mutex_);
        quota_ = quota;
        if (stopped_) return false;
        const bool reconciliationPending = pendingReconciliation_;
        if (quota.hardLimited) {
            acceptingWork_ = false;
            pendingReconciliation_ = root_.has_value();
            dirtyAdmissionPending_ = root_.has_value();
            health_ = RootHealth::Dirty;
            if (root_) {
                if (dirtyAdmissionInFlight_) return false;
                ++epoch_;
                dirtyAdmissionInFlight_ = true;
                saturation = RootDirty{root_->rootId, epoch_, static_cast<std::uint32_t>(DirtyReason::QueueSaturation)};
            }
            diagnostic_ = boundedDiagnostic("monitoring quota saturated");
        } else if (!quota.reconciliationAdmissionAllowed) {
            acceptingWork_ = false;
            if (!reconciliationPending) health_ = RootHealth::Degraded;
            diagnostic_ = boundedDiagnostic("monitoring publication prioritized");
        } else if (root_) {
            acceptingWork_ = true;
            if (reconciliationPending) {
                health_ = RootHealth::Dirty;
                diagnostic_ = boundedDiagnostic("monitoring reconciliation required");
            } else {
                health_ = RootHealth::Healthy;
                diagnostic_ = boundedDiagnostic("monitoring healthy");
            }
        }
    }
    bool accepted = true; while (saturation) {
        accepted = startup_.accept(*saturation) == IngressDelivery::Accepted;
        std::lock_guard lock(mutex_);
        const bool fenceMatches = root_ && root_->rootId.value() == saturation->rootId.value() && epoch_ == saturation->epoch;
        if (accepted && fenceMatches) dirtyAdmissionPending_ = false;
        if (accepted && !stopped_ && dirtyAdmissionPending_ && root_) {
            saturation = RootDirty{root_->rootId, epoch_, static_cast<std::uint32_t>(DirtyReason::QueueSaturation)};
        } else {
            dirtyAdmissionInFlight_ = false;
            saturation.reset();
        }
    }
    return accepted && !dirtyAdmissionPending_;
}
RootStatusSnapshot FileMonitorService::status(const RootId& rootId) const
{
    std::lock_guard lock(mutex_);
    if (!root_ || root_->rootId.value() != rootId.value()) return {};
    return {health_, health_ == RootHealth::Healthy && acceptingWork_ && quota_.reconciliationAdmissionAllowed && !dirtyAdmissionPending_,
        acceptingWork_ && quota_.reconciliationAdmissionAllowed, pendingReconciliation_, quota_.saturated,
        quota_.publicationPrioritized, diagnostic_};
}
} // namespace semantic_fs::monitoring
