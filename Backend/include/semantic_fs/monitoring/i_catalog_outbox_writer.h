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
struct ReconciliationRun { RootId rootId; GapEpoch capturedEpoch; std::size_t scanCursor; std::size_t finalizeCursor; bool scanArmed; std::string runId; std::optional<WatcherSequence> requiredHighWater; std::optional<EventId> finalOutboxEvent; };
struct ReconciliationRead { ReconciliationStorageStatus status; std::optional<ReconciliationRun> run; std::vector<ReconciliationStagedObservation> staged; };
struct ReconciliationCompletion { RootId rootId; std::string runId; GapEpoch epoch; BarrierReached barrier; std::optional<EventId> finalOutboxEvent; WatcherSequence requiredHighWater; };
enum class CoverageStatus { Persisted, Deferred, Refused, StorageFailure };
struct CoverageCommand { RootId rootId; std::uint64_t epoch; std::uint32_t reasons; std::optional<ChangeKind> deferredKind; };
struct CoverageResult { CoverageStatus status; };
enum class RecoveryStatus { Ready, ClockRollback, Corrupt, StorageFailure };
enum class ClaimStatus { Claimed, Empty, ClockRollback, Corrupt, StorageFailure };
enum class DeliveryStatus { Updated, StaleLease, Invalid, StorageFailure };
enum class RetryQueryStatus { Found, Empty, StorageFailure };
struct PendingEventSummary { EventId eventId; UtcTimestamp availableAt; std::uint32_t attempts; };
struct RetryQueryResult { RetryQueryStatus status; std::optional<UtcTimestamp> availableAt; };
struct TerminalDiagnostic { EventId eventId; std::string message; };
struct RecoveryRequest { UtcTimestamp now; std::size_t limit; };
struct RecoveryResult { RecoveryStatus status; std::size_t reclaimed; std::vector<PendingEventSummary> pending; std::vector<TerminalDiagnostic> terminalDiagnostics; };
struct StoreRuntimeSettings { std::string journalMode; bool foreignKeysEnabled = false; bool synchronousFull = false; int busyTimeoutMilliseconds = 0; };
struct ClaimRequest { std::string owner; UtcTimestamp now; UtcTimestamp duration; std::size_t limit; };
struct ClaimedEvent { FileChange change; std::string token; UtcTimestamp deadline; };
struct ClaimResult { ClaimStatus status; std::vector<ClaimedEvent> events; };
struct DeliveryCommand { EventId eventId; std::string token; PublishResult outcome; UtcTimestamp availableAt; std::string diagnostic; };
class IReconciliationObligationReader {
public:
    virtual ~IReconciliationObligationReader() = default;
    virtual std::optional<PendingReconciliation> pendingReconciliation(const RootId&) const = 0;
};
class ICatalogOutboxWriter : public IReconciliationObligationReader {
public:
    virtual ~ICatalogOutboxWriter() = default;
    virtual MutationResult apply(const TransitionCommand& command) = 0;
    virtual MutationResult applyReconciliation(const std::string&, const TransitionCommand& command) { return apply(command); }
    virtual ReconciliationRead beginReconciliation(const RootId&, GapEpoch, UtcTimestamp) { return {ReconciliationStorageStatus::StorageFailure, {}, {}}; }
    virtual ReconciliationStorageStatus stageReconciliation(const RootId&, GapEpoch, const std::vector<ReconciliationStagedObservation>&) { return ReconciliationStorageStatus::StorageFailure; }
    virtual ReconciliationStorageStatus armReconciliation(const RootId&, GapEpoch, WatcherSequence) { return ReconciliationStorageStatus::StorageFailure; }
    virtual ReconciliationStorageStatus advanceReconciliation(const RootId&, GapEpoch, std::size_t, bool) { return ReconciliationStorageStatus::StorageFailure; }
    virtual ReconciliationStorageStatus completeReconciliation(const ReconciliationCompletion&) { return ReconciliationStorageStatus::StorageFailure; }
    virtual std::size_t reconciliationFinalOutboxCount(const RootId&) const { return 0; }
    virtual CoverageResult recordCoverage(const CoverageCommand& command) = 0;
    virtual RecoveryResult recover(const RecoveryRequest&) = 0;
    virtual ClaimResult claim(const ClaimRequest&) = 0;
    virtual DeliveryStatus complete(const DeliveryCommand&) = 0;
    virtual RetryQueryResult earliestPendingRetry() const { return {RetryQueryStatus::StorageFailure, {}}; }
    virtual std::size_t pendingEventCount() const = 0;
    virtual UtcTimestamp lastSeenUtc() const = 0;
    virtual std::uint32_t dirtyReasonCount(const RootId&) const = 0;
    std::optional<PendingReconciliation> pendingReconciliation(const RootId&) const override { return {}; }
    virtual std::size_t deferredEvidenceCount() const = 0;
    virtual std::optional<Generation> generationFor(const RootId&, const NormalizedPath&) const = 0;
    virtual StoreRuntimeSettings runtimeSettings() const { return {}; }
};
std::unique_ptr<ICatalogOutboxWriter> openSqliteCatalogOutbox(const std::filesystem::path&, const IPathSemantics&, IIdSource&, const CatalogOutboxConfig&);
} // namespace semantic_fs::monitoring
