#pragma once

#include "semantic_fs/monitoring/monitoring_types.h"

#include <cstddef>
#include <unordered_map>

namespace semantic_fs::monitoring {

enum class CoalescingStatus { Accepted, Waiting, Ready, Dirty };
struct CoalescingInput { RootId rootId; NormalizedPath path; MonotonicTimestamp acceptedAt; GapEpoch epoch; };
struct CoalescingOutcome { CoalescingStatus status; std::vector<NormalizedPath> paths; };

class EventCoalescer {
public:
    explicit EventCoalescer(std::size_t maxRoots = 1024) : maxRoots_(maxRoots) {}
    CoalescingOutcome accept(CoalescingInput input);
    CoalescingOutcome step(const RootId& rootId, MonotonicTimestamp now);
    CoalescingOutcome barrier(const RootId& rootId, GapEpoch epoch);
    CoalescingOutcome uncertain(const RootId& rootId, GapEpoch epoch);
private:
    struct Pending { GapEpoch epoch; MonotonicTimestamp startedAt; std::vector<NormalizedPath> paths; };
    std::unordered_map<std::string, Pending> pending_;
    std::size_t maxRoots_;
};

} // namespace semantic_fs::monitoring
