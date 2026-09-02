#include "semantic_fs/monitoring/file_watcher_factory.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
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
} // namespace

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

TEST_CASE("monitoring windows_file_watcher: saturated admission remains non-blocking and reports sticky dirty")
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
