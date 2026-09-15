#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"
#include "semantic_fs/monitoring/reconciliation_service.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>

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
std::filesystem::path database()
{
    const auto path = std::filesystem::temp_directory_path() / "semantic-fs-phase4-u5a.db";
    std::error_code error;
    std::filesystem::remove(path, error);
    return path;
}
ReconciliationStagedObservation observation(std::string path, bool complete = true)
{
    return {{path, {path, {path}}}, FileMetadata{}, complete};
}
NormalizedPath path(std::string value)
{
    RelativePath relative{value, {}};
    for (std::size_t start = 0; start < value.size();) {
        const auto end = value.find('/', start);
        relative.components.emplace_back(value.substr(start, end == std::string::npos ? value.size() - start : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {value, std::move(relative)};
}
} // namespace

TEST_CASE("SQLite reconciliation storage: stages bounded cursor state across reopen", "[phase4.u5a]")
{
    Paths paths;
    Ids ids;
    const auto file = database();
    const auto root = *RootId::create("root");
    {
        auto writer = openSqliteCatalogOutbox(file, paths, ids, {});
        REQUIRE(writer->beginReconciliation(root, 7, {100}).status == ReconciliationStorageStatus::Started);
        std::vector<ReconciliationStagedObservation> oversized(257, observation("over"));
        REQUIRE(writer->stageReconciliation(root, 7, oversized) == ReconciliationStorageStatus::Invalid);
        REQUIRE(writer->stageReconciliation(root, 7, {observation("a"), observation("b", false)}) == ReconciliationStorageStatus::Stored);
        const auto staged = writer->beginReconciliation(root, 7, {100});
        REQUIRE(staged.staged.size() == 2);
        REQUIRE(staged.staged.front().completedSubtree);
        REQUIRE(writer->advanceReconciliation(root, 7, 1, true) != ReconciliationStorageStatus::Stored);
        REQUIRE(writer->advanceReconciliation(root, 7, 1, false) == ReconciliationStorageStatus::Stored);
        REQUIRE(writer->advanceReconciliation(root, 7, 0, false) != ReconciliationStorageStatus::Stored);
        REQUIRE(writer->advanceReconciliation(root, 7, 2, false) == ReconciliationStorageStatus::Stored);
        REQUIRE(writer->advanceReconciliation(root, 7, 2, true) != ReconciliationStorageStatus::Stored);
        REQUIRE(writer->stageReconciliation(root, 7, {}) == ReconciliationStorageStatus::Stored);
        REQUIRE(writer->advanceReconciliation(root, 7, 2, true) == ReconciliationStorageStatus::Stored);
    }
    auto reopened = openSqliteCatalogOutbox(file, paths, ids, {});
    const auto resumed = reopened->beginReconciliation(root, 7, {101});
    REQUIRE(resumed.status == ReconciliationStorageStatus::Active);
    REQUIRE(resumed.run->scanCursor == 2);
    REQUIRE(resumed.run->finalizeCursor == 2);
    REQUIRE(resumed.run->scanArmed);
    REQUIRE(resumed.staged.empty());
    REQUIRE(reopened->beginReconciliation(root, 8, {102}).status == ReconciliationStorageStatus::Busy);
}

TEST_CASE("SQLite reconciliation storage: catalog pages are bounded and resume across reopen", "[phase4.u13]")
{
    Paths paths;
    Ids ids;
    const auto file = database();
    const auto root = *RootId::create("root");
    std::size_t cursor = 0;
    std::set<std::string> catalogPaths;
    {
        auto writer = openSqliteCatalogOutbox(file, paths, ids, {});
        for (std::size_t index = 0; index < 257; ++index)
            REQUIRE(writer->apply({root, path("catalog/" + std::to_string(index)), {}, {1}, ChangeKind::Created, ObservationSource::Reconciliation, {}}).status == MutationStatus::Applied);
        REQUIRE(writer->apply({root, path("unsafe/gone"), {}, {1}, ChangeKind::Created, ObservationSource::Reconciliation, {}}).status == MutationStatus::Applied);

        const auto first = writer->catalogPage(root, cursor);
        REQUIRE(first.status == ReconciliationStorageStatus::Stored);
        REQUIRE(first.entries.size() == 256);
        REQUIRE_FALSE(first.complete);
        REQUIRE(first.cursor > cursor);
        for (const auto& entry : first.entries) catalogPaths.insert(entry.path.displayUtf8);
        REQUIRE(catalogPaths.size() == 256);
        REQUIRE(catalogPaths.contains("catalog/0"));
        REQUIRE(catalogPaths.contains("catalog/255"));
        cursor = first.cursor;
    }

    auto reopened = openSqliteCatalogOutbox(file, paths, ids, {});
    const auto second = reopened->catalogPage(root, cursor);
    REQUIRE(second.status == ReconciliationStorageStatus::Stored);
    REQUIRE(second.entries.size() == 2);
    REQUIRE(second.complete);
    REQUIRE(second.entries[0].path.displayUtf8 == "catalog/256");
    REQUIRE(second.entries[1].path.displayUtf8 == "unsafe/gone");
    for (const auto& entry : second.entries) catalogPaths.insert(entry.path.displayUtf8);
    REQUIRE(catalogPaths.size() == 258);
    for (std::size_t index = 0; index < 257; ++index) REQUIRE(catalogPaths.contains("catalog/" + std::to_string(index)));
    REQUIRE(catalogPaths.contains("unsafe/gone"));

    REQUIRE(reopened->beginReconciliation(root, 9, {2}).status == ReconciliationStorageStatus::Started);
    REQUIRE(reopened->stageReconciliation(root, 9, {{path("unsafe"), {}, false}}) == ReconciliationStorageStatus::Stored);
    REQUIRE(reopened->armReconciliation(root, 9, 3) == ReconciliationStorageStatus::Stored);
    ReconciliationService service;
    REQUIRE(service.stepPage(*reopened, root, 9, second, {3}).status == ReconciliationStepStatus::AwaitingBarrier);
    REQUIRE(reopened->generationFor(root, path("unsafe/gone")).has_value());
}
