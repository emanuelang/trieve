#pragma once
#include "semantic_fs/monitoring/i_watcher_ingress_sink.h"

#include <memory>

namespace semantic_fs::monitoring {
class IWatcherSession {
public:
    virtual ~IWatcherSession() = default;
    virtual BarrierRequestOutcome requestBarrier() noexcept = 0;
    virtual CancelOutcome requestCancellation() noexcept = 0;
    virtual StopOutcome stopAndJoin() noexcept = 0;
};

struct WatcherStartOutcome { WatcherStartStatus status; std::shared_ptr<IWatcherSession> session; };

class IFileWatcher {
public:
    virtual ~IFileWatcher() = default;
    virtual WatcherStartOutcome start(const WatchRootConfig& config, IWatcherIngressSink& ingress) = 0;
    virtual StopOutcome stopAndJoin() noexcept = 0;
};
} // namespace semantic_fs::monitoring
