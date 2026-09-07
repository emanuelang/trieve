#pragma once
#include "semantic_fs/monitoring/monitoring_types.h"
namespace semantic_fs::monitoring {

class IClock {
public:
    virtual ~IClock() = default;

    virtual UtcTimestamp utcNow() const = 0;
    virtual MonotonicTimestamp monotonicNow() const = 0;
};

} // namespace semantic_fs::monitoring
