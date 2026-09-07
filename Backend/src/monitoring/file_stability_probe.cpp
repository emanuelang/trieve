#include "semantic_fs/monitoring/file_stability_probe.h"

namespace semantic_fs::monitoring {
namespace { constexpr std::int64_t kIntervalUs = 250000; constexpr std::int64_t kTimeoutUs = 5000000; constexpr std::uint32_t kMaxAttempts = 20;
bool same(const FileMetadata& left, const FileMetadata& right)
{
    const auto sameTime = !left.modifiedAt || !right.modifiedAt
        ? !left.modifiedAt && !right.modifiedAt
        : left.modifiedAt->microsecondsSinceEpoch == right.modifiedAt->microsecondsSinceEpoch;
    return left.size == right.size && sameTime && left.nativeFileId == right.nativeFileId;
} }

StabilityOutcome FileStabilityProbe::step(StabilitySample sample)
{
    if (sample.kind == StabilitySampleKind::Cancelled) return {StabilityStatus::Cancelled, {}};
    if (sample.kind != StabilitySampleKind::Metadata || !sample.metadata) return {StabilityStatus::Reconcile, {}};
    if (!startedAt_) startedAt_ = sample.observedAt;
    if (++attempts_ >= kMaxAttempts || sample.observedAt.microsecondsSinceOrigin - startedAt_->microsecondsSinceOrigin >= kTimeoutUs) return {StabilityStatus::Reconcile, {}};
    if (previous_ && same(*previous_, *sample.metadata) && sample.observedAt.microsecondsSinceOrigin - previousAt_->microsecondsSinceOrigin >= kIntervalUs) return {StabilityStatus::Stable, sample.metadata};
    if (!previous_ || !same(*previous_, *sample.metadata)) { previous_ = std::move(sample.metadata); previousAt_ = sample.observedAt; }
    return {StabilityStatus::Retry, {}};
}
} // namespace semantic_fs::monitoring
