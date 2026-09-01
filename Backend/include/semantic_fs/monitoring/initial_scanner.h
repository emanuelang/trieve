#pragma once

#include "semantic_fs/monitoring/i_clock.h"
#include "semantic_fs/monitoring/i_file_observation_sink.h"
#include "semantic_fs/monitoring/i_file_system_view.h"
#include "semantic_fs/monitoring/path_policy.h"

#include <functional>

namespace semantic_fs::monitoring {
enum class ScanCheckpoint { BeforeDescent, BeforeMetadata, BeforeDelivery };
enum class ScanStatus { Completed, Cancelled, RootUnavailable, SinkStopped };
struct ScanDiagnostic { FsErrorCode code; RelativePath relativePath; };
struct ScanOptions { std::size_t diagnosticCapacity{}; std::function<bool(ScanCheckpoint, const RelativePath&)> checkpointHook; };
struct ScanResult { ScanStatus status{ScanStatus::Completed}; std::size_t observationCount{}; std::size_t errorCount{}; std::vector<ScanDiagnostic> diagnostics; ObservationDelivery sinkOutcome{ObservationDelivery::Accepted}; };

class InitialScanner {
public:
    InitialScanner(const IFileSystemView& fileSystem, const IPathSemantics& paths, const IClock& clock, IFileObservationSink& sink)
        : fileSystem_(fileSystem), paths_(paths), clock_(clock), sink_(sink), policy_(paths) {}
    ScanResult scan(const WatchRootConfig& root, const ScanOptions& options = {}) const;
private:
    const IFileSystemView& fileSystem_;
    const IPathSemantics& paths_;
    const IClock& clock_;
    IFileObservationSink& sink_;
    PathPolicy policy_;
};
} // namespace semantic_fs::monitoring