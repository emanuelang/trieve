#include "semantic_fs/monitoring/file_monitor_service.h"

namespace semantic_fs::monitoring {

FileMonitorService::FileMonitorService(SafeStartupCoordinator& startup, FileMonitorConfig config)
    : startup_(startup), config_(std::move(config))
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
    std::lock_guard lock(mutex_);
    return acceptingWork_ ? FileMonitorStepStatus::Admitted : FileMonitorStepStatus::Stopped;
}

StopOutcome FileMonitorService::stop()
{
    std::lock_guard lock(mutex_);
    if (stopped_) return StopOutcome::AlreadyStopped;
    if (root_) {
        ++epoch_;
        startup_.accept(RootDirty{root_->rootId, epoch_, static_cast<std::uint32_t>(DirtyReason::Cancellation)});
        startup_.stop();
    }
    acceptingWork_ = false;
    pendingReconciliation_ = root_.has_value();
    health_ = RootHealth::Stopped;
    stopped_ = true;
    diagnostic_ = boundedDiagnostic("monitoring stopped with pending reconciliation");
    return StopOutcome::Stopped;
}

RootStatusSnapshot FileMonitorService::status(const RootId& rootId) const
{
    std::lock_guard lock(mutex_);
    if (!root_ || root_->rootId.value() != rootId.value()) return {};
    return {health_, health_ == RootHealth::Healthy && acceptingWork_, acceptingWork_, pendingReconciliation_, diagnostic_};
}

} // namespace semantic_fs::monitoring
