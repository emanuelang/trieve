#pragma once

#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_clock.h"
#include "semantic_fs/monitoring/i_file_system_view.h"
#include "semantic_fs/monitoring/i_path_semantics.h"
#include "semantic_fs/monitoring/i_startup_coverage_sink.h"

namespace semantic_fs::monitoring {

class DurableStartupCoverageSink final : public IStartupCoverageSink {
public:
    DurableStartupCoverageSink(
        ICatalogOutboxWriter& writer,
        const IPathSemantics& paths,
        const IFileSystemView& files,
        const IClock& clock,
        WatchRootConfig root);

    CoverageDelivery accept(const StartupCoverage& coverage) override;

private:
    CoverageDelivery apply(const FileObservation& observation, ChangeKind kind);
    CoverageDelivery persist(const CoverageCommand& command);
    CoverageDelivery acceptRecord(const WatcherRecord& record);

    ICatalogOutboxWriter& writer_;
    const IPathSemantics& paths_;
    const IFileSystemView& files_;
    const IClock& clock_;
    WatchRootConfig root_;
};

} // namespace semantic_fs::monitoring
