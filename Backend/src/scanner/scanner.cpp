#include "semantic_fs/monitoring/initial_scanner.h"

#include <algorithm>

namespace semantic_fs::monitoring {
ScanResult InitialScanner::scan(const WatchRootConfig& config, const ScanOptions& options) const
{
    ScanResult result;
    const auto root = paths_.normalizeAbsolute(config.root.utf8);
    if (!root) { result.status = ScanStatus::RootUnavailable; return result; }
    const auto record = [&](FsErrorCode code, const RelativePath& path) {
        ++result.errorCount;
        if (result.diagnostics.size() < options.diagnosticCapacity) result.diagnostics.push_back({code, path});
    };
    const auto checkpoint = [&](ScanCheckpoint point, const RelativePath& path) {
        if (!options.checkpointHook || options.checkpointHook(point, path)) return true;
        result.status = ScanStatus::Cancelled;
        return false;
    };
    std::function<bool(const AbsolutePath&, const RelativePath&, bool)> visit;
    visit = [&](const AbsolutePath& directory, const RelativePath& directoryPath, bool isRoot) {
        if (!checkpoint(ScanCheckpoint::BeforeDescent, directoryPath)) return false;
        auto listed = fileSystem_.list(directory);
        if (const auto* error = std::get_if<FsError>(&listed)) {
            if (isRoot) result.status = ScanStatus::RootUnavailable;
            else record(error->code, directoryPath);
            return !isRoot;
        }
        auto entries = std::get<std::vector<FileSystemEntry>>(std::move(listed));
        std::sort(entries.begin(), entries.end(), [&](const FileSystemEntry& left, const FileSystemEntry& right) {
            const auto leftRelative = paths_.relativeTo(*root, left.path);
            const auto rightRelative = paths_.relativeTo(*root, right.path);
            if (!leftRelative || !rightRelative) return left.path.utf8 < right.path.utf8;
            const auto count = std::min(leftRelative->components.size(), rightRelative->components.size());
            for (std::size_t index = 0; index < count; ++index) {
                const auto comparison = paths_.compareComponent(leftRelative->components[index], rightRelative->components[index]);
                if (comparison != 0) return comparison < 0;
            }
            return leftRelative->components.size() < rightRelative->components.size();
        });
        for (const auto& entry : entries) {
            const auto relative = paths_.relativeTo(*root, entry.path);
            if (!relative) { record(FsErrorCode::InvalidEncoding, {}); continue; }
            if (entry.kind == FileSystemEntryKind::LinkOrReparse || entry.kind == FileSystemEntryKind::Other) continue;
            if (entry.kind == FileSystemEntryKind::Directory) {
                if (!policy_.owns(*root, entry.path) || !visit(entry.path, *relative, false)) return result.status == ScanStatus::Completed;
                continue;
            }
            if (!policy_.owns(*root, entry.path) || !checkpoint(ScanCheckpoint::BeforeMetadata, *relative)) return false;
            const auto metadata = fileSystem_.metadata(entry.path);
            if (const auto* error = std::get_if<FsError>(&metadata)) { record(error->code, *relative); continue; }
            const auto& value = std::get<FileMetadata>(metadata);
            if (!policy_.owns(*root, entry.path) || !policy_.admits(entry.kind, *relative, value.size, config.policy)) continue;
            if (!checkpoint(ScanCheckpoint::BeforeDelivery, *relative)) return false;
            const FileObservation observation{config.rootId, {entry.path.utf8, *relative}, value, clock_.utcNow(), ObservationKind::Discovered, ObservationSource::InitialScan};
            result.sinkOutcome = sink_.observe(observation);
            if (result.sinkOutcome != ObservationDelivery::Accepted) { result.status = ScanStatus::SinkStopped; return false; }
            ++result.observationCount;
        }
        return true;
    };
    visit(*root, {}, true);
    return result;
}
} // namespace semantic_fs::monitoring