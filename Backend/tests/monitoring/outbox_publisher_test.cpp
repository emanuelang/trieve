#include "semantic_fs/monitoring/outbox_publisher.h"

#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_id_source.h"
#include "semantic_fs/monitoring/i_path_semantics.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <filesystem>

using namespace semantic_fs::monitoring;

namespace {
class Paths final : public IPathSemantics { public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override { return AbsolutePath{std::string(value)}; }
    int compareComponent(std::string_view left, std::string_view right) const override { return left == right ? 0 : left < right ? -1 : 1; }
    std::optional<RelativePath> relativeTo(const AbsolutePath&, const AbsolutePath&) const override { return std::nullopt; }
};
class Ids final : public IIdSource { public:
    std::string nextObservationId() override { return "o" + std::to_string(++observation); }
    std::string nextEventId() override { return "e" + std::to_string(++event); }
private: int observation = 0, event = 0; };
class Sink final : public IFileChangeSink { public:
    explicit Sink(ICatalogOutboxWriter* storage = nullptr) : storage(storage) {}
    PublishResult publish(const FileChange& change) override { ++calls; event = std::string(change.eventId.value()); if (storage) reentered = storage->recover({{100}, 1}).status == RecoveryStatus::Ready; return result; }
    PublishResult result = PublishResult::Accepted; int calls = 0; bool reentered = false; std::string event; private: ICatalogOutboxWriter* storage;
};
std::filesystem::path database() { static int n = 0; auto path = std::filesystem::temp_directory_path() / ("semantic-fs-outbox-" + std::to_string(++n) + ".db"); std::error_code error; std::filesystem::remove(path,error); return path; }
TransitionCommand change(std::string path = "report.txt") { return {*RootId::create("root"), {path, {path, {path}}}, FileMetadata{1, {}, {}}, {100}, ChangeKind::Created, ObservationSource::Watcher, {}}; }
}

TEST_CASE("Recoverable outbox publisher: claims before a reentrant sink and acknowledges accepted rows", "[outbox_publisher]")
{
    Paths paths; Ids ids; auto writer = openSqliteCatalogOutbox(database(), paths, ids, {}); REQUIRE(writer->apply(change()).status == MutationStatus::Applied);
    Sink sink(writer.get()); OutboxPublisher publisher(*writer, sink, "publisher");
    REQUIRE(publisher.publish({{100}, 10, {20}, {150}}).published == 1);
    REQUIRE(sink.calls == 1); REQUIRE(sink.reentered); REQUIRE(writer->pendingEventCount() == 0); REQUIRE(publisher.publish({{150}, 10, {20}, {200}}).published == 0);
}

TEST_CASE("Recoverable outbox publisher: retry, rejection, stale ACK, lease recovery, and clock rollback are durable", "[outbox_publisher]")
{
    Paths paths; Ids ids; auto writer = openSqliteCatalogOutbox(database(), paths, ids, {}); REQUIRE(writer->apply(change()).eventId);
    auto claim = writer->claim({"owner-a", {100}, {10}, 1}); REQUIRE(claim.status == ClaimStatus::Claimed); REQUIRE(claim.events.size() == 1);
    REQUIRE(writer->complete({claim.events[0].change.eventId, claim.events[0].token, PublishResult::RetryableFailure, {150}, "retry"}) == DeliveryStatus::Updated);
    REQUIRE(writer->recover({{149}, 10}).pending.size() == 0); REQUIRE(writer->recover({{150}, 10}).pending.size() == 1);
    auto again = writer->claim({"owner-b", {150}, {10}, 1}); REQUIRE(again.status == ClaimStatus::Claimed); REQUIRE(again.events[0].change.eventId.value() == claim.events[0].change.eventId.value());
    REQUIRE(writer->complete({again.events[0].change.eventId, claim.events[0].token, PublishResult::Accepted, {0}, {}}) == DeliveryStatus::StaleLease);
    REQUIRE(writer->recover({{149}, 10}).status == RecoveryStatus::ClockRollback);
    REQUIRE(writer->complete({again.events[0].change.eventId, again.events[0].token, PublishResult::Rejected, {0}, "bad"}) == DeliveryStatus::Updated);
    REQUIRE(writer->pendingEventCount() == 0);
    REQUIRE(writer->apply(change("duplicate.txt")).status == MutationStatus::Applied); auto duplicate=writer->claim({"owner-c", {150}, {10}, 1}); REQUIRE(writer->complete({duplicate.events[0].change.eventId, duplicate.events[0].token, PublishResult::Duplicate, {0}, {}}) == DeliveryStatus::Updated); REQUIRE(writer->pendingEventCount() == 0);
    REQUIRE(writer->apply(change("pre-ack-crash.txt")).status == MutationStatus::Applied); auto beforeAck=writer->claim({"owner-d", {150}, {10}, 1}); REQUIRE(writer->recover({{160}, 1}).reclaimed == 1); auto republished=writer->claim({"owner-e", {160}, {10}, 1}); REQUIRE(republished.events[0].change.eventId.value() == beforeAck.events[0].change.eventId.value()); REQUIRE(writer->complete({republished.events[0].change.eventId, republished.events[0].token, PublishResult::Accepted, {0}, {}}) == DeliveryStatus::Updated);
}

TEST_CASE("Recoverable outbox publisher: corrupt rows are quarantined without calling the sink", "[outbox_publisher]")
{
    Paths paths; Ids ids; const auto path = database(); auto writer = openSqliteCatalogOutbox(path, paths, ids, {}); REQUIRE(writer->apply(change()).status == MutationStatus::Applied); REQUIRE(writer->apply(change("enum.txt")).status == MutationStatus::Applied); REQUIRE(writer->apply(change("payload.txt")).status == MutationStatus::Applied);
    sqlite3* raw = nullptr; REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK); REQUIRE(sqlite3_exec(raw, "UPDATE event_outbox SET schema_version=99 WHERE event_id='e1'; UPDATE event_outbox SET kind='future' WHERE event_id='e2'; UPDATE event_outbox SET payload=CAST(X'C0' AS TEXT) WHERE event_id='e3'", nullptr, nullptr, nullptr) == SQLITE_OK); sqlite3_close(raw);
    Sink sink; OutboxPublisher publisher(*writer, sink, "publisher"); REQUIRE(publisher.publish({{100}, 10, {20}}).quarantined == 1); REQUIRE(sink.calls == 0); REQUIRE(writer->pendingEventCount() == 0);
}
