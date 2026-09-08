#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

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
    static int sequence = 0;
    const auto result = std::filesystem::temp_directory_path() / ("semantic-fs-phase4-u4-" + std::to_string(++sequence) + ".db");
    std::error_code error;
    std::filesystem::remove(result, error);
    return result;
}
TransitionCommand transition(std::string root, std::string path, ChangeKind kind, std::string native, std::uint64_t size = 1)
{
    return {*RootId::create(root), {path, {path, {path}}}, FileMetadata{size, {}, std::move(native)}, {100}, kind, ObservationSource::Reconciliation, {}};
}
std::string scalar(const std::filesystem::path& database, std::string_view sql)
{
    sqlite3* raw = nullptr;
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_open(database.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(sqlite3_prepare_v2(raw, sql.data(), -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    const auto result = std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
    sqlite3_finalize(statement);
    sqlite3_close(raw);
    return result;
}
} // namespace

TEST_CASE("SQLite catalog outbox: atomically persists every transition kind", "[phase4.u4]")
{
    Paths paths;
    Ids ids;
    const auto file = database();
    auto writer = openSqliteCatalogOutbox(file, paths, ids, {});

    REQUIRE(writer->apply(transition("root", "a.txt", ChangeKind::Discovered, "A")).status == MutationStatus::Applied);
    REQUIRE(writer->apply(transition("root", "a.txt", ChangeKind::Modified, "A", 2)).status == MutationStatus::Applied);
    REQUIRE(writer->apply(transition("root", "b.txt", ChangeKind::Renamed, "A", 2)).status == MutationStatus::Applied);
    REQUIRE(writer->apply(transition("root", "b.txt", ChangeKind::Removed, "A", 2)).status == MutationStatus::Applied);
    REQUIRE(writer->apply(transition("root", "c.txt", ChangeKind::Created, "C")).status == MutationStatus::Applied);

    REQUIRE(scalar(file, "SELECT group_concat(kind, ',') FROM event_outbox ORDER BY rowid") == "discovered,modified,renamed,removed,created");
    REQUIRE(scalar(file, "SELECT observation_id FROM observed_files WHERE path=char(31)||'b.txt'") == "observation-1");
    REQUIRE(scalar(file, "SELECT CAST(tombstoned AS TEXT) FROM observed_files WHERE path=char(31)||'b.txt'") == "1");
}

TEST_CASE("SQLite catalog outbox: equivalent, recreate, cross-root, and failure transitions preserve identity rules", "[phase4.u4]")
{
    Paths paths;
    Ids ids;
    const auto file = database();
    auto writer = openSqliteCatalogOutbox(file, paths, ids, {});
    REQUIRE(writer->apply(transition("one", "same.txt", ChangeKind::Created, "A")).status == MutationStatus::Applied);
    REQUIRE(writer->apply(transition("one", "same.txt", ChangeKind::Created, "A")).status == MutationStatus::Equivalent);
    REQUIRE(writer->apply(transition("one", "same.txt", ChangeKind::Removed, "A")).status == MutationStatus::Applied);
    const auto replacement = writer->apply(transition("one", "same.txt", ChangeKind::Created, "B"));
    REQUIRE(replacement.generation->value == 1);
    REQUIRE(writer->generationFor(*RootId::create("one"), transition("one", "same.txt", ChangeKind::Created, "B").path)->value == 1);
    REQUIRE(writer->apply(transition("two", "same.txt", ChangeKind::Discovered, "B")).generation->value == 1);
    REQUIRE(scalar(file, "SELECT count(*) FROM event_outbox") == "4");
    REQUIRE(scalar(file, "SELECT count(*) FROM observed_files WHERE root_id='one'") == "2");
    REQUIRE(scalar(file, "SELECT observation_id FROM observed_files WHERE root_id='two'") == "observation-3");

    CatalogOutboxConfig fail;
    fail.failpoint = CatalogOutboxFailpoint::BeforeCommit;
    Ids failingIds;
    const auto failedFile = database();
    auto failing = openSqliteCatalogOutbox(failedFile, paths, failingIds, fail);
    REQUIRE(failing->apply(transition("root", "rollback.txt", ChangeKind::Created, "R")).status == MutationStatus::StorageFailure);
    REQUIRE(scalar(failedFile, "SELECT count(*) FROM observed_files") == "0");
    REQUIRE(scalar(failedFile, "SELECT count(*) FROM event_outbox") == "0");
}
