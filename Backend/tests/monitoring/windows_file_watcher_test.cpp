#include "semantic_fs/monitoring/file_watcher_factory.h"
#include "../../src/monitoring/windows_file_watcher_parser.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <algorithm>
#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include <windows.h>

using namespace semantic_fs::monitoring;

namespace {
class Ingress final : public IWatcherIngressSink {
public:
    IngressDelivery accept(WatcherIngress item) override {
        std::lock_guard lock(mutex);
        items.push_back(std::move(item));
        cv.notify_all();
        return IngressDelivery::Accepted;
    }
    bool waitForRecord() {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return std::any_of(items.begin(), items.end(), [](const auto& item) { return std::holds_alternative<WatcherRecord>(item); }); });
    }
    bool waitFor(WatcherEventKind kind) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return std::any_of(items.begin(), items.end(), [kind](const auto& item) { const auto* record = std::get_if<WatcherRecord>(&item); return record && record->event.kind == kind; }); });
    }
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<WatcherIngress> items;
};

class BlockingIngress final : public IWatcherIngressSink {
public:
    IngressDelivery accept(WatcherIngress item) override {
        std::unique_lock lock(mutex);
        items.push_back(std::move(item));
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
        return IngressDelivery::Accepted;
    }
    bool waitForEntry() { std::unique_lock lock(mutex); return cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }); }
    void release() { std::lock_guard lock(mutex); released = true; cv.notify_all(); }
    bool waitForSaturation() {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return std::any_of(items.begin(), items.end(), [](const auto& item) { const auto* dirty = std::get_if<RootDirty>(&item); return dirty && (dirty->reasons & static_cast<std::uint32_t>(DirtyReason::QueueSaturation)); }); });
    }
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<WatcherIngress> items;
    bool entered{}, released{};
};

WatchRootConfig config(const std::filesystem::path& path) { return {*RootId::create("native-root"), {path.string()}, {}}; }

WatchRootConfig parserConfig() { return {*RootId::create("parser-root"), {"C:/watch"}, {}}; }

void appendRecord(std::vector<std::byte>& bytes, DWORD action, std::wstring_view name)
{
    const auto start = bytes.size();
    std::size_t previous{};
    while (previous < start) {
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes.data() + previous);
        if (!info->NextEntryOffset) break;
        previous += info->NextEntryOffset;
    }
    bytes.resize(start + offsetof(FILE_NOTIFY_INFORMATION, FileName) + name.size() * sizeof(wchar_t));
    auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(bytes.data() + start);
    info->NextEntryOffset = 0;
    info->Action = action;
    info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, name.data(), info->FileNameLength);
    if (start) reinterpret_cast<FILE_NOTIFY_INFORMATION*>(bytes.data() + previous)->NextEntryOffset = static_cast<DWORD>(start - previous);
}
} // namespace

TEST_CASE("monitoring windows_file_watcher: controlled parser bounds, UTF-16, and root containment", "[phase4.u8]")
{
    std::vector<std::byte> malformed(offsetof(FILE_NOTIFY_INFORMATION, FileName));
    const auto malformedResult = detail::decodeWindowsWatcherRecords(parserConfig(), malformed);
    REQUIRE(malformedResult.dirty);
    REQUIRE(malformedResult.events.empty());

    std::vector<std::byte> invalidUtf16;
    appendRecord(invalidUtf16, FILE_ACTION_ADDED, std::wstring(1, static_cast<wchar_t>(0xD800)));
    const auto invalidUtf16Result = detail::decodeWindowsWatcherRecords(parserConfig(), invalidUtf16);
    REQUIRE(invalidUtf16Result.dirty);
    REQUIRE(invalidUtf16Result.dirtyReasons == static_cast<std::uint32_t>(DirtyReason::DecodeGap));
    REQUIRE(invalidUtf16Result.events.empty());

    std::vector<std::byte> escaping;
    appendRecord(escaping, FILE_ACTION_ADDED, L"..\\outside.txt");
    const auto escapingResult = detail::decodeWindowsWatcherRecords(parserConfig(), escaping);
    REQUIRE(escapingResult.dirty);
    REQUIRE(escapingResult.dirtyReasons == static_cast<std::uint32_t>(DirtyReason::DecodeGap));
    REQUIRE(escapingResult.events.empty());

    std::vector<std::byte> valid;
    appendRecord(valid, FILE_ACTION_ADDED, L"nested\\\u03B4.txt");
    const auto validResult = detail::decodeWindowsWatcherRecords(parserConfig(), valid);
    REQUIRE_FALSE(validResult.dirty);
    REQUIRE(validResult.events.size() == 1);
    REQUIRE(validResult.events.front().kind == WatcherEventKind::Created);
    REQUIRE(validResult.events.front().path.utf8 == "C:/watch/nested/\xCE\xB4.txt");

    std::vector<std::byte> trailing;
    appendRecord(trailing, FILE_ACTION_ADDED, L"complete.txt");
    trailing.resize(trailing.size() + sizeof(DWORD));
    trailing.back() = std::byte{1};
    const auto trailingResult = detail::decodeWindowsWatcherRecords(parserConfig(), trailing);
    REQUIRE(trailingResult.dirty);
    REQUIRE(trailingResult.events.size() == 1);

    std::vector<std::byte> invalidAction;
    appendRecord(invalidAction, 99, L"ignored.txt");
    const auto invalidActionResult = detail::decodeWindowsWatcherRecords(parserConfig(), invalidAction);
    REQUIRE(invalidActionResult.dirty);
    REQUIRE(invalidActionResult.events.empty());

    std::vector<std::byte> invalidOffset;
    appendRecord(invalidOffset, FILE_ACTION_ADDED, L"bounded.txt");
    reinterpret_cast<FILE_NOTIFY_INFORMATION*>(invalidOffset.data())->NextEntryOffset = 1;
    const auto invalidOffsetResult = detail::decodeWindowsWatcherRecords(parserConfig(), invalidOffset);
    REQUIRE(invalidOffsetResult.dirty);
    REQUIRE(invalidOffsetResult.events.size() == 1);
}

TEST_CASE("monitoring windows_file_watcher: controlled parser preserves order and degrades ambiguous rename evidence", "[phase4.u8]")
{
    std::vector<std::byte> contiguous;
    appendRecord(contiguous, FILE_ACTION_RENAMED_OLD_NAME, L"old.txt");
    appendRecord(contiguous, FILE_ACTION_RENAMED_NEW_NAME, L"new.txt");
    const auto contiguousResult = detail::decodeWindowsWatcherRecords(parserConfig(), contiguous);
    REQUIRE_FALSE(contiguousResult.dirty);
    REQUIRE(contiguousResult.events.size() == 1);
    REQUIRE(contiguousResult.events.front().kind == WatcherEventKind::Renamed);
    REQUIRE(contiguousResult.events.front().previousPath->utf8 == "C:/watch/old.txt");
    REQUIRE(contiguousResult.events.front().path.utf8 == "C:/watch/new.txt");

    std::vector<std::byte> gapped;
    appendRecord(gapped, FILE_ACTION_RENAMED_OLD_NAME, L"old.txt");
    appendRecord(gapped, FILE_ACTION_MODIFIED, L"other.txt");
    appendRecord(gapped, FILE_ACTION_RENAMED_NEW_NAME, L"new.txt");
    const auto gappedResult = detail::decodeWindowsWatcherRecords(parserConfig(), gapped);
    REQUIRE(gappedResult.dirty);
    REQUIRE(gappedResult.events.size() == 3);
    REQUIRE(gappedResult.events[0].kind == WatcherEventKind::Removed);
    REQUIRE(gappedResult.events[1].kind == WatcherEventKind::Modified);
    REQUIRE(gappedResult.events[2].kind == WatcherEventKind::Created);

    std::vector<std::byte> ordered;
    appendRecord(ordered, FILE_ACTION_ADDED, L"first.txt");
    appendRecord(ordered, FILE_ACTION_MODIFIED, L"second.txt");
    const auto orderedResult = detail::decodeWindowsWatcherRecords(parserConfig(), ordered);
    REQUIRE_FALSE(orderedResult.dirty);
    REQUIRE(orderedResult.events.size() == 2);
    REQUIRE(orderedResult.events[0].path.utf8 == "C:/watch/first.txt");
    REQUIRE(orderedResult.events[1].path.utf8 == "C:/watch/second.txt");
}

TEST_CASE("monitoring windows_file_watcher: cancellation finalizes a pending rename without false identity", "[phase4.u8]")
{
    std::vector<std::byte> pendingOld;
    appendRecord(pendingOld, FILE_ACTION_RENAMED_OLD_NAME, L"old.txt");
    const auto cancelledOld = detail::decodeWindowsWatcherRecords(
        parserConfig(), pendingOld, detail::WindowsWatcherDecodeTermination::Cancelled);
    REQUIRE(cancelledOld.dirty);
    REQUIRE(cancelledOld.events.size() == 1);
    REQUIRE(cancelledOld.events.front().kind == WatcherEventKind::Removed);
    REQUIRE_FALSE(cancelledOld.events.front().previousPath.has_value());

    std::vector<std::byte> cancelledPair;
    appendRecord(cancelledPair, FILE_ACTION_RENAMED_OLD_NAME, L"old.txt");
    appendRecord(cancelledPair, FILE_ACTION_RENAMED_NEW_NAME, L"new.txt");
    const auto cancelledResult = detail::decodeWindowsWatcherRecords(
        parserConfig(), cancelledPair, detail::WindowsWatcherDecodeTermination::Cancelled);
    REQUIRE(cancelledResult.dirty);
    REQUIRE(cancelledResult.events.size() == 2);
    REQUIRE(cancelledResult.events[0].kind == WatcherEventKind::Removed);
    REQUIRE(cancelledResult.events[1].kind == WatcherEventKind::Created);
}

TEST_CASE("monitoring windows_file_watcher: zero and failed reads produce OS overflow", "[phase4.u8]")
{
    const auto zero = detail::decodeWindowsWatcherRead(parserConfig(), {}, detail::WindowsWatcherReadCompletion::ZeroBytes);
    REQUIRE(zero.dirtyReasons == static_cast<std::uint32_t>(DirtyReason::OsOverflow));
    REQUIRE(zero.events.empty());

    const auto failed = detail::decodeWindowsWatcherRead(parserConfig(), {}, detail::WindowsWatcherReadCompletion::Failed);
    REQUIRE(failed.dirtyReasons == static_cast<std::uint32_t>(DirtyReason::OsOverflow));
    REQUIRE(failed.events.empty());
}

TEST_CASE("monitoring windows_file_watcher: factory captures a temporary-directory mutation")
{
    const auto directory = std::filesystem::temp_directory_path() / ("semantic-fs-watcher-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    Ingress ingress;
    auto watcher = makeFileWatcher();
    const auto started = watcher->start(config(directory), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    {
        std::ofstream(directory / "created.txt") << "native watcher";
    }
    REQUIRE(ingress.waitForRecord());
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    std::filesystem::remove_all(directory);
}

TEST_CASE("monitoring windows_file_watcher: stop before start is idempotently already stopped")
{
    auto watcher = makeFileWatcher();
    REQUIRE(watcher->stopAndJoin() == StopOutcome::AlreadyStopped);
    REQUIRE(watcher->stopAndJoin() == StopOutcome::AlreadyStopped);
}

TEST_CASE("monitoring windows_file_watcher: start and stop are idempotent")
{
    const auto directory = std::filesystem::temp_directory_path() / ("semantic-fs-watcher-stop-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    Ingress ingress;
    auto watcher = makeFileWatcher();
    const auto started = watcher->start(config(directory), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    REQUIRE(watcher->start(config(directory), ingress).status == WatcherStartStatus::AlreadyStarted);
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    REQUIRE(started.session->stopAndJoin() == StopOutcome::AlreadyStopped);
    std::filesystem::remove_all(directory);
}

TEST_CASE("monitoring windows_file_watcher: stop joins before later filesystem activity")
{
    const auto directory = std::filesystem::temp_directory_path() / ("semantic-fs-watcher-joined-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    Ingress ingress;
    auto watcher = makeFileWatcher();
    const auto started = watcher->start(config(directory), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    std::ofstream(directory / "after-stop.txt") << "must not dispatch";
    std::lock_guard lock(ingress.mutex);
    REQUIRE(ingress.items.empty());
    std::filesystem::remove_all(directory);
}

TEST_CASE("monitoring windows_file_watcher: correlates only a native contiguous rename")
{
    const auto directory = std::filesystem::temp_directory_path() / ("semantic-fs-watcher-rename-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "old.txt") << "before";
    Ingress ingress;
    auto watcher = makeFileWatcher();
    const auto started = watcher->start(config(directory), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    std::filesystem::rename(directory / "old.txt", directory / "new.txt");
    REQUIRE(ingress.waitFor(WatcherEventKind::Renamed));
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    std::filesystem::remove_all(directory);
}

TEST_CASE("monitoring windows_file_watcher: rejects invalid UTF-8 and unavailable roots")
{
    Ingress ingress;
    auto watcher = makeFileWatcher();
    auto invalid = config(std::filesystem::temp_directory_path());
    invalid.root.utf8 = "\xFF";
    REQUIRE(watcher->start(invalid, ingress).status == WatcherStartStatus::InvalidConfig);
    auto missing = config(std::filesystem::temp_directory_path() / "semantic-fs-watcher-missing-root");
    REQUIRE(watcher->start(missing, ingress).status == WatcherStartStatus::RootUnavailable);
}

TEST_CASE("monitoring windows_file_watcher: cancellation is idempotent while an overlapped read is outstanding")
{
    const auto directory = std::filesystem::temp_directory_path() / ("semantic-fs-watcher-cancel-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    Ingress ingress;
    auto watcher = makeFileWatcher();
    const auto started = watcher->start(config(directory), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    REQUIRE(started.session->requestCancellation() == CancelOutcome::Requested);
    REQUIRE(started.session->requestCancellation() == CancelOutcome::AlreadyRequested);
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    std::filesystem::remove_all(directory);
}

TEST_CASE("monitoring windows_file_watcher: saturated admission remains non-blocking and reports sticky dirty", "[phase4.u8]")
{
    const auto directory = std::filesystem::temp_directory_path() / ("semantic-fs-watcher-saturated-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    BlockingIngress ingress;
    auto watcher = makeFileWatcher();
    const auto started = watcher->start(config(directory), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    std::ofstream(directory / "first.txt") << "block dispatch";
    REQUIRE(ingress.waitForEntry());
    for (int index = 0; index != 100; ++index) std::ofstream(directory / ("burst-" + std::to_string(index))) << index;
    ingress.release();
    REQUIRE(ingress.waitForSaturation());
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    std::filesystem::remove_all(directory);
}
