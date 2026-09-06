#pragma once

#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace semantic_fs::monitoring {
enum class MutationStatus { Applied, Equivalent, SoftLimited, HardLimited, Invalid, StorageFailure };
enum class CatalogOutboxFailpoint { None, BeforeCommit, AfterCommit };
struct CatalogOutboxConfig { std::size_t hardPendingRows = 0; std::size_t hardPayloadBytes = 0; int busyTimeoutMilliseconds = 2500; std::string migrationChecksum = "catalog-outbox-v2"; std::size_t softPendingRows = 0; CatalogOutboxFailpoint failpoint = CatalogOutboxFailpoint::None; std::uint32_t schemaVersion = kFileChangeSchemaVersion; };
struct TransitionCommand { RootId rootId; NormalizedPath path; FileMetadata metadata; UtcTimestamp observedAt; ChangeKind kind; ObservationSource source; std::optional<Generation> expectedGeneration; };
struct MutationResult { MutationStatus status; std::optional<EventId> eventId; std::optional<Generation> generation; };
enum class CoverageStatus { Persisted, Deferred, Refused, StorageFailure };
struct CoverageCommand { RootId rootId; std::uint64_t epoch; std::uint32_t reasons; std::optional<ChangeKind> deferredKind; };
struct CoverageResult { CoverageStatus status; };
class ICatalogOutboxWriter {
public:
    virtual ~ICatalogOutboxWriter() = default;
    virtual MutationResult apply(const TransitionCommand& command) = 0;
    virtual CoverageResult recordCoverage(const CoverageCommand& command) = 0;
    virtual std::size_t pendingEventCount() const = 0;
    virtual UtcTimestamp lastSeenUtc() const = 0;
    virtual std::uint32_t dirtyReasonCount(const RootId&) const = 0;
    virtual std::size_t deferredEvidenceCount() const = 0;
    virtual std::optional<Generation> generationFor(const RootId&, const NormalizedPath&) const = 0;
};
std::unique_ptr<ICatalogOutboxWriter> openSqliteCatalogOutbox(const std::filesystem::path&, const IPathSemantics&, IIdSource&, const CatalogOutboxConfig&);
} // namespace semantic_fs::monitoring
