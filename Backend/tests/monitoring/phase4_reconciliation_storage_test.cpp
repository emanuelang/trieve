#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
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
        REQUIRE(writer->advanceReconciliation(root, 7, 1, true) == ReconciliationStorageStatus::Stored);
    }
    auto reopened = openSqliteCatalogOutbox(file, paths, ids, {});
    const auto resumed = reopened->beginReconciliation(root, 7, {101});
    REQUIRE(resumed.status == ReconciliationStorageStatus::Active);
    REQUIRE(resumed.run->scanCursor == 2);
    REQUIRE(resumed.run->finalizeCursor == 1);
    REQUIRE(resumed.run->scanComplete);
    REQUIRE(resumed.staged.size() == 1);
    REQUIRE(resumed.staged.front().path.displayUtf8 == "b");
    REQUIRE_FALSE(resumed.staged.front().completedSubtree);
    REQUIRE(reopened->beginReconciliation(root, 8, {102}).status == ReconciliationStorageStatus::Busy);
}
