#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <filesystem>

using namespace semantic_fs::monitoring;

namespace {

class CaseInsensitivePaths final : public IPathSemantics {
public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override { return AbsolutePath{std::string(value)}; }
    int compareComponent(std::string_view left, std::string_view right) const override
    {
        if (left.size() != right.size()) return left < right ? -1 : 1;
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto lower = [](char value) { return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value; };
            if (lower(left[index]) != lower(right[index])) return lower(left[index]) < lower(right[index]) ? -1 : 1;
        }
        return 0;
    }
    std::optional<RelativePath> relativeTo(const AbsolutePath&, const AbsolutePath&) const override { return std::nullopt; }
};

class SequentialIds final : public IIdSource {
public:
    std::string nextObservationId() override { return "observation-" + std::to_string(++observation_); }
    std::string nextEventId() override { return "event-" + std::to_string(++event_); }
private:
    int observation_ = 0;
    int event_ = 0;
};

std::filesystem::path temporaryDatabase()
{
    static int sequence = 0;
    const auto path = std::filesystem::temp_directory_path() / ("semantic-fs-sqlite-catalog-outbox-test-" + std::to_string(++sequence) + ".db");
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
    return path;
}

TransitionCommand command(std::string component, std::uint64_t size = 1)
{
    return {*RootId::create("root"), {component, {component, {component}}}, FileMetadata{size, {}, {}}, {100}, ChangeKind::Created, ObservationSource::Watcher, {}};
}

} // namespace

TEST_CASE("SQLite catalog outbox: applies state atomically and uses injected path semantics", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    const auto database = temporaryDatabase();
    auto writer = openSqliteCatalogOutbox(database, paths, ids, {});

    const auto first = writer->apply(command("Report.txt"));
    const auto equivalent = writer->apply(command("report.TXT"));
    const auto changed = writer->apply(command("REPORT.txt", 2));

    REQUIRE(first.status == MutationStatus::Applied);
    REQUIRE(first.eventId->value() == "event-1");
    REQUIRE(equivalent.status == MutationStatus::Equivalent);
    REQUIRE(writer->pendingEventCount() == 2);
    REQUIRE(changed.generation->value == 2);
    auto stale = command("report.txt", 3); stale.expectedGeneration = Generation{1};
    REQUIRE(writer->apply(stale).status == MutationStatus::Invalid);
    REQUIRE(writer->pendingEventCount() == 2);
}

TEST_CASE("SQLite catalog outbox: hard quota rolls back catalog and outbox", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    auto writer = openSqliteCatalogOutbox(temporaryDatabase(), paths, ids, CatalogOutboxConfig{1, 0, 100});

    REQUIRE(writer->apply(command("first.txt")).status == MutationStatus::Applied);
    REQUIRE(writer->apply(command("second.txt")).status == MutationStatus::HardLimited);
    REQUIRE(writer->pendingEventCount() == 1);
    REQUIRE_FALSE(writer->generationFor(*RootId::create("root"), {"second.txt", {"second.txt"}}));
}

TEST_CASE("SQLite catalog outbox: soft quota commits then signals backpressure", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    CatalogOutboxConfig config;
    config.softPendingRows = 1;
    auto writer = openSqliteCatalogOutbox(temporaryDatabase(), paths, ids, config);

    REQUIRE(writer->apply(command("first.txt")).status == MutationStatus::Applied);
    REQUIRE(writer->apply(command("second.txt")).status == MutationStatus::SoftLimited);
    REQUIRE(writer->pendingEventCount() == 2);
}

TEST_CASE("SQLite catalog outbox: commit boundaries recover stable catalog identity", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    const auto database = temporaryDatabase();
    CatalogOutboxConfig before;
    before.failpoint = CatalogOutboxFailpoint::BeforeCommit;
    { auto writer = openSqliteCatalogOutbox(database, paths, ids, before); REQUIRE(writer->apply(command("report.txt")).status == MutationStatus::StorageFailure); }
    { auto writer = openSqliteCatalogOutbox(database, paths, ids, {}); REQUIRE_FALSE(writer->generationFor(*RootId::create("root"), {"report.txt", {"report.txt", {"report.txt"}}})); }
    CatalogOutboxConfig after;
    after.failpoint = CatalogOutboxFailpoint::AfterCommit;
    { auto writer = openSqliteCatalogOutbox(database, paths, ids, after); REQUIRE(writer->apply(command("report.txt")).status == MutationStatus::StorageFailure); }
    auto reopened = openSqliteCatalogOutbox(database, paths, ids, {});
    REQUIRE(reopened->generationFor(*RootId::create("root"), {"REPORT.TXT", {"REPORT.TXT", {"REPORT.TXT"}}})->value == 1);
    REQUIRE(reopened->pendingEventCount() == 1);
}

TEST_CASE("SQLite catalog outbox: preserves its UTC watermark and rejects invalid durable input", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    auto writer = openSqliteCatalogOutbox(temporaryDatabase(), paths, ids, {});
    REQUIRE(writer->apply(command("new.txt")).status == MutationStatus::Applied);
    auto older = command("older.txt"); older.observedAt = {1};
    REQUIRE(writer->apply(older).status == MutationStatus::Applied);
    REQUIRE(writer->lastSeenUtc().microsecondsSinceEpoch == 100);
    auto invalid = command("\xC0");
    REQUIRE(writer->apply(invalid).status == MutationStatus::Invalid);
    CatalogOutboxConfig incompatible;
    incompatible.schemaVersion = 2;
    REQUIRE_THROWS(openSqliteCatalogOutbox(temporaryDatabase(), paths, ids, incompatible));
}

TEST_CASE("SQLite catalog outbox: migrations reject checksum drift without rewriting the database", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    const auto database = temporaryDatabase();
    REQUIRE_NOTHROW(openSqliteCatalogOutbox(database, paths, ids, {}));
    REQUIRE_THROWS(openSqliteCatalogOutbox(database, paths, ids, CatalogOutboxConfig{0, 0, 100, "wrong-checksum"}));
}

TEST_CASE("SQLite catalog outbox: newer migration rows fail closed and remain present", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    const auto database = temporaryDatabase();
    REQUIRE_NOTHROW(openSqliteCatalogOutbox(database, paths, ids, {}));
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(database.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(sqlite3_exec(raw, "UPDATE schema_migrations SET checksum='future' WHERE version=2", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);
    REQUIRE_THROWS(openSqliteCatalogOutbox(database, paths, ids, {}));
    REQUIRE(sqlite3_open(database.string().c_str(), &raw) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(raw, "SELECT checksum FROM schema_migrations WHERE version=2", -1, &statement, nullptr);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    sqlite3_finalize(statement);
    sqlite3_close(raw);
}

TEST_CASE("SQLite catalog outbox: outbox rows retain the file-change schema version", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    const auto database = temporaryDatabase();
    auto writer = openSqliteCatalogOutbox(database, paths, ids, {});
    REQUIRE(writer->apply(command("schema.txt")).status == MutationStatus::Applied);
    sqlite3* raw = nullptr; sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_open(database.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(sqlite3_prepare_v2(raw, "SELECT schema_version FROM event_outbox", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(statement, 0) == static_cast<int>(kFileChangeSchemaVersion));
    sqlite3_finalize(statement); sqlite3_close(raw);
}

TEST_CASE("SQLite catalog outbox: durable dirty obligations and excluded evidence remain explicit", "[sqlite_catalog_outbox]")
{
    CaseInsensitivePaths paths;
    SequentialIds ids;
    auto writer = openSqliteCatalogOutbox(temporaryDatabase(), paths, ids, {});
    const auto dirty = writer->recordCoverage({*RootId::create("root"), 3, 9, {}});
    const auto removed = writer->recordCoverage({*RootId::create("root"), 3, 9, ChangeKind::Removed});

    REQUIRE(dirty.status == CoverageStatus::Persisted);
    REQUIRE(removed.status == CoverageStatus::Deferred);
    REQUIRE(writer->dirtyReasonCount(*RootId::create("root")) == 9);
    REQUIRE(writer->deferredEvidenceCount() == 1);
}
