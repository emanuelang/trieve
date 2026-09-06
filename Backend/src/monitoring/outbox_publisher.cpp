#include "semantic_fs/monitoring/outbox_publisher.h"

namespace semantic_fs::monitoring {
PublishSummary OutboxPublisher::publish(const PublishRequest& request)
{
    PublishSummary summary;
    const auto claim = writer_.claim({owner_, request.now, request.leaseDuration, request.limit});
    if (claim.status == ClaimStatus::Corrupt) ++summary.quarantined;
    for (const auto& event : claim.events) {
        const auto outcome = sink_.publish(event.change);
        writer_.complete({event.change.eventId, event.token, outcome, request.retryAvailableAt, outcome == PublishResult::Rejected ? "sink rejected event" : ""});
        ++summary.published;
    }
    return summary;
}
} // namespace semantic_fs::monitoring
