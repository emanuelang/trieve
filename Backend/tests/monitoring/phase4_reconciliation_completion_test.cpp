#include "semantic_fs/monitoring/reconciliation_service.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

using namespace semantic_fs::monitoring;

namespace {
class Paths final : public IPathSemantics {
public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override { return AbsolutePath{std::string(value)}; }
    int compareComponent(std::string_view left, std::string_view right) const override { return left == right ? 0 : left < right ? -1 : 1; }
    std::optional<RelativePath> relativeTo(const AbsolutePath&, const AbsolutePath&) const override { return std::nullopt; }
};

class Ids final : public IIdSource {
public:
    std::string nextObservationId() override { return "observation-" + std::to_string(++observation); }
    std::string nextEventId() override { return "event-" + std::to_string(++event); }
private:
    unsigned observation{};
    unsigned event{};
};

std::filesystem::path database()
{
    static unsigned sequence{};
    const auto path = std::filesystem::temp_directory_path() / ("semantic-fs-u11-completion-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++sequence) + ".db");
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
    return path;
}

RootId root() { return *RootId::create("root"); }
NormalizedPath path() { return {"a.txt", {"a.txt", {"a.txt"}}}; }
NormalizedPath path(std::string value) { return {value, {value, {value}}}; }
void arm(ICatalogOutboxWriter& writer, GapEpoch epoch, WatcherSequence requiredHighWater, bool finalEntry = true)
{
    REQUIRE(writer.beginReconciliation(root(), epoch, {10}).status == ReconciliationStorageStatus::Started);
    if (finalEntry) REQUIRE(writer.stageReconciliation(root(), epoch, {{path(), FileMetadata{1, {}, {}}, true}}) == ReconciliationStorageStatus::Stored);
    REQUIRE(writer.armReconciliation(root(), epoch, requiredHighWater) == ReconciliationStorageStatus::Stored);
}
void dirty(ICatalogOutboxWriter& writer, GapEpoch epoch)
{
    REQUIRE(writer.recordCoverage({root(), epoch, static_cast<std::uint32_t>(DirtyReason::QueueSaturation), {}}).status == CoverageStatus::Persisted);
}
} // namespace

TEST_CASE("reconciliation completion: atomically pairs final outbox work, completes the run, and clears matching dirty", "[phase4.u11]")
{
    Paths paths;
    Ids ids;
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    dirty(*writer, 5);
    arm(*writer, 5, 7);
    ReconciliationService service;

    REQUIRE(service.step(*writer, root(), 5, {}, {11}).status == ReconciliationStepStatus::AwaitingBarrier);
    REQUIRE(writer->pendingEventCount() == 1);
    REQUIRE(writer->reconciliationFinalOutboxCount(root()) == 0);
    REQUIRE_FALSE(service.accept(*writer, {1, 6, 5}));
    REQUIRE(writer->dirtyReasonCount(root()) != 0);
    REQUIRE(writer->beginReconciliation(root(), 5, {12}).status == ReconciliationStorageStatus::Active);
    REQUIRE(service.accept(*writer, {2, 7, 5}));
    REQUIRE(writer->pendingEventCount() == 1);
    REQUIRE(writer->reconciliationFinalOutboxCount(root()) == 1);
    REQUIRE(writer->dirtyReasonCount(root()) == 0);
    REQUIRE(writer->beginReconciliation(root(), 5, {12}).status == ReconciliationStorageStatus::Started);
    REQUIRE(writer->armReconciliation(root(), 5, 7) == ReconciliationStorageStatus::Stored);
    REQUIRE_FALSE(service.accept(*writer, {3, 9, 5}));
    REQUIRE(writer->beginReconciliation(root(), 5, {13}).status == ReconciliationStorageStatus::Active);
    REQUIRE(writer->dirtyReasonCount(root()) == 0);
}

TEST_CASE("reconciliation completion: stale epoch and pre-commit refusal retain the durable obligation", "[phase4.u11]")
{
    Paths paths;
    Ids ids;
    const auto stalePath = database();
    auto writer = openSqliteCatalogOutbox(stalePath, paths, ids, {});
    dirty(*writer, 6);
    arm(*writer, 6, 11);
    ReconciliationService service;
    REQUIRE(service.step(*writer, root(), 6, {}, {11}).status == ReconciliationStepStatus::AwaitingBarrier);
    dirty(*writer, 7);
    REQUIRE_FALSE(service.accept(*writer, {1, 11, 7}));
    REQUIRE_FALSE(service.accept(*writer, {1, 11, 6}));
    REQUIRE(writer->dirtyReasonCount(root()) != 0);
    REQUIRE(writer->beginReconciliation(root(), 6, {12}).status == ReconciliationStorageStatus::Active);

    const auto refusalPath = database();
    auto refusing = openSqliteCatalogOutbox(refusalPath, paths, ids, {});
    dirty(*refusing, 8);
    arm(*refusing, 8, 13);
    ReconciliationService refused;
    REQUIRE(refused.step(*refusing, root(), 8, {}, {11}).status == ReconciliationStepStatus::AwaitingBarrier);
    refusing.reset();
    refusing = openSqliteCatalogOutbox(refusalPath, paths, ids, {.failpoint = CatalogOutboxFailpoint::BeforeCommit});
    REQUIRE_FALSE(refused.accept(*refusing, {2, 13, 8}));
    REQUIRE(refusing->dirtyReasonCount(root()) != 0);
    auto reopened = openSqliteCatalogOutbox(refusalPath, paths, ids, {});
    const auto active = reopened->beginReconciliation(root(), 8, {12});
    REQUIRE(active.status == ReconciliationStorageStatus::Active);
    REQUIRE(active.run->requiredHighWater == 13);
    REQUIRE(reopened->dirtyReasonCount(root()) != 0);
    REQUIRE(reopened->reconciliationFinalOutboxCount(root()) == 0);
}

TEST_CASE("reconciliation completion: a terminal equivalent page retains the run-wide final outbox identity", "[phase4.u11]")
{
    Paths paths;
    Ids ids;
    const auto file = database();
    auto writer = openSqliteCatalogOutbox(file, paths, ids, {});
    const auto terminal = path("terminal.txt");
    REQUIRE(writer->apply({root(), terminal, FileMetadata{9, {}, {}}, {9}, ChangeKind::Created, ObservationSource::Reconciliation, {}}).status == MutationStatus::Applied);
    dirty(*writer, 9);
    REQUIRE(writer->beginReconciliation(root(), 9, {10}).status == ReconciliationStorageStatus::Started);
    std::vector<ReconciliationStagedObservation> firstPage;
    firstPage.reserve(256);
    for (std::size_t index = 0; index < 256; ++index) firstPage.push_back({path("new-" + std::to_string(index)), FileMetadata{1, {}, {}}, true});
    REQUIRE(writer->stageReconciliation(root(), 9, firstPage) == ReconciliationStorageStatus::Stored);
    REQUIRE(writer->stageReconciliation(root(), 9, {{terminal, FileMetadata{9, {}, {}}, true}}) == ReconciliationStorageStatus::Stored);
    REQUIRE(writer->armReconciliation(root(), 9, 700) == ReconciliationStorageStatus::Stored);
    ReconciliationService service;
    REQUIRE(service.step(*writer, root(), 9, {{terminal, FileMetadata{9, {}, {}}}}, {11}).status == ReconciliationStepStatus::Dirty);
    writer.reset();
    writer = openSqliteCatalogOutbox(file, paths, ids, {});
    ReconciliationService resumed;
    REQUIRE(resumed.step(*writer, root(), 9, {{terminal, FileMetadata{9, {}, {}}}}, {12}).status == ReconciliationStepStatus::AwaitingBarrier);
    REQUIRE(resumed.accept(*writer, {3, 700, 9}));
    REQUIRE(writer->reconciliationFinalOutboxCount(root()) == 1);
    REQUIRE(writer->dirtyReasonCount(root()) == 0);
}

TEST_CASE("reconciliation completion: an after-commit ambiguity replays as the exact committed success", "[phase4.u11]")
{
    Paths paths;
    Ids ids;
    const auto file = database();
    auto writer = openSqliteCatalogOutbox(file, paths, ids, {});
    dirty(*writer, 10);
    arm(*writer, 10, 17);
    ReconciliationService service;
    REQUIRE(service.step(*writer, root(), 10, {}, {11}).status == ReconciliationStepStatus::AwaitingBarrier);
    writer.reset();
    writer = openSqliteCatalogOutbox(file, paths, ids, {.failpoint = CatalogOutboxFailpoint::AfterCommit});
    REQUIRE_FALSE(service.accept(*writer, {4, 17, 10}));
    REQUIRE(writer->reconciliationFinalOutboxCount(root()) == 1);
    REQUIRE(writer->dirtyReasonCount(root()) == 0);
    REQUIRE_FALSE(service.accept(*writer, {5, 17, 10}));
    REQUIRE(service.accept(*writer, {4, 17, 10}));
    writer.reset();
    writer = openSqliteCatalogOutbox(file, paths, ids, {});
    REQUIRE(service.accept(*writer, {4, 17, 10}));
    REQUIRE(writer->reconciliationFinalOutboxCount(root()) == 1);
    REQUIRE(writer->dirtyReasonCount(root()) == 0);
}
