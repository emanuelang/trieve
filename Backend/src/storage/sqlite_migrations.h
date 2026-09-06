#pragma once
#include "sqlite_raii.h"
#include <string_view>
namespace semantic_fs::storage { void runCatalogOutboxMigrations(const SqliteDatabase&, std::string_view checksum); }
