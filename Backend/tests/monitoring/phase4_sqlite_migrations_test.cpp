#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <string>

using namespace semantic_fs::monitoring;

namespace {

class ExactPaths final : public IPathSemantics {
public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override { return AbsolutePath{std::string(value)}; }
    int compareComponent(std::string_view left, std::string_view right) const override { return left == right ? 0 : left < right ? -1 : 1; }
    std::optional<RelativePath> relativeTo(const AbsolutePath&, const AbsolutePath&) const override { return std::nullopt; }
};

class FixedIds final : public IIdSource {
public:
    std::string nextObservationId() override { return "new-observation"; }
    std::string nextEventId() override { return "new-event"; }
};

std::filesystem::path databasePath()
{
    static unsigned sequence = 0;
    const auto path = std::filesystem::temp_directory_path() / ("semantic-fs-phase4-v4-" + std::to_string(++sequence) + ".db");
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
    return path;
}

void execute(sqlite3* database, const char* sql)
{
    char* error = nullptr;
    const auto result = sqlite3_exec(database, sql, nullptr, nullptr, &error);
    const std::string message = error ? error : "";
    sqlite3_free(error);
    REQUIRE(result == SQLITE_OK);
    INFO(message);
}

sqlite3* createV3Database(const std::filesystem::path& path)
{
    sqlite3* database = nullptr;
    REQUIRE(sqlite3_open(path.string().c_str(), &database) == SQLITE_OK);
    execute(database, "CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY, checksum TEXT NOT NULL);"
                      "INSERT INTO schema_migrations VALUES(1,'catalog-outbox-v1'),(2,'catalog-outbox-v2'),(3,'catalog-outbox-v3');"
                      "CREATE TABLE observed_files(observation_id TEXT PRIMARY KEY, root_id TEXT NOT NULL, path TEXT NOT NULL, generation INTEGER NOT NULL, size INTEGER, modified_at INTEGER);"
                      "INSERT INTO observed_files VALUES('old-observation','root','\x1freport.txt',7,12,NULL);"
                      "INSERT INTO observed_files VALUES('catalog-observation','catalog-root','\x1f" "catalog.txt',1,1,NULL);"
                      "CREATE TABLE event_outbox(event_id TEXT PRIMARY KEY, observation_id TEXT NOT NULL, generation INTEGER NOT NULL, payload TEXT NOT NULL, state TEXT NOT NULL, schema_version INTEGER NOT NULL DEFAULT 1, available_at INTEGER NOT NULL DEFAULT 0, attempts INTEGER NOT NULL DEFAULT 0, lease_owner TEXT, lease_token TEXT, lease_deadline INTEGER, diagnostic TEXT, kind TEXT NOT NULL DEFAULT 'created', observed_at INTEGER NOT NULL DEFAULT 0, source INTEGER NOT NULL DEFAULT 0);"
                      "INSERT INTO event_outbox(event_id,observation_id,generation,payload,state,schema_version,kind) VALUES('old-event','old-observation',7,'report.txt','pending',1,'created');"
                      "CREATE TABLE store_runtime(singleton INTEGER PRIMARY KEY, last_seen_utc INTEGER NOT NULL);"
                      "CREATE TABLE dirty_obligations(root_id TEXT PRIMARY KEY, epoch INTEGER NOT NULL, reasons INTEGER NOT NULL);"
                      "INSERT INTO dirty_obligations VALUES('root',9,5);"
                      "INSERT INTO dirty_obligations VALUES('dirty-only-root',4,2);"
                      "CREATE TABLE deferred_coverage(id INTEGER PRIMARY KEY, root_id TEXT NOT NULL, kind TEXT NOT NULL);");
    return database;
}

bool hasTable(sqlite3* database, std::string_view name)
{
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &statement, nullptr) == SQLITE_OK);
    sqlite3_bind_text(statement, 1, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

bool hasColumn(sqlite3* database, std::string_view table, std::string_view column)
{
    sqlite3_stmt* statement = nullptr;
    const auto query = "PRAGMA table_info(" + std::string(table) + ")";
    REQUIRE(sqlite3_prepare_v2(database, query.c_str(), -1, &statement, nullptr) == SQLITE_OK);
    while (sqlite3_step(statement) == SQLITE_ROW) if (column == reinterpret_cast<const char*>(sqlite3_column_text(statement, 1))) { sqlite3_finalize(statement); return true; }
    sqlite3_finalize(statement);
    return false;
}

void requireRejected(sqlite3* database, const char* sql)
{
    char* error = nullptr;
    const auto result = sqlite3_exec(database, sql, nullptr, nullptr, &error);
    sqlite3_free(error);
    REQUIRE(result != SQLITE_OK);
}

std::string queryPlan(sqlite3* database, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    const std::string detail = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
    sqlite3_finalize(statement);
    return detail;
}

int scalar(sqlite3* database, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    const auto value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

std::int64_t scalar64(sqlite3* database, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    const auto value = static_cast<std::int64_t>(sqlite3_column_int64(statement, 0));
    sqlite3_finalize(statement);
    return value;
}

std::string text(sqlite3* database, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    const std::string value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    sqlite3_finalize(statement);
    return value;
}

bool isNullable(sqlite3* database, std::string_view table, std::string_view column)
{
    sqlite3_stmt* statement = nullptr;
    const auto query = "PRAGMA table_info(" + std::string(table) + ")";
    REQUIRE(sqlite3_prepare_v2(database, query.c_str(), -1, &statement, nullptr) == SQLITE_OK);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (column == reinterpret_cast<const char*>(sqlite3_column_text(statement, 1))) {
            const bool nullable = sqlite3_column_int(statement, 3) == 0;
            sqlite3_finalize(statement);
            return nullable;
        }
    }
    sqlite3_finalize(statement);
    return false;
}

} // namespace

TEST_CASE("Phase 4 SQLite v4 upgrades v3 durably and reopens", "[phase4.u3]")
{
    ExactPaths paths;
    FixedIds ids;
    const auto path = databasePath();
    sqlite3* raw = createV3Database(path);
    sqlite3_close(raw);

    auto writer = openSqliteCatalogOutbox(path, paths, ids, {});
    REQUIRE(writer->pendingEventCount() == 1);
    writer.reset();

    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(scalar(raw, "SELECT max(version) FROM schema_migrations") == 4);
    REQUIRE(hasTable(raw, "watch_roots"));
    REQUIRE(hasTable(raw, "reconciliation_runs"));
    REQUIRE(hasTable(raw, "reconciliation_staging"));
    REQUIRE(hasTable(raw, "operational_status"));
    REQUIRE(hasTable(raw, "operational_errors"));
    REQUIRE(hasColumn(raw, "watch_roots", "normalized_root_path"));
    REQUIRE(isNullable(raw, "watch_roots", "normalized_root_path"));
    REQUIRE(hasColumn(raw, "reconciliation_staging", "metadata_modified_at"));
    REQUIRE(isNullable(raw, "reconciliation_staging", "metadata_modified_at"));
    REQUIRE(scalar(raw, "SELECT dirty_epoch FROM watch_roots WHERE root_id='root'") == 9);
    REQUIRE(scalar(raw, "SELECT dirty_reasons FROM watch_roots WHERE root_id='root'") == 5);
    REQUIRE(scalar(raw, "SELECT dirty_epoch FROM watch_roots WHERE root_id='catalog-root'") == 0);
    REQUIRE(scalar(raw, "SELECT dirty_reasons FROM watch_roots WHERE root_id='catalog-root'") == 0);
    REQUIRE(scalar(raw, "SELECT dirty_epoch FROM watch_roots WHERE root_id='dirty-only-root'") == 4);
    REQUIRE(scalar(raw, "SELECT dirty_reasons FROM watch_roots WHERE root_id='dirty-only-root'") == 2);
    REQUIRE(scalar(raw, "SELECT count(*) FROM watch_roots WHERE root_id='root' AND normalized_root_path IS NULL") == 1);
    REQUIRE(scalar(raw, "SELECT generation FROM observed_files WHERE observation_id='old-observation'") == 7);
    REQUIRE(scalar(raw, "SELECT count(*) FROM observed_files") == 2);
    REQUIRE(scalar(raw, "SELECT count(*) FROM event_outbox WHERE event_id='old-event'") == 1);
    REQUIRE(scalar(raw, "SELECT count(*) FROM sqlite_master WHERE type='index' AND name='reconciliation_runs_one_active_root'") == 1);
    execute(raw, "PRAGMA foreign_keys=ON;");
    execute(raw, "INSERT INTO reconciliation_runs(run_id,root_id,captured_epoch,state,created_at) VALUES('run-1','root',9,'active',1);");
    REQUIRE(queryPlan(raw, "EXPLAIN QUERY PLAN SELECT run_id FROM reconciliation_runs WHERE root_id='root' AND state='active'").find("reconciliation_runs_one_active_root") != std::string::npos);
    requireRejected(raw, "INSERT INTO reconciliation_runs(run_id,root_id,captured_epoch,state,created_at) VALUES('run-2','root',9,'active',1);");
    requireRejected(raw, "INSERT INTO reconciliation_runs(run_id,root_id,captured_epoch,state,created_at) VALUES('orphan','missing-root',0,'cancelled',1);");
    requireRejected(raw, "INSERT INTO reconciliation_runs(run_id,root_id,captured_epoch,state,armed_scan,created_at) VALUES('invalid-bool','root',9,'cancelled',2,1);");
    requireRejected(raw, "INSERT INTO reconciliation_staging(run_id,sequence,relative_path,completed_subtree) VALUES('run-1',1,'report.txt',2);");
    requireRejected(raw, "UPDATE observed_files SET tombstoned=2 WHERE observation_id='old-observation';");
    const WatchRootConfig configuredRoot{*RootId::create("configured-root"), {"C:/normalized/root"}, {}};
    const auto insertConfiguredRoot = "INSERT INTO watch_roots(root_id,normalized_root_path,dirty_epoch,dirty_reasons) VALUES('" + std::string(configuredRoot.rootId.value()) + "','" + configuredRoot.root.utf8 + "',0,0);";
    execute(raw, insertConfiguredRoot.c_str());
    execute(raw, "INSERT INTO reconciliation_runs(run_id,root_id,captured_epoch,state,created_at) VALUES('run-2','configured-root',0,'cancelled',2);");
    execute(raw, "INSERT INTO reconciliation_staging(run_id,sequence,relative_path,metadata_size,metadata_modified_at,completed_subtree) VALUES('run-2',1,'report.txt',12,1700000000123456,1);");
    sqlite3_close(raw);

    auto reopened = openSqliteCatalogOutbox(path, paths, ids, {});
    REQUIRE(reopened->pendingEventCount() == 1);
    reopened.reset();
    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(text(raw, "SELECT normalized_root_path FROM watch_roots WHERE root_id='configured-root'") == configuredRoot.root.utf8);
    REQUIRE(scalar64(raw, "SELECT metadata_modified_at FROM reconciliation_staging WHERE run_id='run-2' AND sequence=1") == 1700000000123456);
    sqlite3_close(raw);
}

TEST_CASE("Phase 4 SQLite v4 creates fresh state and rejects future or checksum drift", "[phase4.u3]")
{
    ExactPaths paths;
    FixedIds ids;
    const auto path = databasePath();
    REQUIRE_NOTHROW(openSqliteCatalogOutbox(path, paths, ids, {}));

    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(scalar(raw, "SELECT max(version) FROM schema_migrations") == 4);
    REQUIRE(hasTable(raw, "watch_roots"));
    REQUIRE(hasTable(raw, "operational_errors"));
    execute(raw, "UPDATE schema_migrations SET checksum='drift' WHERE version=4;");
    sqlite3_close(raw);
    REQUIRE_THROWS(openSqliteCatalogOutbox(path, paths, ids, {}));

    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(scalar(raw, "SELECT count(*) FROM schema_migrations WHERE version=4 AND checksum='drift'") == 1);
    execute(raw, "UPDATE schema_migrations SET checksum='catalog-outbox-v4-root-path-metadata' WHERE version=4; INSERT INTO schema_migrations VALUES(5,'future');");
    sqlite3_close(raw);
    REQUIRE_THROWS(openSqliteCatalogOutbox(path, paths, ids, {}));
}

TEST_CASE("Phase 4 SQLite v4 rolls back an interrupted migration", "[phase4.u3]")
{
    ExactPaths paths;
    FixedIds ids;
    const auto path = databasePath();
    sqlite3* raw = createV3Database(path);
    execute(raw, "CREATE TABLE watch_roots(root_id TEXT PRIMARY KEY,normalized_root_path TEXT,dirty_epoch INTEGER,dirty_reasons INTEGER);");
    sqlite3_close(raw);

    REQUIRE_THROWS(openSqliteCatalogOutbox(path, paths, ids, {}));
    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(scalar(raw, "SELECT max(version) FROM schema_migrations") == 3);
    REQUIRE_FALSE(hasTable(raw, "reconciliation_runs"));
    REQUIRE_FALSE(hasColumn(raw, "observed_files", "native_identity"));
    REQUIRE_FALSE(hasColumn(raw, "observed_files", "tombstoned"));
    REQUIRE(scalar(raw, "SELECT count(*) FROM watch_roots") == 0);
    REQUIRE(scalar(raw, "SELECT count(*) FROM event_outbox WHERE event_id='old-event'") == 1);
    execute(raw, "DROP TABLE watch_roots;");
    sqlite3_close(raw);

    auto retried = openSqliteCatalogOutbox(path, paths, ids, {});
    REQUIRE(retried->pendingEventCount() == 1);
    retried.reset();
    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(scalar(raw, "SELECT max(version) FROM schema_migrations") == 4);
    REQUIRE(scalar(raw, "SELECT dirty_epoch FROM watch_roots WHERE root_id='root'") == 9);
    REQUIRE(scalar(raw, "SELECT dirty_reasons FROM watch_roots WHERE root_id='root'") == 5);
    sqlite3_close(raw);
}
