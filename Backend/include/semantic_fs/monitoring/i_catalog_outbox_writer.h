#pragma once

#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace semantic_fs::monitoring {
enum class MutationStatus { Applied, Equivalent, SoftLimited, HardLimited, Invalid, StorageFailure };
enum class CatalogOutboxFailpoint { None, BeforeCommit, AfterCommit };
struct CatalogOutboxConfig { std::size_t hardPendingRows = 0; std::size_t hardPayloadBytes = 0; int busyTimeoutMilliseconds = 2500; std::string migrationChecksum = "catalog-outbox-v4-root-path-metadata"; std::size_t softPendingRows = 0; CatalogOutboxFailpoint failpoint = CatalogOutboxFailpoint::None; std::uint32_t schemaVersion = kFileChangeSchemaVersion; };
struct TransitionCommand { RootId rootId; NormalizedPath path; FileMetadata metadata; UtcTimestamp observedAt; ChangeKind kind; ObservationSource source; std::optional<Generation> expectedGeneration; };
struct MutationResult { MutationStatus status; std::optional<EventId> eventId; std::optional<Generation> generation; };
enum class ReconciliationStorageStatus { Started, Active, Busy, Stored, Invalid, StorageFailure };
struct ReconciliationStagedObservation { NormalizedPath path; FileMetadata metadata; bool completedSubtree = false; };
struct ReconciliationRun { RootId rootId; GapEpoch capturedEpoch; std::size_t scanCursor; std::size_t finalizeCursor; bool scanArmed; };
struct ReconciliationRead { ReconciliationStorageStatus status; std::optional<ReconciliationRun> run; std::vector<ReconciliationStagedObservation> staged; };
enum class CoverageStatus { Persisted, Deferred, Refused, StorageFailure };
struct CoverageCommand { RootId rootId; std::uint64_t epoch; std::uint32_t reasons; std::optional<ChangeKind> deferredKind; };
struct CoverageResult { CoverageStatus status; };
enum class RecoveryStatus { Ready, ClockRollback, Corrupt, StorageFailure };
enum class ClaimStatus { Claimed, Empty, ClockRollback, Corrupt, StorageFailure };
enum class DeliveryStatus { Updated, StaleLease, Invalid, StorageFailure };
struct PendingEventSummary { EventId eventId; UtcTimestamp availableAt; std::uint32_t attempts; };
struct TerminalDiagnostic { EventId eventId; std::string message; };
struct RecoveryRequest { UtcTimestamp now; std::size_t limit; };
struct RecoveryResult { RecoveryStatus status; std::size_t reclaimed; std::vector<PendingEventSummary> pending; std::vector<TerminalDiagnostic> terminalDiagnostics; };
struct StoreRuntimeSettings { std::string journalMode; bool foreignKeysEnabled = false; bool synchronousFull = false; int busyTimeoutMilliseconds = 0; };
struct ClaimRequest { std::string owner; UtcTimestamp now; UtcTimestamp duration; std::size_t limit; };
struct ClaimedEvent { FileChange change; std::string token; UtcTimestamp deadline; };
struct ClaimResult { ClaimStatus status; std::vector<ClaimedEvent> events; };
struct DeliveryCommand { EventId eventId; std::string token; PublishResult outcome; UtcTimestamp availableAt; std::string diagnostic; };
class ICatalogOutboxWriter {
public:
    virtual ~ICatalogOutboxWriter() = default;
    virtual MutationResult apply(const TransitionCommand& command) = 0;
    virtual ReconciliationRead beginReconciliation(const RootId&, GapEpoch, UtcTimestamp) { return {ReconciliationStorageStatus::StorageFailure, {}, {}}; }
    virtual ReconciliationStorageStatus stageReconciliation(const RootId&, GapEpoch, const std::vector<ReconciliationStagedObservation>&) { return ReconciliationStorageStatus::StorageFailure; }
    virtual ReconciliationStorageStatus advanceReconciliation(const RootId&, GapEpoch, std::size_t, bool) { return ReconciliationStorageStatus::StorageFailure; }
    virtual CoverageResult recordCoverage(const CoverageCommand& command) = 0;
    virtual RecoveryResult recover(const RecoveryRequest&) = 0;
    virtual ClaimResult claim(const ClaimRequest&) = 0;
    virtual DeliveryStatus complete(const DeliveryCommand&) = 0;
    virtual std::size_t pendingEventCount() const = 0;
    virtual UtcTimestamp lastSeenUtc() const = 0;
    virtual std::uint32_t dirtyReasonCount(const RootId&) const = 0;
    virtual std::size_t deferredEvidenceCount() const = 0;
    virtual std::optional<Generation> generationFor(const RootId&, const NormalizedPath&) const = 0;
    virtual StoreRuntimeSettings runtimeSettings() const { return {}; }
};
std::unique_ptr<ICatalogOutboxWriter> openSqliteCatalogOutbox(const std::filesystem::path&, const IPathSemantics&, IIdSource&, const CatalogOutboxConfig&);
} // namespace semantic_fs::monitoring
