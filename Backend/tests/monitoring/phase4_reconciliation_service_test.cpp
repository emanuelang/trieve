#include "semantic_fs/monitoring/reconciliation_service.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <catch2/catch_test_macros.hpp>

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
    int observation = 0;
    int event = 0;
};
std::filesystem::path database() { const auto path = std::filesystem::temp_directory_path() / "semantic-fs-phase4-u5b.db"; std::error_code error; std::filesystem::remove(path, error); return path; }
NormalizedPath path(std::string value) { RelativePath relative{value, {}}; for (std::size_t start = 0; start < value.size();) { const auto end = value.find('/', start); relative.components.emplace_back(value.substr(start, end == std::string::npos ? value.size() - start : end - start)); if (end == std::string::npos) break; start = end + 1; } return {value, std::move(relative)}; }
TransitionCommand transition(const RootId& root, std::string value) { return {root, path(std::move(value)), {}, {1}, ChangeKind::Created, ObservationSource::Reconciliation, {}}; }
} // namespace

TEST_CASE("reconciliation service: completed coverage fences tombstones", "[phase4.u5b]")
{
    Paths paths;
    Ids ids;
    const auto root = *RootId::create("root");
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    REQUIRE(writer->apply(transition(root, "dir/gone")).status == MutationStatus::Applied);
    REQUIRE(writer->apply(transition(root, "unsafe/gone")).status == MutationStatus::Applied);
    REQUIRE(writer->beginReconciliation(root, 7, {10}).status == ReconciliationStorageStatus::Started);
    REQUIRE(writer->stageReconciliation(root, 7, {{{path("dir"), {}, true}, {path("unsafe"), {}, false}}}) == ReconciliationStorageStatus::Stored);
    REQUIRE(writer->stageReconciliation(root, 7, {}) == ReconciliationStorageStatus::Stored);
    ReconciliationService service;
    const auto result = service.step(*writer, root, 7, {{path("dir/gone"), {}}, {path("unsafe/gone"), {}}}, {11});
    REQUIRE(result.status == ReconciliationStepStatus::AwaitingBarrier);
    REQUIRE(result.applied == 3);
    REQUIRE_FALSE(writer->generationFor(root, path("dir/gone")).has_value());
    REQUIRE(writer->generationFor(root, path("unsafe/gone")).has_value());
    REQUIRE(writer->pendingEventCount() == 5);
    REQUIRE_FALSE(service.accept({1, 1, 8}));
    REQUIRE_FALSE(service.dirtyClearEligible());
}

TEST_CASE("reconciliation service: only a matching post-scan barrier permits dirty clear", "[phase4.u5b]")
{
    Paths paths;
    Ids ids;
    const auto root = *RootId::create("root");
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    ReconciliationService service;
    REQUIRE(writer->beginReconciliation(root, 4, {20}).status == ReconciliationStorageStatus::Started);
    REQUIRE(writer->stageReconciliation(root, 4, {}) == ReconciliationStorageStatus::Stored);
    REQUIRE(service.step(*writer, root, 4, {}, {20}).status == ReconciliationStepStatus::AwaitingBarrier);
    REQUIRE(service.accept({2, 2, 4}));
    REQUIRE(service.dirtyClearEligible());
}

TEST_CASE("reconciliation service: an unarmed empty run cannot clear dirty", "[phase4.u5b]")
{
    Paths paths;
    Ids ids;
    const auto root = *RootId::create("root");
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    ReconciliationService service;
    REQUIRE(service.step(*writer, root, 4, {}, {20}).status == ReconciliationStepStatus::Dirty);
    REQUIRE_FALSE(service.accept({2, 2, 4}));
    REQUIRE_FALSE(service.dirtyClearEligible());
}

TEST_CASE("reconciliation service: an incomplete paged finalization cannot clear dirty", "[phase4.u5b]")
{
    Paths paths;
    Ids ids;
    const auto root = *RootId::create("root");
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    REQUIRE(writer->beginReconciliation(root, 9, {20}).status == ReconciliationStorageStatus::Started);
    std::vector<ReconciliationStagedObservation> firstPage;
    firstPage.reserve(256);
    for (std::size_t index = 0; index < 256; ++index) firstPage.push_back({path("page/" + std::to_string(index)), {}, true});
    REQUIRE(writer->stageReconciliation(root, 9, firstPage) == ReconciliationStorageStatus::Stored);
    REQUIRE(writer->stageReconciliation(root, 9, {{path("page/256"), {}, true}}) == ReconciliationStorageStatus::Stored);
    REQUIRE(writer->stageReconciliation(root, 9, {}) == ReconciliationStorageStatus::Stored);
    ReconciliationService service;
    REQUIRE(service.step(*writer, root, 9, {}, {21}).status == ReconciliationStepStatus::Dirty);
    REQUIRE_FALSE(service.accept({2, 2, 9}));
    REQUIRE_FALSE(service.dirtyClearEligible());
}

TEST_CASE("reconciliation service: page finalization waits for terminal scan acceptance", "[phase4.u5b]")
{
    Paths paths;
    Ids ids;
    const auto root = *RootId::create("root");
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    REQUIRE(writer->beginReconciliation(root, 10, {30}).status == ReconciliationStorageStatus::Started);
    std::vector<ReconciliationStagedObservation> firstPage;
    firstPage.reserve(256);
    for (std::size_t index = 0; index < 256; ++index) firstPage.push_back({path("interleaved/" + std::to_string(index)), {}, true});
    REQUIRE(writer->stageReconciliation(root, 10, firstPage) == ReconciliationStorageStatus::Stored);
    ReconciliationService service;
    REQUIRE(service.step(*writer, root, 10, {}, {31}).status == ReconciliationStepStatus::Dirty);
    REQUIRE_FALSE(service.accept({3, 3, 10}));
    REQUIRE(writer->stageReconciliation(root, 10, {{path("interleaved/256"), {}, true}}) == ReconciliationStorageStatus::Stored);
    REQUIRE(service.step(*writer, root, 10, {}, {32}).status == ReconciliationStepStatus::Dirty);
    REQUIRE_FALSE(service.accept({3, 3, 10}));
    REQUIRE(writer->stageReconciliation(root, 10, {}) == ReconciliationStorageStatus::Stored);
    REQUIRE(service.step(*writer, root, 10, {}, {33}).status == ReconciliationStepStatus::AwaitingBarrier);
    REQUIRE(service.accept({3, 3, 10}));
    REQUIRE(service.dirtyClearEligible());
}
