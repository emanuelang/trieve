#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/durable_startup_coverage_sink.h"
#include "semantic_fs/monitoring/file_monitor_service.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/reconciliation_service.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <stdexcept>
#include <thread>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {
class FakeClock final : public IClock {
public:
    UtcTimestamp utcNow() const override { return {}; }
    MonotonicTimestamp monotonicNow() const override { return now; }

    MonotonicTimestamp now{};
};

RootId root(std::string_view value) { return *RootId::create(value); }
WatchRootConfig watchedRoot() { return {root("root"), {"/root"}, {}}; }

class Coverage final : public IStartupCoverageSink {
public:
    CoverageDelivery accept(const StartupCoverage&) override { return CoverageDelivery::Accepted; }
};

class Session final : public IWatcherSession {
public:
    explicit Session(IWatcherIngressSink& ingress) : ingress_(ingress) {}
    BarrierRequestOutcome requestBarrier() noexcept override {
        ingress_.accept(BarrierReached{1, 0, 0});
        return {BarrierRequestStatus::Accepted, BarrierId{1}};
    }
    CancelOutcome requestCancellation() noexcept override { return CancelOutcome::Requested; }
    StopOutcome stopAndJoin() noexcept override { return StopOutcome::Stopped; }
private:
    IWatcherIngressSink& ingress_;
};

class Watcher final : public IFileWatcher {
public:
    WatcherStartOutcome start(const WatchRootConfig&, IWatcherIngressSink& ingress) override {
        ++starts;
        return {WatcherStartStatus::Started, std::make_shared<Session>(ingress)};
    }
    StopOutcome stopAndJoin() noexcept override { return StopOutcome::Stopped; }
    unsigned starts{};
};

class Executor final : public IReconciliationExecutor {
public:
    ReconciliationExecution step(const RootId& rootId, GapEpoch epoch) override {
        ++calls;
        epochs.push_back(epoch);
        return {rootId, epoch, {ReconciliationStepStatus::AwaitingBarrier, 0}, true};
    }
    unsigned calls{};
    std::vector<GapEpoch> epochs;
};

class BlockingExecutor final : public IReconciliationExecutor {
public:
    ReconciliationExecution step(const RootId& rootId, GapEpoch epoch) override {
        std::unique_lock lock(mutex_);
        ++calls;
        entered_.notify_all();
        released_.wait(lock, [this] { return released; });
        return {rootId, epoch, {ReconciliationStepStatus::AwaitingBarrier, 0}, true};
    }

    bool waitForEntry() {
        std::unique_lock lock(mutex_);
        return entered_.wait_for(lock, std::chrono::seconds(1), [this] { return calls != 0; });
    }

    void release() {
        std::lock_guard lock(mutex_);
        released = true;
        released_.notify_all();
    }

    unsigned callCount() const {
        std::lock_guard lock(mutex_);
        return calls;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable entered_;
    std::condition_variable released_;
    unsigned calls{};
    bool released{};
};

class ThrowingExecutor final : public IReconciliationExecutor {
public:
    ReconciliationExecution step(const RootId& rootId, GapEpoch epoch) override {
        if (++calls == 1) throw std::runtime_error("reconciliation executor failure");
        return {rootId, epoch, {ReconciliationStepStatus::AwaitingBarrier, 0}, true};
    }

    unsigned calls{};
};

class Ids final : public IIdSource {
public:
    std::string nextObservationId() override { return "observation-" + std::to_string(++observation); }
    std::string nextEventId() override { return "event-" + std::to_string(++event); }
private:
    unsigned observation{};
    unsigned event{};
};

std::filesystem::path database()
{
    static unsigned sequence{};
    const auto path = std::filesystem::temp_directory_path() / ("semantic-fs-u10-scheduler-" + std::to_string(++sequence) + ".db");
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
    return path;
}
} // namespace

TEST_CASE("reconciliation scheduler: unifies startup, interval, dirty overflow, and post-saturation admission", "[phase4.u10]")
{
    FakeClock clock;
    ReconciliationScheduler scheduler(clock);
    const auto primary = root("primary");

    REQUIRE(scheduler.request(primary, 1, ReconciliationTrigger::Startup) == ReconciliationAdmissionStatus::Admitted);
    REQUIRE(scheduler.request(primary, 1, ReconciliationTrigger::Interval) == ReconciliationAdmissionStatus::Coalesced);
    REQUIRE(scheduler.request(primary, 2, ReconciliationTrigger::DirtyOverflow) == ReconciliationAdmissionStatus::Coalesced);
    REQUIRE(scheduler.request(primary, 3, ReconciliationTrigger::PostSaturation) == ReconciliationAdmissionStatus::Coalesced);
    REQUIRE(scheduler.activeRunCount() == 1);
    REQUIRE_FALSE(scheduler.complete(primary, 1));
    REQUIRE_FALSE(scheduler.complete(primary, 2));
    REQUIRE(scheduler.complete(primary, 3));

    REQUIRE(scheduler.request(primary, 4, ReconciliationTrigger::DirtyOverflow) == ReconciliationAdmissionStatus::Admitted);
    REQUIRE(scheduler.complete(primary, 4));
    REQUIRE(scheduler.request(primary, 5, ReconciliationTrigger::PostSaturation) == ReconciliationAdmissionStatus::Admitted);
    REQUIRE(scheduler.request(root("secondary"), 1, ReconciliationTrigger::Startup) == ReconciliationAdmissionStatus::Admitted);
    REQUIRE(scheduler.activeRunCount() == 2);
}

TEST_CASE("reconciliation scheduler: admits interval only at the exact fifteen-minute monotonic boundary", "[phase4.u10]")
{
    FakeClock clock;
    ReconciliationScheduler scheduler(clock);
    const auto watched = root("root");

    REQUIRE(scheduler.request(watched, 1, ReconciliationTrigger::Startup) == ReconciliationAdmissionStatus::Admitted);
    REQUIRE(scheduler.complete(watched, 1));

    clock.now.microsecondsSinceOrigin = 15 * 60 * 1'000'000 - 1;
    REQUIRE(scheduler.request(watched, 2, ReconciliationTrigger::Interval) == ReconciliationAdmissionStatus::NotDue);
    clock.now.microsecondsSinceOrigin = 15 * 60 * 1'000'000;
    REQUIRE(scheduler.request(watched, 2, ReconciliationTrigger::Interval) == ReconciliationAdmissionStatus::Admitted);
}

TEST_CASE("reconciliation scheduler: file monitor composes all runtime trigger sources through one admission boundary", "[phase4.u10]")
{
    FakeClock clock;
    FixturePaths paths;
    FixtureView files;
    files.listings["/root"] = std::vector<FileSystemEntry>{};
    Watcher watcher;
    Coverage coverage;
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Executor executor;
    FileMonitorService service(startup, {.maximumRoots = 1, .maximumStatusBytes = 32}, nullptr, &executor);

    REQUIRE(service.start(watchedRoot(), {}).status == FileMonitorStartStatus::Started);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.calls == 1);

    clock.now.microsecondsSinceOrigin = 15 * 60 * 1'000'000 - 1;
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.calls == 1);
    clock.now.microsecondsSinceOrigin = 15 * 60 * 1'000'000;
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.calls == 2);

    REQUIRE(service.updateQuota({false, true, true, true, false}));
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    REQUIRE(executor.calls == 2);
    REQUIRE(service.updateQuota({false, false, false, true, false}));
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.calls == 3);
}

TEST_CASE("reconciliation scheduler: file-backed dirty epoch promotes an active runtime admission exactly once", "[phase4.u10]")
{
    FakeClock clock;
    FixturePaths paths;
    FixtureView files;
    files.listings["/root"] = std::vector<FileSystemEntry>{};
    Ids ids;
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    Watcher watcher;
    DurableStartupCoverageSink coverage(*writer, paths, files, clock, watchedRoot());
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Executor executor;
    FileMonitorService service(startup, {.maximumRoots = 1, .maximumStatusBytes = 32}, nullptr, &executor);

    REQUIRE(service.start(watchedRoot(), {}).status == FileMonitorStartStatus::Started);
    REQUIRE(service.updateQuota({false, true, true, true, false}));
    REQUIRE((writer->dirtyReasonCount(watchedRoot().rootId) & static_cast<std::uint32_t>(DirtyReason::QueueSaturation)) != 0);
    REQUIRE(service.updateQuota({false, false, false, true, false}));
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.epochs == std::vector<GapEpoch>{1});
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.calls == 1);
    REQUIRE((writer->dirtyReasonCount(watchedRoot().rootId) & static_cast<std::uint32_t>(DirtyReason::QueueSaturation)) != 0);
}

TEST_CASE("reconciliation scheduler: file monitor executes a scheduled root only once while a step is in flight", "[phase4.u10]")
{
    FakeClock clock;
    FixturePaths paths;
    FixtureView files;
    files.listings["/root"] = std::vector<FileSystemEntry>{};
    Watcher watcher;
    Coverage coverage;
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    BlockingExecutor executor;
    FileMonitorService service(startup, {.maximumRoots = 1, .maximumStatusBytes = 32}, nullptr, &executor);
    REQUIRE(service.start(watchedRoot(), {}).status == FileMonitorStartStatus::Started);

    std::atomic<bool> secondFinished{};
    std::jthread first([&] { service.step(); });
    REQUIRE(executor.waitForEntry());
    std::jthread second([&] {
        service.step();
        secondFinished = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!secondFinished && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    executor.release();
    first.join();
    second.join();

    REQUIRE(secondFinished);
    REQUIRE(executor.callCount() == 1);

    clock.now.microsecondsSinceOrigin = 15 * 60 * 1'000'000;
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.callCount() == 2);
}

TEST_CASE("reconciliation scheduler: file monitor releases execution admission when the executor throws", "[phase4.u10]")
{
    FakeClock clock;
    FixturePaths paths;
    FixtureView files;
    files.listings["/root"] = std::vector<FileSystemEntry>{};
    Watcher watcher;
    Coverage coverage;
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    ThrowingExecutor executor;
    FileMonitorService service(startup, {.maximumRoots = 1, .maximumStatusBytes = 32}, nullptr, &executor);
    REQUIRE(service.start(watchedRoot(), {}).status == FileMonitorStartStatus::Started);

    REQUIRE_THROWS_AS(service.step(), std::runtime_error);
    REQUIRE(executor.calls == 1);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.calls == 2);
}

TEST_CASE("restart recovery: file monitor reads persisted dirty work into watcher-first startup", "[phase4.u12]")
{
    FakeClock clock;
    FixturePaths paths;
    FixtureView files;
    files.listings["/root"] = std::vector<FileSystemEntry>{};
    Ids ids;
    const auto file = database();
    auto writer = openSqliteCatalogOutbox(file, paths, ids, {});
    REQUIRE(writer->recordCoverage({root("root"), 7, static_cast<std::uint32_t>(DirtyReason::OsOverflow), {}}).status == CoverageStatus::Persisted);
    writer.reset();

    writer = openSqliteCatalogOutbox(file, paths, ids, {});
    Watcher watcher;
    DurableStartupCoverageSink coverage(*writer, paths, files, clock, watchedRoot());
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Executor executor;
    FileMonitorService service(startup, {.maximumRoots = 1, .maximumStatusBytes = 32}, nullptr, &executor, writer.get());

    const auto started = service.start(watchedRoot(), {});
    REQUIRE(watcher.starts == 1);
    REQUIRE(started.status == FileMonitorStartStatus::Degraded);
    REQUIRE(service.status(watchedRoot().rootId).health == RootHealth::Dirty);
    REQUIRE(service.status(watchedRoot().rootId).pendingReconciliation);
    REQUIRE_FALSE(service.status(watchedRoot().rootId).healthy);
    REQUIRE((writer->dirtyReasonCount(watchedRoot().rootId) & static_cast<std::uint32_t>(DirtyReason::OsOverflow)) != 0);

    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.epochs == std::vector<GapEpoch>{7});
    REQUIRE(service.status(watchedRoot().rootId).healthy);
}

TEST_CASE("restart recovery: an absent durable obligation does not degrade watcher-first startup", "[phase4.u12]")
{
    FakeClock clock;
    FixturePaths paths;
    FixtureView files;
    files.listings["/root"] = std::vector<FileSystemEntry>{};
    Ids ids;
    auto writer = openSqliteCatalogOutbox(database(), paths, ids, {});
    Watcher watcher;
    DurableStartupCoverageSink coverage(*writer, paths, files, clock, watchedRoot());
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Executor executor;
    FileMonitorService service(startup, {.maximumRoots = 1, .maximumStatusBytes = 32}, nullptr, &executor, writer.get());

    const auto started = service.start(watchedRoot(), {});
    REQUIRE(watcher.starts == 1);
    REQUIRE(started.status == FileMonitorStartStatus::Started);
    REQUIRE(service.status(watchedRoot().rootId).healthy);
    REQUIRE_FALSE(service.status(watchedRoot().rootId).pendingReconciliation);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(executor.epochs == std::vector<GapEpoch>{0});
}
