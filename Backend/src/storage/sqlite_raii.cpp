#include "sqlite_raii.h"
#include <stdexcept>
namespace semantic_fs::storage {
SqliteDatabase::SqliteDatabase(const char* path) { if (sqlite3_open_v2(path, &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) throw std::runtime_error("sqlite open failed"); }
SqliteDatabase::~SqliteDatabase() { if (db_) sqlite3_close(db_); }
void SqliteDatabase::exec(std::string_view sql) const { char* error = nullptr; if (sqlite3_exec(db_, sql.data(), nullptr, nullptr, &error) != SQLITE_OK) { const std::string message = error ? error : "sqlite execution failed"; sqlite3_free(error); throw std::runtime_error(message); } }
} // namespace semantic_fs::storage
