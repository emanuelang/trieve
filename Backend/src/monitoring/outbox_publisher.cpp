#include "semantic_fs/monitoring/outbox_publisher.h"

#include <algorithm>

namespace semantic_fs::monitoring {
namespace {
constexpr std::size_t kMaximumRetainedTerminalEvents = 16;
constexpr std::size_t kMaximumDiagnosticBytes = 128;

bool exceeds(std::size_t value, std::size_t limit)
{
    return limit != 0 && value >= limit;
}

bool belowResumeThreshold(std::size_t value, std::size_t limit)
{
    return limit == 0 || value * 4 < limit * 3;
}

std::string boundedDiagnostic(std::string value)
{
    value.resize(std::min(value.size(), kMaximumDiagnosticBytes));
    return value;
}
} // namespace

PublishSummary OutboxPublisher::publish(const PublishRequest& request)
{
    PublishSummary summary;
    const auto claim = writer_.claim({owner_, request.now, request.leaseDuration, request.limit});
    if (claim.status == ClaimStatus::Corrupt) ++summary.quarantined;
    if (claim.status == ClaimStatus::StorageFailure) recordStorageFailure();

    for (const auto& event : claim.events) {
        const auto outcome = sink_.publish(event.change);
        const auto diagnostic = outcome == PublishResult::Rejected ? "sink rejected event" : outcome == PublishResult::RetryableFailure ? "delivery retry scheduled" : "";
        const auto completion = writer_.complete({event.change.eventId, event.token, outcome, request.retryAvailableAt, diagnostic});
        if (completion == DeliveryStatus::Updated) recordDurableOutcome(event, outcome, request.now);
        if (completion == DeliveryStatus::StorageFailure) recordStorageFailure();
        ++summary.published;
    }
    return summary;
}

PublisherStatus OutboxPublisher::status() const
{
    PublisherStatus result{accepted_, retryable_, terminal_, {}, maximumLatencyMicroseconds_, lastError_, interventionRequired_, terminalEvents_};
    const auto retry = writer_.earliestPendingRetry();
    if (retry.status == RetryQueryStatus::Found) result.nextRetry = retry.availableAt;
    if (retry.status == RetryQueryStatus::StorageFailure) {
        result.lastError = PublisherDiagnostic::StorageFailure;
        result.interventionRequired = true;
    }
    return result;
}

void OutboxPublisher::recordDurableOutcome(const ClaimedEvent& event, PublishResult outcome, UtcTimestamp now)
{
    maximumLatencyMicroseconds_ = std::max(maximumLatencyMicroseconds_, std::max<std::int64_t>(0, now.microsecondsSinceEpoch - event.change.observedAt.microsecondsSinceEpoch));
    switch (outcome) {
    case PublishResult::Accepted:
    case PublishResult::Duplicate:
        ++accepted_;
        break;
    case PublishResult::RetryableFailure:
        ++retryable_;
        break;
    case PublishResult::Rejected:
        ++terminal_;
        lastError_ = PublisherDiagnostic::SinkRejected;
        interventionRequired_ = true;
        if (terminalEvents_.size() == kMaximumRetainedTerminalEvents) terminalEvents_.erase(terminalEvents_.begin());
        terminalEvents_.push_back({event.change, boundedDiagnostic("sink rejected event")});
        break;
    }
}

void OutboxPublisher::recordStorageFailure()
{
    lastError_ = PublisherDiagnostic::StorageFailure;
    interventionRequired_ = true;
}

OutboxQuotaStatus OutboxQuotaController::update(OutboxQuotaMetrics metrics)
{
    const bool hardLimited = exceeds(metrics.pendingRows, limits_.hardPendingRows) || exceeds(metrics.payloadBytes, limits_.hardPayloadBytes);
    const bool softReached = exceeds(metrics.pendingRows, limits_.softPendingRows) || exceeds(metrics.payloadBytes, limits_.softPayloadBytes);
    if (softReached) softLimited_ = true;
    if (softLimited_ && belowResumeThreshold(metrics.pendingRows, limits_.softPendingRows) && belowResumeThreshold(metrics.payloadBytes, limits_.softPayloadBytes)) softLimited_ = false;
    return {softLimited_, hardLimited, hardLimited, !softLimited_ && !hardLimited, softLimited_ || hardLimited};
}

bool monitoringHealthy(const MonitoringHealthInput& input)
{
    return !input.starting && !input.converging && !input.dirty && !input.saturated && !input.stopped && !input.cancelled && !input.interventionRequired;
}
} // namespace semantic_fs::monitoring
