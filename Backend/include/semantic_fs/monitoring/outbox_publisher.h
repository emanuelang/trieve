#pragma once

#include "semantic_fs/monitoring/i_catalog_outbox_writer.h"
#include "semantic_fs/monitoring/i_file_change_sink.h"

namespace semantic_fs::monitoring {
struct PublishRequest { UtcTimestamp now; std::size_t limit; UtcTimestamp leaseDuration; UtcTimestamp retryAvailableAt; };
struct PublishSummary { std::size_t published = 0; std::size_t quarantined = 0; };
class OutboxPublisher {
public:
    OutboxPublisher(ICatalogOutboxWriter& writer, IFileChangeSink& sink, std::string owner) : writer_(writer), sink_(sink), owner_(std::move(owner)) {}
    PublishSummary publish(const PublishRequest&);
private:
    ICatalogOutboxWriter& writer_; IFileChangeSink& sink_; std::string owner_;
};
} // namespace semantic_fs::monitoring
