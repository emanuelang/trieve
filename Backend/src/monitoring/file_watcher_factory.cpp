#include "semantic_fs/monitoring/file_watcher_factory.h"

namespace semantic_fs::monitoring {
std::unique_ptr<IFileWatcher> makeWindowsFileWatcher();
std::unique_ptr<IFileWatcher> makeFileWatcher() { return makeWindowsFileWatcher(); }
} // namespace semantic_fs::monitoring
