#include "semantic_fs/monitoring/outbox_publisher.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace semantic_fs::monitoring;

namespace {

NormalizedPath path(std::string value)
{
    return {value, {value}};
}

FileChange change(std::string event, std::string observation, std::string payload, UtcTimestamp observedAt)
{
    return {kFileChangeSchemaVersion, *EventId::create(event), *RootId::create("root"), *ObservationId::create(observation), {1}, ChangeKind::Created, path(std::move(payload)), {}, {}, observedAt, ObservationSource::Watcher};
}

class Writer final : public ICatalogOutboxWriter {
public:
    ClaimResult claim(const ClaimRequest&) override
    {
        if (claims.empty()) return {ClaimStatus::Claimed, events};
        return claims.at(claimIndex++);
    }
    DeliveryStatus complete(const DeliveryCommand& command) override
    {
        deliveries.push_back(command);
        const auto status = completionStatuses.empty() ? DeliveryStatus::Updated : completionStatuses.at(completionIndex++);
        if (status != DeliveryStatus::Updated) return status;
        const auto retry = std::find_if(retries.begin(), retries.end(), [&](const auto& entry) { return entry.first == command.eventId.value(); });
        if (command.outcome == PublishResult::RetryableFailure) {
            if (retry == retries.end()) retries.emplace_back(command.eventId.value(), command.availableAt);
            else retry->second = command.availableAt;
        } else if (retry != retries.end()) retries.erase(retry);
        return status;
    }
    MutationResult apply(const TransitionCommand&) override { return {}; }
    CoverageResult recordCoverage(const CoverageCommand&) override { return {}; }
    RecoveryResult recover(const RecoveryRequest&) override { return {}; }
    RetryQueryResult earliestPendingRetry() const override
    {
        if (retryQueryStatus == RetryQueryStatus::StorageFailure) return {RetryQueryStatus::StorageFailure, {}};
        if (retries.empty()) return {RetryQueryStatus::Empty, {}};
        return {RetryQueryStatus::Found, std::min_element(retries.begin(), retries.end(), [](const auto& left, const auto& right) {
            return left.second.microsecondsSinceEpoch < right.second.microsecondsSinceEpoch;
        })->second};
    }
    std::size_t pendingEventCount() const override { return pending; }
    UtcTimestamp lastSeenUtc() const override { return {}; }
    std::uint32_t dirtyReasonCount(const RootId&) const override { return 0; }
    std::size_t deferredEvidenceCount() const override { return 0; }
    std::optional<Generation> generationFor(const RootId&, const NormalizedPath&) const override { return {}; }

    std::vector<ClaimedEvent> events;
    std::vector<ClaimResult> claims;
    std::vector<DeliveryCommand> deliveries;
    std::vector<DeliveryStatus> completionStatuses;
    std::vector<std::pair<std::string, UtcTimestamp>> retries;
    std::size_t pending = 3;
    RetryQueryStatus retryQueryStatus = RetryQueryStatus::Empty;
    std::size_t claimIndex = 0;
    std::size_t completionIndex = 0;
};

class Sink final : public IFileChangeSink {
public:
    PublishResult publish(const FileChange&) override { return outcomes.at(index++); }
    std::vector<PublishResult> outcomes{PublishResult::Accepted, PublishResult::RetryableFailure, PublishResult::Rejected};
    std::size_t index = 0;
};

class SqlPaths final : public IPathSemantics {
public:
    std::optional<AbsolutePath> normalizeAbsolute(std::string_view value) const override { return AbsolutePath{std::string(value)}; }
    int compareComponent(std::string_view left, std::string_view right) const override { return left == right ? 0 : left < right ? -1 : 1; }
    std::optional<RelativePath> relativeTo(const AbsolutePath&, const AbsolutePath&) const override { return std::nullopt; }
};

class SqlIds final : public IIdSource {
public:
    std::string nextObservationId() override { return "observation-" + std::to_string(++observation); }
    std::string nextEventId() override { return "event-" + std::to_string(++event); }
private:
    int observation = 0;
    int event = 0;
};

std::filesystem::path retryDatabase()
{
    const auto file = std::filesystem::temp_directory_path() / "semantic-fs-u6-retry-status.db";
    std::error_code error;
    std::filesystem::remove(file, error);
    return file;
}

TransitionCommand durableChange(int index)
{
    const auto name = "retry-" + std::to_string(index) + ".txt";
    return {*RootId::create("root"), {name, {name, {name}}}, {1, {}, {}}, {10}, ChangeKind::Created, ObservationSource::Watcher, {}};
}

} // namespace

TEST_CASE("Phase 4 publisher status retains bounded outcomes, timing, and terminal identity", "[phase4.u6b]")
{
    Writer writer;
    writer.events = {
        {change("event-1", "observation-1", "ok.txt", {90}), "lease-1", {120}},
        {change("event-2", "observation-2", "retry.txt", {80}), "lease-2", {120}},
        {change("event-3", "observation-3", "terminal.txt", {70}), "lease-3", {120}},
    };
    Sink sink;
    OutboxPublisher publisher(writer, sink, "publisher");
    REQUIRE(publisher.publish({{100}, 3, {20}, {150}}).published == 3);
    const auto status = publisher.status();
    REQUIRE(status.accepted == 1);
    REQUIRE(status.retryable == 1);
    REQUIRE(status.terminal == 1);
    REQUIRE(status.nextRetry.has_value());
    REQUIRE(status.nextRetry->microsecondsSinceEpoch == 150);
    REQUIRE(status.maximumLatencyMicroseconds == 30);
    REQUIRE(status.lastError == PublisherDiagnostic::SinkRejected);
    REQUIRE(status.interventionRequired);
    REQUIRE(status.terminalEvents.size() == 1);
    REQUIRE(status.terminalEvents.front().payload.eventId.value() == "event-3");
    REQUIRE(status.terminalEvents.front().payload.observationId.value() == "observation-3");
    REQUIRE(status.terminalEvents.front().payload.path.displayUtf8 == "terminal.txt");
    REQUIRE(status.terminalEvents.front().diagnostic == "sink rejected event");
    REQUIRE(writer.deliveries[1].availableAt.microsecondsSinceEpoch == 150);
    REQUIRE(writer.deliveries[1].diagnostic == "delivery retry scheduled");
}

TEST_CASE("Phase 4 quota pauses publication admission until every soft limit drops below seventy-five percent", "[phase4.u6b]")
{
    OutboxQuotaController quotas({10, 100, 20, 200});
    auto status = quotas.update({10, 100});
    REQUIRE(status.softLimited);
    REQUIRE_FALSE(status.reconciliationAdmissionAllowed);
    REQUIRE(status.publicationPrioritized);
    status = quotas.update({8, 74});
    REQUIRE(status.softLimited);
    REQUIRE_FALSE(status.reconciliationAdmissionAllowed);
    status = quotas.update({7, 74});
    REQUIRE_FALSE(status.softLimited);
    REQUIRE(status.reconciliationAdmissionAllowed);
    REQUIRE_FALSE(status.publicationPrioritized);
    status = quotas.update({20, 1});
    REQUIRE(status.hardLimited);
    REQUIRE(status.saturated);
    REQUIRE_FALSE(status.reconciliationAdmissionAllowed);
}

TEST_CASE("Phase 4 publisher reports only durably completed outcomes and retains no failed terminal payload", "[phase4.u6b]")
{
    Writer writer;
    writer.events = {
        {change("stale", "observation-stale", "stale.txt", {90}), "lease-stale", {120}},
        {change("invalid", "observation-invalid", "invalid.txt", {90}), "lease-invalid", {120}},
        {change("storage", "observation-storage", "storage.txt", {90}), "lease-storage", {120}},
    };
    writer.completionStatuses = {DeliveryStatus::StaleLease, DeliveryStatus::Invalid, DeliveryStatus::StorageFailure};
    Sink sink;
    OutboxPublisher publisher(writer, sink, "publisher");
    REQUIRE(publisher.publish({{100}, 3, {20}, {150}}).published == 3);
    const auto status = publisher.status();
    REQUIRE(status.accepted == 0);
    REQUIRE(status.retryable == 0);
    REQUIRE(status.terminal == 0);
    REQUIRE_FALSE(status.nextRetry.has_value());
    REQUIRE(status.terminalEvents.empty());
    REQUIRE(status.lastError == PublisherDiagnostic::StorageFailure);
    REQUIRE(status.interventionRequired);
}

TEST_CASE("Phase 4 publisher bounds terminal retention and surfaces authoritative retry-query failures", "[phase4.u6b]")
{
    Writer writer;
    Sink sink;
    sink.outcomes.assign(17, PublishResult::Rejected);
    for (int index = 0; index != 17; ++index) {
        writer.events.push_back({change("event-" + std::to_string(index), "observation-" + std::to_string(index), "terminal-" + std::to_string(index), {90}), "lease-" + std::to_string(index), {120}});
    }
    OutboxPublisher publisher(writer, sink, "publisher");
    REQUIRE(publisher.publish({{100}, 17, {20}, {150}}).published == 17);
    const auto retained = publisher.status();
    REQUIRE(retained.terminal == 17);
    REQUIRE(retained.terminalEvents.size() == 16);
    REQUIRE(retained.terminalEvents.front().payload.eventId.value() == "event-1");
    REQUIRE(retained.terminalEvents.back().payload.observationId.value() == "observation-16");
    writer.retryQueryStatus = RetryQueryStatus::StorageFailure;
    const auto failure = publisher.status();
    REQUIRE_FALSE(failure.nextRetry.has_value());
    REQUIRE(failure.lastError == PublisherDiagnostic::StorageFailure);
    REQUIRE(failure.interventionRequired);
}

TEST_CASE("Phase 4 publisher exposes the earliest current durable retry and clears resolved retries", "[phase4.u6b]")
{
    Writer writer;
    writer.claims = {
        {ClaimStatus::Claimed, {{change("retry-late", "observation-late", "late.txt", {90}), "lease-late", {120}}}},
        {ClaimStatus::Claimed, {{change("retry-early", "observation-early", "early.txt", {90}), "lease-early", {120}}}},
        {ClaimStatus::Claimed, {{change("retry-late", "observation-late", "late.txt", {90}), "lease-late-2", {120}}}},
        {ClaimStatus::Claimed, {{change("retry-early", "observation-early", "early.txt", {90}), "lease-early-2", {120}}}},
    };
    writer.completionStatuses = {DeliveryStatus::Updated, DeliveryStatus::Updated, DeliveryStatus::Updated, DeliveryStatus::Updated};
    Sink sink;
    sink.outcomes = {PublishResult::RetryableFailure, PublishResult::RetryableFailure, PublishResult::Accepted, PublishResult::Rejected};
    OutboxPublisher publisher(writer, sink, "publisher");
    publisher.publish({{100}, 1, {20}, {300}});
    publisher.publish({{110}, 1, {20}, {200}});
    REQUIRE(publisher.status().nextRetry->microsecondsSinceEpoch == 200);
    publisher.publish({{120}, 1, {20}, {400}});
    REQUIRE(publisher.status().nextRetry->microsecondsSinceEpoch == 200);
    publisher.publish({{130}, 1, {20}, {500}});
    REQUIRE_FALSE(publisher.status().nextRetry.has_value());
}

TEST_CASE("Phase 4 publisher reads all durable retries across reopen instead of forgetting the seventeenth", "[phase4.u6b]")
{
    SqlPaths paths;
    SqlIds ids;
    const auto database = retryDatabase();
    auto writer = openSqliteCatalogOutbox(database, paths, ids, {});
    for (int index = 0; index != 17; ++index) REQUIRE(writer->apply(durableChange(index)).eventId.has_value());
    const auto claimed = writer->claim({"retry-owner", {100}, {20}, 17});
    REQUIRE(claimed.events.size() == 17);
    for (int index = 0; index != 17; ++index) REQUIRE(writer->complete({claimed.events[index].change.eventId, claimed.events[index].token, PublishResult::RetryableFailure, {1000 + index}, "delivery retry scheduled"}) == DeliveryStatus::Updated);
    Sink sink;
    OutboxPublisher publisher(*writer, sink, "publisher");
    REQUIRE(publisher.status().nextRetry.has_value());
    REQUIRE(publisher.status().nextRetry->microsecondsSinceEpoch == 1000);
    writer.reset();
    SqlIds reopenedIds;
    auto reopened = openSqliteCatalogOutbox(database, paths, reopenedIds, {});
    OutboxPublisher reopenedPublisher(*reopened, sink, "publisher");
    REQUIRE(reopenedPublisher.status().nextRetry.has_value());
    REQUIRE(reopenedPublisher.status().nextRetry->microsecondsSinceEpoch == 1000);
    const auto firstSixteen = reopened->claim({"resolve-owner", {2000}, {20}, 16});
    REQUIRE(firstSixteen.events.size() == 16);
    for (const auto& event : firstSixteen.events) REQUIRE(reopened->complete({event.change.eventId, event.token, PublishResult::Accepted, {}, {}}) == DeliveryStatus::Updated);
    REQUIRE(reopenedPublisher.status().nextRetry.has_value());
    REQUIRE(reopenedPublisher.status().nextRetry->microsecondsSinceEpoch == 1016);
    const auto last = reopened->claim({"resolve-owner", {2000}, {20}, 1});
    REQUIRE(last.events.size() == 1);
    REQUIRE(reopened->complete({last.events.front().change.eventId, last.events.front().token, PublishResult::Accepted, {}, {}}) == DeliveryStatus::Updated);
    REQUIRE_FALSE(reopenedPublisher.status().nextRetry.has_value());
}

TEST_CASE("Phase 4 health is false for every operationally unsafe state", "[phase4.u6b]")
{
    REQUIRE(monitoringHealthy({}));
    REQUIRE_FALSE(monitoringHealthy({.starting = true}));
    REQUIRE_FALSE(monitoringHealthy({.converging = true}));
    REQUIRE_FALSE(monitoringHealthy({.dirty = true}));
    REQUIRE_FALSE(monitoringHealthy({.saturated = true}));
    REQUIRE_FALSE(monitoringHealthy({.stopped = true}));
    REQUIRE_FALSE(monitoringHealthy({.cancelled = true}));
    REQUIRE_FALSE(monitoringHealthy({.interventionRequired = true}));
}
