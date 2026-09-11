#pragma once

#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_file_change_sink.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace semantic_fs::monitoring {
struct PublishRequest { UtcTimestamp now; std::size_t limit; UtcTimestamp leaseDuration; UtcTimestamp retryAvailableAt; };
struct PublishSummary { std::size_t published = 0; std::size_t quarantined = 0; };

enum class PublisherDiagnostic { None, SinkRejected, StorageFailure };
struct TerminalEvent { FileChange payload; std::string diagnostic; };
struct PublisherStatus {
    std::size_t accepted = 0;
    std::size_t retryable = 0;
    std::size_t terminal = 0;
    std::optional<UtcTimestamp> nextRetry;
    std::int64_t maximumLatencyMicroseconds = 0;
    PublisherDiagnostic lastError = PublisherDiagnostic::None;
    bool interventionRequired = false;
    std::vector<TerminalEvent> terminalEvents;
};

struct OutboxQuotaLimits { std::size_t softPendingRows = 0; std::size_t softPayloadBytes = 0; std::size_t hardPendingRows = 0; std::size_t hardPayloadBytes = 0; };
struct OutboxQuotaMetrics { std::size_t pendingRows = 0; std::size_t payloadBytes = 0; };
struct OutboxQuotaStatus { bool softLimited = false; bool hardLimited = false; bool saturated = false; bool reconciliationAdmissionAllowed = true; bool publicationPrioritized = false; };
class OutboxQuotaController {
public:
    explicit OutboxQuotaController(OutboxQuotaLimits limits) : limits_(limits) {}
    OutboxQuotaStatus update(OutboxQuotaMetrics metrics);
private:
    OutboxQuotaLimits limits_;
    bool softLimited_ = false;
};

struct MonitoringHealthInput { bool starting = false; bool converging = false; bool dirty = false; bool saturated = false; bool stopped = false; bool cancelled = false; bool interventionRequired = false; };
bool monitoringHealthy(const MonitoringHealthInput& input);

class OutboxPublisher {
public:
    OutboxPublisher(ICatalogOutboxWriter& writer, IFileChangeSink& sink, std::string owner) : writer_(writer), sink_(sink), owner_(std::move(owner)) {}
    PublishSummary publish(const PublishRequest&);
    PublisherStatus status() const;
private:
    void recordDurableOutcome(const ClaimedEvent&, PublishResult, UtcTimestamp);
    void recordStorageFailure();

    ICatalogOutboxWriter& writer_;
    IFileChangeSink& sink_;
    std::string owner_;
    std::size_t accepted_ = 0;
    std::size_t retryable_ = 0;
    std::size_t terminal_ = 0;
    std::int64_t maximumLatencyMicroseconds_ = 0;
    PublisherDiagnostic lastError_ = PublisherDiagnostic::None;
    bool interventionRequired_ = false;
    std::vector<TerminalEvent> terminalEvents_;
};
} // namespace semantic_fs::monitoring
