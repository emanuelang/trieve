#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/file_monitor_service.h"

#include <catch2/catch_test_macros.hpp>

#include <future>
#include <functional>
#include <thread>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {
WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }
WatchRootConfig foreignRoot() { return {*RootId::create("foreign"), {"/foreign"}, {}}; }

class Coverage final : public IStartupCoverageSink {
public:
    CoverageDelivery accept(const StartupCoverage& item) override { items.push_back(item); if (onAccept) onAccept(item); return deliveryFor ? deliveryFor(item) : CoverageDelivery::Accepted; }
    std::vector<StartupCoverage> items; std::function<void(const StartupCoverage&)> onAccept; std::function<CoverageDelivery(const StartupCoverage&)> deliveryFor;
};

class Obligations final : public IReconciliationObligationReader {
public:
    explicit Obligations(std::optional<PendingReconciliation> pending) : pending_(std::move(pending)) {}
    std::optional<PendingReconciliation> pendingReconciliation(const RootId&) const override { return pending_; }
private:
    std::optional<PendingReconciliation> pending_;
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

class Drain final : public IShutdownDrain {
public:
    explicit Drain(std::vector<DrainStepProgress> progress) : progress_(std::move(progress)) {}
    DrainStepProgress drainOne() noexcept override { return progress_.at(attempts++); }
    void releaseLease() noexcept override { ++releases; }

    std::vector<DrainStepProgress> progress_;
    std::size_t attempts{};
    std::size_t releases{};
};

class Reconciliation final : public IReconciliationExecutor {
public:
    explicit Reconciliation(std::vector<std::pair<ReconciliationStepResult, bool>> results) : results_(std::move(results)) {}
    ReconciliationExecution step(const RootId& rootId, GapEpoch epoch) override {
        const auto result = results_.at(calls++);
        if (duringStep) duringStep();
        return {returnedRoot ? *returnedRoot : rootId, returnedEpoch.value_or(epoch), result.first, result.second};
    }
    std::vector<std::pair<ReconciliationStepResult, bool>> results_; std::optional<RootId> returnedRoot; std::optional<GapEpoch> returnedEpoch;
    std::function<void()> duringStep; std::size_t calls{};
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
    Obligations obligations(PendingReconciliation{root().rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config(), nullptr, nullptr, &obligations);
    RootHealth observed = RootHealth::Stopped;
    watcher.barrierEpoch = GapEpoch{1};
    watcher.duringStart = [&] { observed = service.status(root().rootId).health; };

    const auto outcome = service.start(root(), {});
    REQUIRE(observed == RootHealth::Starting);
    REQUIRE(outcome.status == FileMonitorStartStatus::Degraded);
    REQUIRE(outcome.health == RootHealth::Dirty);
    REQUIRE_FALSE(service.status(root().rootId).healthy);
    REQUIRE(watcher.starts == 1);
    REQUIRE(coverage.items.size() == 2);
    REQUIRE(service.status(root().rootId).diagnostic.size() <= 32);
}

TEST_CASE("monitoring: file monitor fails closed for a foreign recovered obligation", "[phase4.u7]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    Obligations obligations(PendingReconciliation{foreignRoot().rootId, GapEpoch{1}, static_cast<std::uint32_t>(DirtyReason::OsOverflow)});
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config(), nullptr, nullptr, &obligations);
    watcher.barrierEpoch = GapEpoch{1};

    const auto outcome = service.start(root(), {});
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

    const auto invalid = service.stop(DrainBudget{std::nullopt});
    REQUIRE(invalid.status == FileMonitorStopStatus::InvalidBudget);
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

TEST_CASE("monitoring: file monitor drains only its explicit finite shutdown budget", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Drain drain({{true, true}, {true, false}});
    FileMonitorService service(startup, config(), &drain);
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    const auto stopped = service.stop(DrainBudget{3});
    const auto snapshot = service.status(root().rootId);
    REQUIRE(stopped.status == FileMonitorStopStatus::Stopped);
    REQUIRE(stopped.drainAttempts == 2);
    REQUIRE(stopped.drainCompleted == 2);
    REQUIRE_FALSE(stopped.drainBudgetExhausted);
    REQUIRE(drain.attempts == 2);
    REQUIRE(drain.releases == 1);
    REQUIRE(watcher.session->cancelled);
    REQUIRE(watcher.session->joined);
    REQUIRE(snapshot.health == RootHealth::Stopped);
    REQUIRE(snapshot.pendingReconciliation);
    REQUIRE_FALSE(snapshot.acceptingWork);
    REQUIRE(snapshot.diagnostic.size() <= 32);
    REQUIRE(service.step() == FileMonitorStepStatus::Stopped);
}
TEST_CASE("monitoring: file monitor rejects zero drain budget and reports exhausted recovery", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Drain drain({{true, true}, {true, false}});
    FileMonitorService service(startup, config(), &drain);
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    const auto invalid = service.stop(DrainBudget{0});
    REQUIRE(invalid.status == FileMonitorStopStatus::InvalidBudget);
    REQUIRE(invalid.watcher == StopOutcome::NotAttempted);
    REQUIRE_FALSE(watcher.session->cancelled);
    REQUIRE_FALSE(watcher.session->joined);
    const auto stopped = service.stop(DrainBudget{1});
    REQUIRE(stopped.drainAttempts == 1);
    REQUIRE(stopped.drainCompleted == 1);
    REQUIRE(stopped.drainBudgetExhausted);
    REQUIRE(drain.attempts == 1);
    REQUIRE(drain.releases == 1);
    REQUIRE(service.status(root().rootId).pendingReconciliation);
    FixturePaths compatibilityPaths; FixtureClock compatibilityClock; Watcher compatibilityWatcher; Coverage compatibilityCoverage; auto compatibilityFiles = view();
    SafeStartupCoordinator compatibilityStartup(compatibilityFiles, compatibilityPaths, compatibilityClock, compatibilityWatcher, compatibilityCoverage);
    FileMonitorService compatibility(compatibilityStartup, config());
    REQUIRE(compatibility.start(root(), {}).status == FileMonitorStartStatus::Started);
    REQUIRE(compatibility.stop() == StopOutcome::Stopped);
}
TEST_CASE("monitoring: file monitor pauses for soft quota and keeps hard refusal dirty", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    service.updateQuota({true, false, false, false, true});
    auto snapshot = service.status(root().rootId);
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    REQUIRE_FALSE(snapshot.healthy);
    REQUIRE(snapshot.publicationPrioritized);
    REQUIRE_FALSE(snapshot.saturated);
    service.updateQuota({false, false, false, true, false});
    snapshot = service.status(root().rootId);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE_FALSE(snapshot.publicationPrioritized);
    REQUIRE_FALSE(snapshot.saturated);
    REQUIRE(snapshot.healthy);
    service.updateQuota({false, true, true, false, true});
    snapshot = service.status(root().rootId);
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    REQUIRE(snapshot.health == RootHealth::Dirty);
    REQUIRE(snapshot.pendingReconciliation);
    REQUIRE(snapshot.saturated);
    REQUIRE_FALSE(snapshot.healthy);
}
TEST_CASE("monitoring: file monitor resumes only after every soft quota metric is strictly below threshold", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    OutboxQuotaController quotas({10, 100, 20, 200});
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    service.updateQuota(quotas.update({10, 0}));
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    service.updateQuota(quotas.update({7, 100}));
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    REQUIRE(service.status(root().rootId).publicationPrioritized);
    service.updateQuota(quotas.update({7, 74}));
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(service.status(root().rootId).healthy);
}
TEST_CASE("monitoring: hard quota recovery re-admits pending reconciliation without declaring health", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage); FileMonitorService service(startup, config());
    OutboxQuotaController quotas({10, 100, 20, 200});
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    service.updateQuota(quotas.update({20, 0}));
    REQUIRE(service.status(root().rootId).health == RootHealth::Dirty);
    REQUIRE(service.status(root().rootId).pendingReconciliation);
    service.updateQuota(quotas.update({10, 0}));
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    REQUIRE(service.status(root().rootId).health == RootHealth::Dirty);
    service.updateQuota(quotas.update({7, 74}));
    const auto recovered = service.status(root().rootId);
    REQUIRE(service.step() == FileMonitorStepStatus::ReconciliationUnavailable);
    REQUIRE(recovered.health == RootHealth::Dirty);
    REQUIRE(recovered.pendingReconciliation);
    REQUIRE_FALSE(recovered.healthy);
}
TEST_CASE("monitoring: reconciliation completion requires matching root and epoch", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Reconciliation reconciliation({{{ReconciliationStepStatus::AwaitingBarrier, 1}, true}, {{ReconciliationStepStatus::AwaitingBarrier, 1}, true}, {{ReconciliationStepStatus::AwaitingBarrier, 1}, true}});
    FileMonitorService service(startup, config(), nullptr, &reconciliation);
    OutboxQuotaController quotas({10, 100, 20, 200});
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    service.updateQuota(quotas.update({20, 0}));
    service.updateQuota(quotas.update({7, 74}));
    reconciliation.returnedRoot = foreignRoot().rootId;
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(service.status(root().rootId).pendingReconciliation);
    reconciliation.returnedRoot.reset();
    reconciliation.returnedEpoch = 0;
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(service.status(root().rootId).pendingReconciliation);
    reconciliation.returnedEpoch.reset();
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE_FALSE(service.status(root().rootId).pendingReconciliation);
    REQUIRE(service.status(root().rootId).healthy);
}
TEST_CASE("monitoring: quota recovery executes reconciliation and clears only fenced completion", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Reconciliation reconciliation({{{ReconciliationStepStatus::Dirty, 1}, false}, {{ReconciliationStepStatus::AwaitingBarrier, 1}, true}});
    FileMonitorService service(startup, config(), nullptr, &reconciliation);
    OutboxQuotaController quotas({10, 100, 20, 200});
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    service.updateQuota(quotas.update({20, 0}));
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused);
    REQUIRE(reconciliation.calls == 0);
    service.updateQuota(quotas.update({7, 74}));
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(reconciliation.calls == 1);
    REQUIRE(service.status(root().rootId).pendingReconciliation);
    REQUIRE_FALSE(service.status(root().rootId).healthy);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(reconciliation.calls == 2);
    REQUIRE_FALSE(service.status(root().rootId).pendingReconciliation);
    REQUIRE(service.status(root().rootId).healthy);
}
TEST_CASE("monitoring: concurrent hard quota keeps the reconciliation obligation", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view();
    SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Reconciliation reconciliation({{{ReconciliationStepStatus::AwaitingBarrier, 1}, true}});
    FileMonitorService service(startup, config(), nullptr, &reconciliation); OutboxQuotaController quotas({10, 100, 20, 200});
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    service.updateQuota(quotas.update({20, 0}));
    service.updateQuota(quotas.update({7, 74}));
    reconciliation.duringStep = [&] { service.updateQuota(quotas.update({20, 0})); };
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted);
    REQUIRE(reconciliation.calls == 1);
    REQUIRE(service.status(root().rootId).pendingReconciliation);
    REQUIRE_FALSE(service.status(root().rootId).healthy);
}
TEST_CASE("monitoring: quota recovery cannot overtake hard-loss dirty publication", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view(); SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Reconciliation reconciliation({{{ReconciliationStepStatus::AwaitingBarrier, 1}, true}}); FileMonitorService service(startup, config(), nullptr, &reconciliation); OutboxQuotaController quotas({10, 100, 20, 200});
    std::promise<void> publicationEntered; std::promise<void> releasePublication;
    const auto released = releasePublication.get_future().share();
    coverage.onAccept = [&](const StartupCoverage& item) { const auto* ingress = std::get_if<WatcherIngress>(&item); const auto* dirty = ingress ? std::get_if<RootDirty>(ingress) : nullptr; if (dirty && dirty->reasons == static_cast<std::uint32_t>(DirtyReason::QueueSaturation)) { publicationEntered.set_value(); released.wait(); } };
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    std::thread hardQuota([&] { service.updateQuota(quotas.update({20, 0})); });
    publicationEntered.get_future().wait(); service.updateQuota(quotas.update({7, 74}));
    const auto stepBeforePublicationCompletes = service.step(); const auto beforePublicationCompletes = service.status(root().rootId);
    releasePublication.set_value(); hardQuota.join();
    REQUIRE(stepBeforePublicationCompletes == FileMonitorStepStatus::QuotaPaused); REQUIRE(beforePublicationCompletes.health == RootHealth::Dirty);
    REQUIRE(beforePublicationCompletes.pendingReconciliation); REQUIRE_FALSE(beforePublicationCompletes.healthy); REQUIRE(reconciliation.calls == 0);
    REQUIRE(service.step() == FileMonitorStepStatus::Admitted); REQUIRE(service.status(root().rootId).healthy);
}
TEST_CASE("monitoring: saturation publication refusal and synchronous re-entry retain the admission fence", "[phase4.u9]")
{
    FixturePaths paths; FixtureClock clock; Watcher watcher; Coverage coverage; auto files = view(); SafeStartupCoordinator startup(files, paths, clock, watcher, coverage);
    Reconciliation reconciliation({{{ReconciliationStepStatus::AwaitingBarrier, 1}, true}}); FileMonitorService service(startup, config(), nullptr, &reconciliation); OutboxQuotaController quotas({10, 100, 20, 200});
    std::size_t publications{};
    coverage.onAccept = [&](const StartupCoverage& item) { const auto* ingress = std::get_if<WatcherIngress>(&item); const auto* dirty = ingress ? std::get_if<RootDirty>(ingress) : nullptr; if (dirty && dirty->reasons == static_cast<std::uint32_t>(DirtyReason::QueueSaturation)) { ++publications; if (publications < 4) { REQUIRE_FALSE(service.updateQuota(quotas.update({20, 0}))); const auto nestedStop = service.stop(DrainBudget{1}); REQUIRE(nestedStop.status == FileMonitorStopStatus::NotAttempted); REQUIRE(nestedStop.watcher == StopOutcome::NotAttempted); } } };
    REQUIRE(service.start(root(), {}).status == FileMonitorStartStatus::Started);
    REQUIRE(service.updateQuota(quotas.update({20, 0})));
    REQUIRE(publications == 1); REQUIRE(service.status(root().rootId).pendingReconciliation); REQUIRE_FALSE(service.status(root().rootId).healthy);
    coverage.deliveryFor = [](const StartupCoverage&) { return CoverageDelivery::Refused; };
    REQUIRE_FALSE(service.updateQuota(quotas.update({20, 0})));
    service.updateQuota(quotas.update({7, 74}));
    REQUIRE(service.step() == FileMonitorStepStatus::QuotaPaused); REQUIRE(reconciliation.calls == 0); REQUIRE_FALSE(service.status(root().rootId).healthy);
}
