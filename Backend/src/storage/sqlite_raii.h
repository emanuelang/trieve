#pragma once
#include <sqlite3.h>
#include <string_view>
namespace semantic_fs::storage {
class SqliteDatabase { public: explicit SqliteDatabase(const char* path); ~SqliteDatabase(); SqliteDatabase(const SqliteDatabase&) = delete; sqlite3* get() const { return db_; } void exec(std::string_view sql) const; private: sqlite3* db_ = nullptr; };
} // namespace semantic_fs::storage
