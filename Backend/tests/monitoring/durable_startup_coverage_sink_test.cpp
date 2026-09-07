#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/durable_startup_coverage_sink.h"
#include "semantic_fs/monitoring/i_id_source.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {

class RecordingWriter final : public ICatalogOutboxWriter {
public:
    MutationResult apply(const TransitionCommand& command) override
    {
        transitions.push_back(command);
        return {mutationStatus, {}, {Generation{1}}};
    }

    CoverageResult recordCoverage(const CoverageCommand& command) override
    {
        coverage.push_back(command);
        return {coverageStatus};
    }

    RecoveryResult recover(const RecoveryRequest&) override { return {RecoveryStatus::Ready, 0, {}}; }
    ClaimResult claim(const ClaimRequest&) override { return {ClaimStatus::Empty, {}}; }
    DeliveryStatus complete(const DeliveryCommand&) override { return DeliveryStatus::Updated; }
    std::size_t pendingEventCount() const override { return 0; }
    UtcTimestamp lastSeenUtc() const override { return {}; }
    std::uint32_t dirtyReasonCount(const RootId&) const override { return 0; }
    std::size_t deferredEvidenceCount() const override { return 0; }
    std::optional<Generation> generationFor(const RootId&, const NormalizedPath&) const override { return std::nullopt; }

    MutationStatus mutationStatus{MutationStatus::Applied};
    CoverageStatus coverageStatus{CoverageStatus::Persisted};
    std::vector<TransitionCommand> transitions;
    std::vector<CoverageCommand> coverage;
};

WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }

FileObservation discovered()
{
    return {root().rootId, {"/root/a.txt", {"a.txt", {"a.txt"}}}, FileMetadata{7, {}, {}}, {100}, ObservationKind::Discovered, ObservationSource::InitialScan};
}

class SequentialIds final : public IIdSource {
public:
    std::string nextObservationId() override { return "observation-" + std::to_string(++observation_); }
    std::string nextEventId() override { return "event-" + std::to_string(++event_); }
private:
    int observation_{};
    int event_{};
};

std::filesystem::path temporaryDatabase()
{
    static int sequence{};
    const auto path = std::filesystem::temp_directory_path() / ("semantic-fs-durable-coverage-test-" + std::to_string(++sequence) + ".db");
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
    return path;
}

} // namespace

TEST_CASE("Durable startup coverage sink routes discovered, created, and modified coverage", "[durable_startup_coverage_sink]")
{
    FixturePaths paths;
    FixtureView files;
    files.metadataResults["/root/created.txt"] = FileMetadata{3, {}, {}};
    files.metadataResults["/root/modified.txt"] = FileMetadata{4, {}, {}};
    FixtureClock clock;
    RecordingWriter writer;
    DurableStartupCoverageSink sink(writer, paths, files, clock, root());

    REQUIRE(sink.accept(discovered()) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(WatcherRecord{1, {root().rootId, WatcherEventKind::Created, {"/root/created.txt"}, {}}}) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(WatcherRecord{2, {root().rootId, WatcherEventKind::Modified, {"/root/modified.txt"}, {}}}) == CoverageDelivery::Accepted);
    REQUIRE(writer.transitions.size() == 3);
    REQUIRE(writer.transitions[0].kind == ChangeKind::Discovered);
    REQUIRE(writer.transitions[1].kind == ChangeKind::Created);
    REQUIRE(writer.transitions[2].kind == ChangeKind::Modified);
    REQUIRE(writer.transitions[1].path.relative.utf8 == "created.txt");
    REQUIRE(writer.transitions[2].observedAt.microsecondsSinceEpoch == 42);
}

TEST_CASE("Durable startup coverage sink persists overflow and retains excluded evidence", "[durable_startup_coverage_sink]")
{
    FixturePaths paths;
    FixtureView files;
    FixtureClock clock;
    RecordingWriter writer;
    DurableStartupCoverageSink sink(writer, paths, files, clock, root());

    REQUIRE(sink.accept(RootDirty{root().rootId, 4, static_cast<std::uint32_t>(DirtyReason::OsOverflow)}) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(PendingReconciliation{root().rootId, 5, static_cast<std::uint32_t>(DirtyReason::QueueSaturation)}) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(WatcherRecord{3, {root().rootId, WatcherEventKind::Overflow, {"/root"}, {}}}) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(WatcherRecord{4, {root().rootId, WatcherEventKind::Removed, {"/root/gone.txt"}, {}}}) == CoverageDelivery::Refused);
    REQUIRE(sink.accept(WatcherRecord{5, {root().rootId, WatcherEventKind::Renamed, {"/root/new.txt"}, AbsolutePath{"/root/old.txt"}}}) == CoverageDelivery::Refused);
    REQUIRE(writer.coverage.size() == 5);
    REQUIRE_FALSE(writer.coverage[0].deferredKind);
    REQUIRE_FALSE(writer.coverage[1].deferredKind);
    REQUIRE_FALSE(writer.coverage[2].deferredKind);
    REQUIRE(writer.coverage[3].deferredKind == ChangeKind::Removed);
    REQUIRE(writer.coverage[4].deferredKind == ChangeKind::Renamed);
}

TEST_CASE("Durable startup coverage sink keeps native callback boundaries queue-only", "[durable_startup_coverage_sink]")
{
    FixturePaths paths;
    FixtureView files;
    FixtureClock clock;
    RecordingWriter writer;
    DurableStartupCoverageSink sink(writer, paths, files, clock, root());

    REQUIRE(sink.accept(BarrierReached{9, 12, 3}) == CoverageDelivery::Accepted);
    REQUIRE(writer.transitions.empty());
    REQUIRE(writer.coverage.empty());
}

TEST_CASE("Durable startup coverage sink persists Phase-2 coverage without publishing", "[durable_startup_coverage_sink]")
{
    FixturePaths paths;
    FixtureView files;
    FixtureClock clock;
    SequentialIds ids;
    auto writer = openSqliteCatalogOutbox(temporaryDatabase(), paths, ids, {});
    DurableStartupCoverageSink sink(*writer, paths, files, clock, root());

    REQUIRE(sink.accept(discovered()) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(RootDirty{root().rootId, 5, static_cast<std::uint32_t>(DirtyReason::OsOverflow)}) == CoverageDelivery::Accepted);
    REQUIRE(sink.accept(WatcherRecord{6, {root().rootId, WatcherEventKind::Removed, {"/root/old.txt"}, {}}}) == CoverageDelivery::Refused);
    REQUIRE(writer->pendingEventCount() == 1);
    REQUIRE((writer->dirtyReasonCount(root().rootId) & static_cast<std::uint32_t>(DirtyReason::OsOverflow)) != 0);
    REQUIRE(writer->deferredEvidenceCount() == 1);
}
