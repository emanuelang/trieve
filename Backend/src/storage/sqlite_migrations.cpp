#include "sqlite_migrations.h"

#include <optional>
#include <stdexcept>
#include <string>

namespace semantic_fs::storage {
namespace {

constexpr int kCurrentMigrationVersion = 4;
constexpr std::string_view kCurrentMigrationChecksum = "catalog-outbox-v4-root-path-metadata";

std::optional<std::string> checksumFor(const SqliteDatabase& database, int version)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database.get(), "SELECT checksum FROM schema_migrations WHERE version=?", -1, &statement, nullptr) != SQLITE_OK) throw std::runtime_error("migration lookup failed");
    sqlite3_bind_int(statement, 1, version);
    const auto result = sqlite3_step(statement);
    std::optional<std::string> checksum;
    if (result == SQLITE_ROW) checksum = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    else if (result != SQLITE_DONE) { sqlite3_finalize(statement); throw std::runtime_error("migration lookup failed"); }
    sqlite3_finalize(statement);
    return checksum;
}

bool hasChecksum(const SqliteDatabase& database, int version, std::string_view expected)
{
    const auto checksum = checksumFor(database, version);
    if (checksum && *checksum != expected) throw std::runtime_error("migration checksum drift");
    return checksum.has_value();
}

void ensureNoFutureVersion(const SqliteDatabase& database)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database.get(), "SELECT max(version) FROM schema_migrations", -1, &statement, nullptr) != SQLITE_OK) throw std::runtime_error("migration lookup failed");
    const auto result = sqlite3_step(statement);
    const auto future = result == SQLITE_ROW && sqlite3_column_type(statement, 0) != SQLITE_NULL && sqlite3_column_int(statement, 0) > kCurrentMigrationVersion;
    sqlite3_finalize(statement);
    if (result != SQLITE_ROW) throw std::runtime_error("migration lookup failed");
    if (future) throw std::runtime_error("unsupported migration version");
}

} // namespace

void runCatalogOutboxMigrations(const SqliteDatabase& database, std::string_view checksum)
{
    if (checksum != kCurrentMigrationChecksum) throw std::runtime_error("unsupported migration checksum");
    try {
        database.exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL; PRAGMA busy_timeout=2500; BEGIN IMMEDIATE;"
                      "CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY, checksum TEXT NOT NULL);"
                      "CREATE TABLE IF NOT EXISTS store_runtime(singleton INTEGER PRIMARY KEY CHECK(singleton=1),last_seen_utc INTEGER NOT NULL);"
                      "CREATE TABLE IF NOT EXISTS observed_files(observation_id TEXT PRIMARY KEY, root_id TEXT NOT NULL, path TEXT NOT NULL, generation INTEGER NOT NULL CHECK(generation>0), size INTEGER, modified_at INTEGER);"
                      "CREATE TABLE IF NOT EXISTS event_outbox(event_id TEXT PRIMARY KEY, observation_id TEXT NOT NULL, generation INTEGER NOT NULL, payload TEXT NOT NULL, state TEXT NOT NULL CHECK(state IN ('pending','leased','delivered','terminal')), FOREIGN KEY(observation_id) REFERENCES observed_files(observation_id));"
                      "CREATE TABLE IF NOT EXISTS dirty_obligations(root_id TEXT PRIMARY KEY,epoch INTEGER NOT NULL,reasons INTEGER NOT NULL);"
                      "CREATE TABLE IF NOT EXISTS deferred_coverage(id INTEGER PRIMARY KEY,root_id TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('removed','renamed')));");
        ensureNoFutureVersion(database);
        if (!hasChecksum(database, 1, "catalog-outbox-v1")) database.exec("INSERT INTO schema_migrations(version,checksum) VALUES(1,'catalog-outbox-v1');");
        if (!hasChecksum(database, 2, "catalog-outbox-v2")) database.exec("ALTER TABLE event_outbox ADD COLUMN schema_version INTEGER NOT NULL DEFAULT 1; INSERT INTO schema_migrations(version,checksum) VALUES(2,'catalog-outbox-v2');");
        if (!hasChecksum(database, 3, "catalog-outbox-v3")) database.exec("ALTER TABLE event_outbox ADD COLUMN available_at INTEGER NOT NULL DEFAULT 0; ALTER TABLE event_outbox ADD COLUMN attempts INTEGER NOT NULL DEFAULT 0; ALTER TABLE event_outbox ADD COLUMN lease_owner TEXT; ALTER TABLE event_outbox ADD COLUMN lease_token TEXT; ALTER TABLE event_outbox ADD COLUMN lease_deadline INTEGER; ALTER TABLE event_outbox ADD COLUMN diagnostic TEXT; ALTER TABLE event_outbox ADD COLUMN kind TEXT NOT NULL DEFAULT 'created'; ALTER TABLE event_outbox ADD COLUMN observed_at INTEGER NOT NULL DEFAULT 0; ALTER TABLE event_outbox ADD COLUMN source INTEGER NOT NULL DEFAULT 0; INSERT INTO schema_migrations(version,checksum) VALUES(3,'catalog-outbox-v3');");
        if (!hasChecksum(database, kCurrentMigrationVersion, kCurrentMigrationChecksum)) {
            database.exec("CREATE TABLE watch_roots(root_id TEXT PRIMARY KEY,normalized_root_path TEXT,dirty_epoch INTEGER NOT NULL DEFAULT 0,dirty_reasons INTEGER NOT NULL DEFAULT 0);"
                          "CREATE TABLE reconciliation_runs(run_id TEXT PRIMARY KEY,root_id TEXT NOT NULL,captured_epoch INTEGER NOT NULL,state TEXT NOT NULL CHECK(state IN ('active','cancelled','failed','complete')),scan_cursor INTEGER NOT NULL DEFAULT 0,finalize_cursor INTEGER NOT NULL DEFAULT 0,armed_scan INTEGER NOT NULL DEFAULT 0 CHECK(armed_scan IN (0,1)),created_at INTEGER NOT NULL,FOREIGN KEY(root_id) REFERENCES watch_roots(root_id));"
                          "CREATE UNIQUE INDEX reconciliation_runs_one_active_root ON reconciliation_runs(root_id) WHERE state='active';"
                          "CREATE TABLE reconciliation_staging(run_id TEXT NOT NULL,sequence INTEGER NOT NULL,relative_path TEXT NOT NULL,native_identity TEXT,metadata_size INTEGER,metadata_modified_at INTEGER,completed_subtree INTEGER NOT NULL DEFAULT 0 CHECK(completed_subtree IN (0,1)),PRIMARY KEY(run_id,sequence),FOREIGN KEY(run_id) REFERENCES reconciliation_runs(run_id));"
                          "CREATE TABLE operational_status(root_id TEXT PRIMARY KEY,last_run_id TEXT,last_error TEXT CHECK(length(last_error)<=1024),updated_at INTEGER NOT NULL DEFAULT 0,FOREIGN KEY(root_id) REFERENCES watch_roots(root_id),FOREIGN KEY(last_run_id) REFERENCES reconciliation_runs(run_id));"
                          "CREATE TABLE operational_errors(id INTEGER PRIMARY KEY,root_id TEXT NOT NULL,component TEXT NOT NULL,message TEXT NOT NULL CHECK(length(message)<=1024),occurred_at INTEGER NOT NULL,FOREIGN KEY(root_id) REFERENCES watch_roots(root_id));"
                          "ALTER TABLE observed_files ADD COLUMN native_identity TEXT; ALTER TABLE observed_files ADD COLUMN tombstoned INTEGER NOT NULL DEFAULT 0 CHECK(tombstoned IN (0,1));"
                          "INSERT INTO watch_roots(root_id,dirty_epoch,dirty_reasons) SELECT root_id,0,0 FROM observed_files WHERE tombstoned=0 GROUP BY root_id;"
                          "INSERT OR IGNORE INTO watch_roots(root_id,dirty_epoch,dirty_reasons) SELECT root_id,epoch,reasons FROM dirty_obligations;"
                          "UPDATE watch_roots SET dirty_epoch=(SELECT max(watch_roots.dirty_epoch,epoch) FROM dirty_obligations WHERE root_id=watch_roots.root_id),dirty_reasons=(SELECT watch_roots.dirty_reasons|reasons FROM dirty_obligations WHERE root_id=watch_roots.root_id) WHERE EXISTS(SELECT 1 FROM dirty_obligations WHERE root_id=watch_roots.root_id);"
                          "INSERT INTO schema_migrations(version,checksum) VALUES(4,'catalog-outbox-v4-root-path-metadata');");
        }
        database.exec("COMMIT;");
    } catch (...) {
        try { database.exec("ROLLBACK;"); } catch (...) {}
        throw;
    }
}

} // namespace semantic_fs::storage
