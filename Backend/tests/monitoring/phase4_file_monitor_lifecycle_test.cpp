#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/file_monitor_service.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {
WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }
WatchRootConfig foreignRoot() { return {*RootId::create("foreign"), {"/foreign"}, {}}; }

class Coverage final : public IStartupCoverageSink {
public:
    CoverageDelivery accept(const StartupCoverage& item) override { items.push_back(item); return CoverageDelivery::Accepted; }
    std::vector<StartupCoverage> items;
};

class Session final : public IWatcherSession {
public:
    explicit Session(IWatcherIngressSink& ingress, RootId rootId) : ingress_(ingress), rootId_(std::move(rootId)) {}
    BarrierRequestOutcome requestBarrier() noexcept override {
        const BarrierId id{1};
        ingress_.accept(BarrierReached{id, 0, GapEpoch{epoch}});
        if (duringBarrier) duringBarrier();
        return {BarrierRequestStatus::Accepted, id};
    }
    CancelOutcome requestCancellation() noexcept override { cancelled = true; return CancelOutcome::Requested; }
    StopOutcome stopAndJoin() noexcept override { joined = true; return StopOutcome::Stopped; }
    IWatcherIngressSink& ingress_; RootId rootId_; std::function<void()> duringBarrier; GapEpoch epoch{}; bool cancelled{}; bool joined{};
};

class Watcher final : public IFileWatcher {
public:
    WatcherStartOutcome start(const WatchRootConfig& config, IWatcherIngressSink& ingress) override {
        ++starts;
        if (duringStart) duringStart();
        session = std::make_shared<Session>(ingress, config.rootId);
        session->epoch = barrierEpoch;
        session->duringBarrier = duringBarrier;
        if (dirtyOnStart) ingress.accept(RootDirty{config.rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
        return {WatcherStartStatus::Started, session};
    }
    StopOutcome stopAndJoin() noexcept override { watcherStopAndJoinCalled = true; return StopOutcome::Stopped; }
    unsigned starts{}; bool dirtyOnStart{}; bool watcherStopAndJoinCalled{}; GapEpoch barrierEpoch{}; std::function<void()> duringStart; std::function<void()> duringBarrier; std::shared_ptr<Session> session;
};

FixtureView view() {
    FixtureView result;
    result.listings["/root"] = std::vector<FileSystemEntry>{{{"/root/a.txt"}, FileSystemEntryKind::RegularFile}};
    result.metadataResults["/root/a.txt"] = FileMetadata{};
    return result;
}

FileMonitorConfig config() { return {.maximumRoots = 1, .maximumStatusBytes = 32}; }
} // namespace

TEST_CASE("monitoring: file monitor refuses zero or unbounded limits", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);

    FileMonitorService zero(startup, {.maximumRoots = 0, .maximumStatusBytes = 32});
    REQUIRE(zero.start(root(), {}).status == FileMonitorStartStatus::InvalidConfig);

    FileMonitorService unbounded(startup, {.maximumRoots = 1, .maximumStatusBytes = std::nullopt});
    REQUIRE(unbounded.start(root(), {}).status == FileMonitorStartStatus::InvalidConfig);
    REQUIRE(watcher.starts == 0);
}

TEST_CASE("monitoring: file monitor starts watcher first, recovers dirty work, and fences health", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    RootHealth observed = RootHealth::Stopped;
    watcher.barrierEpoch = GapEpoch{1};
    watcher.duringStart = [&] { observed = service.status(root().rootId).health; };

    const auto outcome = service.start(root(), {}, PendingReconciliation{root().rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
    REQUIRE(observed == RootHealth::Starting);
    REQUIRE(outcome.status == FileMonitorStartStatus::Started);
    REQUIRE(outcome.health == RootHealth::Healthy);
    REQUIRE(service.status(root().rootId).healthy);
    REQUIRE(watcher.starts == 1);
    REQUIRE(coverage.items.size() == 4);
    REQUIRE(std::holds_alternative<RootDirty>(std::get<WatcherIngress>(coverage.items.front())));
    REQUIRE(std::get<FileObservation>(coverage.items[2]).source == ObservationSource::Reconciliation);
    REQUIRE(service.status(root().rootId).diagnostic.size() <= 32);
}

TEST_CASE("monitoring: file monitor fails closed for a foreign recovered obligation", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    watcher.barrierEpoch = GapEpoch{1};

    const auto outcome = service.start(root(), {}, PendingReconciliation{foreignRoot().rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
    const auto snapshot = service.status(root().rootId);
    REQUIRE(outcome.status == FileMonitorStartStatus::Degraded);
    REQUIRE(snapshot.health == RootHealth::Dirty);
    REQUIRE(snapshot.pendingReconciliation);
    REQUIRE_FALSE(snapshot.healthy);
    REQUIRE(watcher.starts == 0);
    REQUIRE(coverage.items.empty());
}

TEST_CASE("monitoring: file monitor stops the coordinator-owned watcher", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher coordinatorWatcher; Watcher unrelatedWatcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, coordinatorWatcher, coverage);
    FileMonitorService service(startup, config());

    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    REQUIRE(service.stop() == StopOutcome::Stopped);
    REQUIRE(coordinatorWatcher.session->joined);
    REQUIRE_FALSE(coordinatorWatcher.watcherStopAndJoinCalled);
    REQUIRE_FALSE(unrelatedWatcher.session);
}

TEST_CASE("monitoring: stop during blocked watcher start joins the returned session", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    watcher.duringStart = [&] { REQUIRE(service.stop() == StopOutcome::Stopped); };

    const auto outcome = service.start(root(), {});
    const auto snapshot = service.status(root().rootId);
    REQUIRE(outcome.status == FileMonitorStartStatus::Stopped);
    REQUIRE(snapshot.health == RootHealth::Stopped);
    REQUIRE(snapshot.pendingReconciliation);
    REQUIRE_FALSE(snapshot.healthy);
    REQUIRE(watcher.session->cancelled);
    REQUIRE(watcher.session->joined);
    REQUIRE_FALSE(watcher.watcherStopAndJoinCalled);
}

TEST_CASE("monitoring: stop preserves terminal state over a pending startup outcome", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    watcher.duringBarrier = [&] { REQUIRE(service.stop() == StopOutcome::Stopped); };

    const auto outcome = service.start(root(), {});
    const auto snapshot = service.status(root().rootId);
    REQUIRE(outcome.status == FileMonitorStartStatus::Stopped);
    REQUIRE(snapshot.health == RootHealth::Stopped);
    REQUIRE(snapshot.pendingReconciliation);
    REQUIRE_FALSE(snapshot.acceptingWork);
    REQUIRE_FALSE(snapshot.healthy);
    REQUIRE(watcher.session->joined);
}

TEST_CASE("monitoring: file monitor admits one start and retains an obligation when stopped", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());

    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::AlreadyStarted);
    REQUIRE(watcher.starts == 1);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(service.stop() == StopOutcome::Stopped);
    const auto snapshot = service.status(root().rootId);
    REQUIRE(snapshot.health == RootHealth::Stopped);
    REQUIRE(snapshot.pendingReconciliation);
    REQUIRE_FALSE(snapshot.acceptingWork);
    REQUIRE(service.step() == FileMonitorStepStatus::Stopped);
    REQUIRE(watcher.session->joined);
}
