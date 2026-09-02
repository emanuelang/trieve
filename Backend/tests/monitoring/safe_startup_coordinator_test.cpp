#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/safe_startup_coordinator.h"

#include <catch2/catch_test_macros.hpp>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {
WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }

class Coverage final : public IStartupCoverageSink {
public:
    CoverageDelivery accept(const StartupCoverage& item) override { items.push_back(item); return next; }
    CoverageDelivery next{CoverageDelivery::Accepted};
    std::vector<StartupCoverage> items;
};

class Session final : public IWatcherSession {
public:
    explicit Session(IWatcherIngressSink& ingress) : ingress_(ingress) {}
    BarrierRequestOutcome requestBarrier() noexcept override {
        const BarrierId id{++barriers};
        ingress_.accept(BarrierReached{id, WatcherSequence{barriers}, GapEpoch{epoch}});
        return {BarrierRequestStatus::Accepted, id};
    }
    CancelOutcome requestCancellation() noexcept override { return CancelOutcome::Requested; }
    StopOutcome stopAndJoin() noexcept override { joined = true; return StopOutcome::Stopped; }
    IWatcherIngressSink& ingress_; std::uint64_t barriers{}; std::uint64_t epoch{}; bool joined{};
};

class Watcher final : public IFileWatcher {
public:
    WatcherStartOutcome start(const WatchRootConfig& root, IWatcherIngressSink& ingress) override {
        session = std::make_shared<Session>(ingress); session->epoch = emitDirty ? 1 : 0;
        if (emitDirty) ingress.accept(RootDirty{root.rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
        return {status, session};
    }
    WatcherStartStatus status{WatcherStartStatus::Started}; bool emitDirty{}; std::shared_ptr<Session> session;
};

FixtureView view() { FixtureView result; result.listings["/root"] = std::vector<FileSystemEntry>{{{"/root/a.txt"}, FileSystemEntryKind::RegularFile}}; result.metadataResults["/root/a.txt"] = FileMetadata{}; return result; }
} // namespace

TEST_CASE("monitoring: watcher-first coordinator gates health on a later barrier")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view();
    SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
    const auto outcome = coordinator.start(root(), {}, {});
    REQUIRE(outcome.status == StartupStatus::Healthy);
    REQUIRE(outcome.health == RootHealth::Healthy);
    REQUIRE_FALSE(outcome.pending);
    REQUIRE(coverage.items.size() == 2);
    REQUIRE(std::holds_alternative<FileObservation>(coverage.items.front()));
    REQUIRE(std::holds_alternative<BarrierReached>(std::get<WatcherIngress>(coverage.items.back())));
}

TEST_CASE("monitoring: watcher start failures retain a reconciliation obligation")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.status = WatcherStartStatus::NativeFailure;
    SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
    const auto outcome = coordinator.start(root(), {}, {});
    REQUIRE(outcome.status == StartupStatus::Degraded);
    REQUIRE(outcome.health == RootHealth::Degraded);
    REQUIRE(outcome.pending);
    REQUIRE(outcome.pending->reasons == static_cast<std::uint32_t>(DirtyReason::NativeFailure));
}

TEST_CASE("monitoring: cancelled startup joins the watcher and exposes its obligation")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); std::stop_source cancelled; cancelled.request_stop();
    SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
    const auto outcome = coordinator.start(root(), {}, cancelled.get_token());
    REQUIRE(outcome.status == StartupStatus::Cancelled);
    REQUIRE(outcome.health == RootHealth::Cancelled);
    REQUIRE(outcome.pending->reasons == static_cast<std::uint32_t>(DirtyReason::Cancellation));
    REQUIRE(watcher.session->joined);
}

TEST_CASE("monitoring: a reconciliation scan clears only the unchanged dirty epoch after its barrier")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.emitDirty = true;
    SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
    const auto outcome = coordinator.start(root(), {}, {});
    REQUIRE(outcome.status == StartupStatus::Healthy);
    REQUIRE(outcome.health == RootHealth::Healthy);
    REQUIRE_FALSE(outcome.pending);
    REQUIRE(coverage.items.size() == 4);
}
