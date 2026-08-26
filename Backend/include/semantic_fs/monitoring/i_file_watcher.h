#pragma once
#include "semantic_fs/monitoring/monitoring_types.h"
#include <functional>
namespace semantic_fs::monitoring { using WatcherCallback = std::function<void(const WatcherEvent&)>; class IFileWatcher { public: virtual ~IFileWatcher() = default; virtual WatcherStartResult start(const WatchRootConfig& config, WatcherCallback callback) = 0; virtual void stop() = 0; }; }
