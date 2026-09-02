#include "semantic_fs/monitoring/i_file_watcher.h"

#include <catch2/catch_test_macros.hpp>

using namespace semantic_fs::monitoring;

namespace {
class CapturingIngress final : public IWatcherIngressSink {
public:
    IngressDelivery accept(WatcherIngress ingress) override { received = std::move(ingress); return IngressDelivery::Accepted; }
    std::optional<WatcherIngress> received;
};

class Session final : public IWatcherSession {
public:
    BarrierRequestOutcome requestBarrier() noexcept override { return {BarrierRequestStatus::Accepted, BarrierId{1}}; }
    CancelOutcome requestCancellation() noexcept override { return CancelOutcome::Requested; }
    StopOutcome stopAndJoin() noexcept override { joined = true; return StopOutcome::Stopped; }
    bool joined{};
};

class Watcher final : public IFileWatcher {
public:
    WatcherStartOutcome start(const WatchRootConfig&, IWatcherIngressSink&) override { return {WatcherStartStatus::Started, session}; }
    std::shared_ptr<Session> session = std::make_shared<Session>();
};

WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }
} // namespace

TEST_CASE("monitoring: watcher contracts carry ordered ingress and joined lifecycle")
{
    CapturingIngress ingress;
    const WatcherRecord record{WatcherSequence{3}, {*RootId::create("root"), WatcherEventKind::Created, {"/root/a"}, {}}};
    REQUIRE(ingress.accept(record) == IngressDelivery::Accepted);
    REQUIRE(std::get<WatcherRecord>(*ingress.received).sequence == 3);

    Watcher watcher;
    const auto started = watcher.start(root(), ingress);
    REQUIRE(started.status == WatcherStartStatus::Started);
    REQUIRE(started.session->requestBarrier().id == BarrierId{1});
    REQUIRE(started.session->requestCancellation() == CancelOutcome::Requested);
    REQUIRE(started.session->stopAndJoin() == StopOutcome::Stopped);
    REQUIRE(watcher.session->joined);
}

TEST_CASE("monitoring: dirty ingress preserves a sticky root epoch")
{
    CapturingIngress ingress;
    const RootDirty dirty{*RootId::create("root"), GapEpoch{4}, static_cast<std::uint32_t>(DirtyReason::QueueSaturation)};
    REQUIRE(ingress.accept(dirty) == IngressDelivery::Accepted);
    const auto& received = std::get<RootDirty>(*ingress.received);
    REQUIRE(received.epoch == 4);
    REQUIRE(received.reasons == static_cast<std::uint32_t>(DirtyReason::QueueSaturation));
}
