#include "semantic_fs/monitoring/i_clock.h"
#include "semantic_fs/monitoring/i_file_change_sink.h"
#include "semantic_fs/monitoring/i_file_observation_sink.h"
#include "semantic_fs/monitoring/i_file_watcher.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using namespace semantic_fs::monitoring;

namespace {

class FakeClock final : public IClock {
public:
    UtcTimestamp utcNow() const override { return {42}; }
};
class FakeObservationSink final : public IFileObservationSink {
public:
    ObservationDelivery observe(const FileObservation&) override { return ObservationDelivery::Accepted; }
};
class FakeChangeSink final : public IFileChangeSink {
public:
    PublishResult publish(const FileChange&) override { return PublishResult::Accepted; }
};
class FakeWatcher final : public IFileWatcher {
public:
    WatcherStartResult start(const WatchRootConfig&, WatcherCallback) override { return WatcherStartResult::Started; }
    void stop() override {}
};
static_assert(std::is_same_v<decltype(&IFileObservationSink::observe), ObservationDelivery (IFileObservationSink::*)(const FileObservation&)>);
static_assert(!std::is_same_v<decltype(&IFileObservationSink::observe), ObservationDelivery (IFileObservationSink::*)(const FileChange&)>);

FileChange makeChange()
{
    return {kFileChangeSchemaVersion, *EventId::create("event"), *RootId::create("root"), *ObservationId::create("observation"), *Generation::create(1), ChangeKind::Created, {}, {}, {}, {42}, ObservationSource::Watcher};
}

} // namespace

TEST_CASE("monitoring contracts compile against portable fakes")
{
    FakeClock clock;
    FakeObservationSink observations;
    FakeChangeSink changes;
    FakeWatcher watcher;
    const FileObservation observation{*RootId::create("root"), {}, {}, clock.utcNow(), ObservationKind::Discovered, ObservationSource::InitialScan};

    REQUIRE(observations.observe(observation) == ObservationDelivery::Accepted);
    REQUIRE(changes.publish(makeChange()) == PublishResult::Accepted);
    const WatchRootConfig config{*RootId::create("root"), {"C:/root"}, {}};
    REQUIRE(watcher.start(config, {}) == WatcherStartResult::Started);
}

TEST_CASE("monitoring identifiers and generation reject invalid durable values")
{
    static_assert(noexcept(RootId::isValid("root")));
    REQUIRE(RootId::isValid("root"));
    REQUIRE_FALSE(RootId::isValid(""));
    REQUIRE_FALSE(RootId::isValid("\xC0\x80"));
    REQUIRE_FALSE(RootId::create(""));
    REQUIRE_FALSE(RootId::create("\xC0\x80"));
    REQUIRE_FALSE(Generation::create(0));
    REQUIRE(EventId::create("evento")->value() == "evento");
    REQUIRE(Generation::create(7)->value == 7);
}
