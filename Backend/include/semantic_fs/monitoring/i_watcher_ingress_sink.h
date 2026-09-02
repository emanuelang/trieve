#pragma once

#include "semantic_fs/monitoring/monitoring_types.h"

namespace semantic_fs::monitoring {
class IWatcherIngressSink {
public:
    virtual ~IWatcherIngressSink() = default;
    virtual IngressDelivery accept(WatcherIngress ingress) = 0;
};
} // namespace semantic_fs::monitoring
