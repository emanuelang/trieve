#pragma once

#include "semantic_fs/monitoring/monitoring_types.h"

#include <cstddef>
#include <span>
#include <vector>

namespace semantic_fs::monitoring::detail {
enum class WindowsWatcherDecodeTermination { BatchComplete, Cancelled };
enum class WindowsWatcherReadCompletion { Complete, ZeroBytes, Failed };

struct DecodedWindowsWatcherRecords {
    std::vector<WatcherEvent> events;
    bool dirty{};
    std::uint32_t dirtyReasons{};

    void markDirty(DirtyReason reason)
    {
        dirty = true;
        dirtyReasons |= static_cast<std::uint32_t>(reason);
    }
};

DecodedWindowsWatcherRecords decodeWindowsWatcherRecords(
    const WatchRootConfig& root,
    std::span<const std::byte> bytes,
    WindowsWatcherDecodeTermination termination = WindowsWatcherDecodeTermination::BatchComplete);

DecodedWindowsWatcherRecords decodeWindowsWatcherRead(
    const WatchRootConfig& root,
    std::span<const std::byte> bytes,
    WindowsWatcherReadCompletion completion,
    WindowsWatcherDecodeTermination termination = WindowsWatcherDecodeTermination::BatchComplete);
} // namespace semantic_fs::monitoring::detail
