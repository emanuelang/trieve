#include "semantic_fs/monitoring/durable_startup_coverage_sink.h"

namespace semantic_fs::monitoring {

DurableStartupCoverageSink::DurableStartupCoverageSink(
    ICatalogOutboxWriter& writer,
    const IPathSemantics& paths,
    const IFileSystemView& files,
    const IClock& clock,
    WatchRootConfig root)
    : writer_(writer), paths_(paths), files_(files), clock_(clock), root_(std::move(root))
{
}

CoverageDelivery DurableStartupCoverageSink::apply(const FileObservation& observation, ChangeKind kind)
{
    const auto result = writer_.apply({observation.rootId, observation.path, observation.metadata, observation.observedAt, kind, observation.source, {}});
    return result.status == MutationStatus::Applied
            || result.status == MutationStatus::Equivalent
            || result.status == MutationStatus::SoftLimited
        ? CoverageDelivery::Accepted
        : CoverageDelivery::Refused;
}

CoverageDelivery DurableStartupCoverageSink::persist(const CoverageCommand& command)
{
    const auto result = writer_.recordCoverage(command);
    return result.status == CoverageStatus::Persisted || result.status == CoverageStatus::Deferred
        ? CoverageDelivery::Accepted
        : CoverageDelivery::Refused;
}

CoverageDelivery DurableStartupCoverageSink::acceptRecord(const WatcherRecord& record)
{
    if (record.event.rootId.value() != root_.rootId.value()) return CoverageDelivery::Refused;

    if (record.event.kind == WatcherEventKind::Overflow) {
        return persist({record.event.rootId, record.sequence, static_cast<std::uint32_t>(DirtyReason::OsOverflow), {}});
    }
    if (record.event.kind == WatcherEventKind::Removed || record.event.kind == WatcherEventKind::Renamed) {
        const auto kind = record.event.kind == WatcherEventKind::Removed ? ChangeKind::Removed : ChangeKind::Renamed;
        persist({record.event.rootId, record.sequence, static_cast<std::uint32_t>(DirtyReason::SinkRefusal), kind});
        return CoverageDelivery::Refused;
    }

    const auto root = paths_.normalizeAbsolute(root_.root.utf8);
    const auto path = paths_.normalizeAbsolute(record.event.path.utf8);
    if (!root || !path) return CoverageDelivery::Refused;
    const auto relative = paths_.relativeTo(*root, *path);
    if (!relative) return CoverageDelivery::Refused;
    const auto metadata = files_.metadata(*path);
    if (const auto* error = std::get_if<FsError>(&metadata)) {
        return persist({record.event.rootId, record.sequence, static_cast<std::uint32_t>(DirtyReason::ScanFailure), {}});
    }
    const auto kind = record.event.kind == WatcherEventKind::Created ? ChangeKind::Created : ChangeKind::Modified;
    return apply({record.event.rootId, {path->utf8, *relative}, std::get<FileMetadata>(metadata), clock_.utcNow(), ObservationKind::Discovered, ObservationSource::Watcher}, kind);
}

CoverageDelivery DurableStartupCoverageSink::accept(const StartupCoverage& coverage)
{
    if (const auto* observation = std::get_if<FileObservation>(&coverage)) return apply(*observation, ChangeKind::Discovered);
    if (const auto* pending = std::get_if<PendingReconciliation>(&coverage)) return persist({pending->rootId, pending->epoch, pending->reasons, {}});
    const auto& ingress = std::get<WatcherIngress>(coverage);
    if (const auto* record = std::get_if<WatcherRecord>(&ingress)) return acceptRecord(*record);
    if (const auto* dirty = std::get_if<RootDirty>(&ingress)) return persist({dirty->rootId, dirty->epoch, dirty->reasons, {}});
    return CoverageDelivery::Accepted;
}

} // namespace semantic_fs::monitoring
