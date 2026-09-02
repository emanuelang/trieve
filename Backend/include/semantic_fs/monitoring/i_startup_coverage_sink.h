#pragma once

#include "semantic_fs/monitoring/monitoring_types.h"

namespace semantic_fs::monitoring {
class IStartupCoverageSink {
public:
    virtual ~IStartupCoverageSink() = default;
    virtual CoverageDelivery accept(const StartupCoverage& coverage) = 0;
};
} // namespace semantic_fs::monitoring
