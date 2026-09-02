#pragma once

#include "semantic_fs/monitoring/i_file_watcher.h"

#include <memory>

namespace semantic_fs::monitoring {
std::unique_ptr<IFileWatcher> makeFileWatcher();
} // namespace semantic_fs::monitoring
