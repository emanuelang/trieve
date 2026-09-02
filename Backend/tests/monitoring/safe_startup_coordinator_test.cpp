#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/safe_startup_coordinator.h"

#include <catch2/catch_test_macros.hpp>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {
WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }

class Coverage final : public IStartupCoverageSink {
public:
    CoverageDelivery accept(const StartupCoverage& item) override {
        items.push_back(item);
        return refuseObservations && std::holds_alternative<FileObservation>(item) ? CoverageDelivery::Refused : next;
    }
    CoverageDelivery next{CoverageDelivery::Accepted};
    bool refuseObservations{};
    std::vector<StartupCoverage> items;
};

class Session final : public IWatcherSession {
public:
    explicit Session(IWatcherIngressSink& ingress) : ingress_(ingress) {}
    BarrierRequestOutcome requestBarrier() noexcept override {
        const BarrierId id{++barriers};
        if (emitAroundBarrier) ingress_.accept(WatcherRecord{WatcherSequence{1}, {*rootId, WatcherEventKind::Created, {"/root/pre"}, {}}});
        if (barrierStatus == BarrierRequestStatus::Accepted && emitBarrier) ingress_.accept(BarrierReached{id, WatcherSequence{1}, GapEpoch{epoch}});
        if (emitAroundBarrier) ingress_.accept(WatcherRecord{WatcherSequence{2}, {*rootId, WatcherEventKind::Modified, {"/root/post"}, {}}});
        return {barrierStatus, barrierStatus == BarrierRequestStatus::Accepted ? std::optional<BarrierId>{id} : std::nullopt};
    }
    CancelOutcome requestCancellation() noexcept override { return CancelOutcome::Requested; }
    StopOutcome stopAndJoin() noexcept override { joined = true; return StopOutcome::Stopped; }
    IWatcherIngressSink& ingress_; std::optional<RootId> rootId; std::uint64_t barriers{}; std::uint64_t epoch{}; bool joined{}; bool emitBarrier{true}; bool emitAroundBarrier{}; BarrierRequestStatus barrierStatus{BarrierRequestStatus::Accepted};
};

class Watcher final : public IFileWatcher {
public:
    WatcherStartOutcome start(const WatchRootConfig& root, IWatcherIngressSink& ingress) override {
        ++starts; session = std::make_shared<Session>(ingress); session->rootId = root.rootId; session->epoch = emitDirty ? 1 : barrierEpoch; session->emitAroundBarrier = emitAroundBarrier; session->emitBarrier = emitBarrier; session->barrierStatus = barrierStatus;
        if (emitDirty) ingress.accept(RootDirty{root.rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
        if (emitRecordAtStart) ingress.accept(WatcherRecord{WatcherSequence{1}, {root.rootId, WatcherEventKind::Created, {"/root/race"}, {}}});
        return {status, session};
    }
    WatcherStartStatus status{WatcherStartStatus::Started}; bool emitDirty{}; bool emitRecordAtStart{}; bool emitAroundBarrier{}; bool emitBarrier{true}; GapEpoch barrierEpoch{}; BarrierRequestStatus barrierStatus{BarrierRequestStatus::Accepted}; unsigned starts{}; std::shared_ptr<Session> session;
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

TEST_CASE("monitoring: snapshot races and barriers preserve serialized ingress order")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.emitRecordAtStart = true; watcher.emitAroundBarrier = true;
    SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
    const auto outcome = coordinator.start(root(), {}, {});
    REQUIRE(outcome.status == StartupStatus::Healthy);
    REQUIRE(coverage.items.size() == 5);
    REQUIRE(std::get<WatcherRecord>(std::get<WatcherIngress>(coverage.items[0])).event.path.utf8 == "/root/race");
    REQUIRE(std::holds_alternative<FileObservation>(coverage.items[1]));
    REQUIRE(std::get<WatcherRecord>(std::get<WatcherIngress>(coverage.items[2])).event.path.utf8 == "/root/pre");
    REQUIRE(std::holds_alternative<BarrierReached>(std::get<WatcherIngress>(coverage.items[3])));
    REQUIRE(std::get<WatcherRecord>(std::get<WatcherIngress>(coverage.items[4])).event.path.utf8 == "/root/post");
}

TEST_CASE("monitoring: nonhealthy exits retain sticky obligations and join the watcher")
{
    SECTION("sink refusal") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); coverage.refuseObservations = true;
        SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
        const auto outcome = coordinator.start(root(), {}, {});
        REQUIRE(outcome.status == StartupStatus::Degraded);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::SinkRefusal)) != 0);
        REQUIRE(watcher.session->joined);
    }
    SECTION("barrier request failure") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.barrierStatus = BarrierRequestStatus::Busy;
        SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
        const auto outcome = coordinator.start(root(), {}, {});
        REQUIRE(outcome.status == StartupStatus::Degraded);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::BarrierFailure)) != 0);
        REQUIRE(watcher.session->joined);
    }
    SECTION("barrier drain failure") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.emitBarrier = false;
        SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
        const auto outcome = coordinator.start(root(), {}, {});
        REQUIRE(outcome.status == StartupStatus::Degraded);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::BarrierFailure)) != 0);
        REQUIRE(watcher.session->joined);
    }
    SECTION("root scan failure") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; FixtureView missing;
        SafeStartupCoordinator coordinator(missing, paths, clock, watcher, coverage);
        const auto outcome = coordinator.start(root(), {}, {});
        REQUIRE(outcome.status == StartupStatus::Degraded);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::ScanFailure)) != 0);
        REQUIRE(watcher.session->joined);
    }
}

TEST_CASE("monitoring: gaps during reconciliation and cancellations remain pending across restart")
{
    SECTION("new gap during reconciliation") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.emitDirty = true; unsigned checkpoints{};
        SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
        const auto outcome = coordinator.start(root(), {.checkpointHook = [&](ScanCheckpoint, const RelativePath&) { if (++checkpoints == 4) watcher.session->ingress_.accept(RootDirty{root().rootId, GapEpoch{2}, static_cast<std::uint32_t>(DirtyReason::DecodeGap)}); return true; }}, {});
        REQUIRE(outcome.status == StartupStatus::Degraded);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::OsOverflow)) != 0);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::DecodeGap)) != 0);
    }
    SECTION("snapshot cancellation then watcher-first restart") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); std::stop_source stop;
        SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
        const auto cancelled = coordinator.start(root(), {.checkpointHook = [&](ScanCheckpoint, const RelativePath&) { stop.request_stop(); return true; }}, stop.get_token());
        REQUIRE(cancelled.status == StartupStatus::Cancelled);
        REQUIRE(cancelled.pending);
        watcher.barrierEpoch = 1;
        const auto restarted = coordinator.start(root(), {}, {});
        REQUIRE(restarted.status == StartupStatus::Healthy);
        REQUIRE(watcher.starts == 2);
        REQUIRE(coverage.items.size() == 4);
        REQUIRE(std::get<FileObservation>(coverage.items[2]).source == ObservationSource::Reconciliation);
    }
    SECTION("reconciliation cancellation") {
        FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto fileSystem = view(); watcher.emitDirty = true; std::stop_source stop; unsigned checkpoints{};
        SafeStartupCoordinator coordinator(fileSystem, paths, clock, watcher, coverage);
        const auto outcome = coordinator.start(root(), {.checkpointHook = [&](ScanCheckpoint, const RelativePath&) { if (++checkpoints == 4) stop.request_stop(); return true; }}, stop.get_token());
        REQUIRE(outcome.status == StartupStatus::Cancelled);
        REQUIRE(outcome.health == RootHealth::Cancelled);
        REQUIRE((outcome.pending->reasons & static_cast<std::uint32_t>(DirtyReason::Cancellation)) != 0);
        REQUIRE(watcher.session->joined);
    }
}
