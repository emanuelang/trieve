#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace semantic_fs::monitoring {

inline bool isValidUtf8(std::string_view value)
{
    for (std::size_t index = 0; index < value.size();) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte < 0x80) {
            ++index;
            continue;
        }

        const auto continuationCount = byte <= 0xDF ? 1 : byte <= 0xEF ? 2 : byte <= 0xF4 ? 3 : 0;
        if (continuationCount == 0 || index + continuationCount >= value.size()) {
            return false;
        }

        const auto firstContinuation = static_cast<unsigned char>(value[index + 1]);
        if ((firstContinuation & 0xC0) != 0x80
            || (byte == 0xC0 || byte == 0xC1)
            || (byte == 0xE0 && firstContinuation < 0xA0)
            || (byte == 0xED && firstContinuation >= 0xA0)
            || (byte == 0xF0 && firstContinuation < 0x90)
            || (byte == 0xF4 && firstContinuation > 0x8F)) {
            return false;
        }

        for (std::size_t offset = 2; offset <= continuationCount; ++offset) {
            if ((static_cast<unsigned char>(value[index + offset]) & 0xC0) != 0x80) {
                return false;
            }
        }
        index += continuationCount + 1;
    }
    return true;
}

template <typename Tag>
class OpaqueId {
public:
    static std::optional<OpaqueId> create(std::string_view value)
    {
        if (value.empty() || !isValidUtf8(value)) return std::nullopt;
        return OpaqueId(std::string(value));
    }
    [[nodiscard]] std::string_view value() const { return value_; }
private:
    explicit OpaqueId(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

using RootId = OpaqueId<struct RootIdTag>;
using ObservationId = OpaqueId<struct ObservationIdTag>;
using EventId = OpaqueId<struct EventIdTag>;

struct Generation {
    std::uint64_t value;
    static std::optional<Generation> create(std::uint64_t value) { return value == 0 ? std::nullopt : std::optional<Generation>{{value}}; }
};

struct UtcTimestamp { std::int64_t microsecondsSinceEpoch; };
struct AbsolutePath { std::string utf8; };
struct RelativePath { std::string utf8; std::vector<std::string> components; };
struct NormalizedPath { std::string displayUtf8; RelativePath relative; };
struct FileMetadata { std::optional<std::uint64_t> size; std::optional<UtcTimestamp> modifiedAt; std::optional<std::string> nativeFileId; };

enum class ObservationKind { Discovered };
enum class ObservationSource { InitialScan, Watcher, Reconciliation };
enum class ChangeKind { Discovered, Created, Modified, Removed, Renamed };
enum class PublishResult { Accepted, Duplicate, RetryableFailure, Rejected };
enum class ObservationDelivery { Accepted, Backpressured, Cancelled, Rejected };

struct FileObservation {
    RootId rootId;
    NormalizedPath path;
    FileMetadata metadata;
    UtcTimestamp observedAt;
    ObservationKind kind;
    ObservationSource source;
};

inline constexpr std::uint32_t kFileChangeSchemaVersion = 1;
struct FileChange {
    std::uint32_t schemaVersion;
    EventId eventId;
    RootId rootId;
    ObservationId observationId;
    Generation generation;
    ChangeKind kind;
    NormalizedPath path;
    std::optional<NormalizedPath> previousPath;
    std::optional<FileMetadata> metadata;
    UtcTimestamp observedAt;
    ObservationSource source;
};

struct PolicyFilterConfig {
    std::vector<std::string> extensions;
    std::vector<RelativePath> excludedRelativePrefixes;
    std::optional<std::uint64_t> maximumSize;
};
struct WatchRootConfig { RootId rootId; AbsolutePath root; PolicyFilterConfig policy; };
enum class WatcherEventKind { Created, Modified, Removed, Renamed, Overflow };
struct WatcherEvent { RootId rootId; WatcherEventKind kind; AbsolutePath path; std::optional<AbsolutePath> previousPath; };
enum class WatcherStartResult { Started, InvalidConfig, RootUnavailable, NativeFailure };

} // namespace semantic_fs::monitoring
