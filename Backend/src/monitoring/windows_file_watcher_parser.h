#pragma once

#include "semantic_fs/monitoring/monitoring_types.h"

#include <cstddef>
#include <span>
#include <vector>

namespace semantic_fs::monitoring::detail {
enum class WindowsWatcherDecodeTermination { BatchComplete, Cancelled };

struct DecodedWindowsWatcherRecords {
    std::vector<WatcherEvent> events;
    bool dirty{};
};

DecodedWindowsWatcherRecords decodeWindowsWatcherRecords(
    const WatchRootConfig& root,
    std::span<const std::byte> bytes,
    WindowsWatcherDecodeTermination termination = WindowsWatcherDecodeTermination::BatchComplete);
} // namespace semantic_fs::monitoring::detail
