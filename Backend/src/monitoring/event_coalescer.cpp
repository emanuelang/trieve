#include "semantic_fs/monitoring/event_coalescer.h"

namespace semantic_fs::monitoring {
namespace { constexpr std::int64_t kWindowUs = 250000; constexpr std::size_t kMaxPaths = 1024; }

CoalescingOutcome EventCoalescer::accept(CoalescingInput input)
{
    auto key = std::string(input.rootId.value());
    if (!pending_.contains(key) && pending_.size() == maxRoots_) return {CoalescingStatus::Dirty, {}};
    auto [it, inserted] = pending_.try_emplace(key, Pending{input.epoch, input.acceptedAt, {}});
    auto& pending = it->second;
    if (!inserted && pending.epoch != input.epoch) { pending_.erase(it); return {CoalescingStatus::Dirty, {}}; }
    if (pending.paths.size() == kMaxPaths) { pending_.erase(it); return {CoalescingStatus::Dirty, {}}; }
    pending.paths.push_back(std::move(input.path));
    return {CoalescingStatus::Accepted, {}};
}

CoalescingOutcome EventCoalescer::step(const RootId& rootId, MonotonicTimestamp now)
{
    const auto it = pending_.find(std::string(rootId.value()));
    if (it == pending_.end() || now.microsecondsSinceOrigin - it->second.startedAt.microsecondsSinceOrigin < kWindowUs)
        return {CoalescingStatus::Waiting, {}};
    auto paths = std::move(it->second.paths); pending_.erase(it); return {CoalescingStatus::Ready, std::move(paths)};
}

CoalescingOutcome EventCoalescer::barrier(const RootId& rootId, GapEpoch epoch)
{
    const auto it = pending_.find(std::string(rootId.value()));
    if (it == pending_.end()) return {CoalescingStatus::Ready, {}};
    if (it->second.epoch == epoch) {
        auto paths = std::move(it->second.paths); pending_.erase(it); return {CoalescingStatus::Ready, std::move(paths)};
    }
    pending_.erase(it); return {CoalescingStatus::Dirty, {}};
}

CoalescingOutcome EventCoalescer::uncertain(const RootId& rootId, GapEpoch)
{
    pending_.erase(std::string(rootId.value()));
    return {CoalescingStatus::Dirty, {}};
}
} // namespace semantic_fs::monitoring
