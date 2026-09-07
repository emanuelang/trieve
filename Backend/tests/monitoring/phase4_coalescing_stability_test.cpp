#include "semantic_fs/monitoring/event_coalescer.h"
#include "semantic_fs/monitoring/file_stability_probe.h"

#include <catch2/catch_test_macros.hpp>

using namespace semantic_fs::monitoring;

namespace {
RootId root(std::string value = "root") { return *RootId::create(value); }
NormalizedPath path(std::string value) { return {value, {value, {value}}}; }
FileMetadata metadata(std::uint64_t size) { return {{size}, UtcTimestamp{1}, "id"}; }
}

TEST_CASE("Phase 4 coalescer preserves order and marks dirty bounded ambiguity", "[phase4.u2]")
{
    EventCoalescer coalescer;
    REQUIRE(coalescer.accept({root(), path("a"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(coalescer.accept({root(), path("b"), MonotonicTimestamp{100000}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(coalescer.step(root(), MonotonicTimestamp{249999}).status == CoalescingStatus::Waiting);
    const auto ready = coalescer.step(root(), MonotonicTimestamp{250000});
    REQUIRE(ready.status == CoalescingStatus::Ready);
    REQUIRE(ready.paths.size() == 2);
    REQUIRE(ready.paths[0].displayUtf8 == "a");
    REQUIRE(ready.paths[1].displayUtf8 == "b");

    EventCoalescer barrier;
    REQUIRE(barrier.accept({root(), path("a"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    const auto closed = barrier.barrier(root(), GapEpoch{1});
    REQUIRE(closed.status == CoalescingStatus::Ready);
    REQUIRE(closed.paths.size() == 1);
    REQUIRE(closed.paths[0].displayUtf8 == "a");
    REQUIRE(barrier.accept({root(), path("b"), MonotonicTimestamp{1}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    const auto afterBarrier = barrier.step(root(), MonotonicTimestamp{250001});
    REQUIRE(afterBarrier.paths.size() == 1);
    REQUIRE(afterBarrier.paths[0].displayUtf8 == "b");

    EventCoalescer uncertain;
    REQUIRE(uncertain.accept({root(), path("a"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(uncertain.uncertain(root(), GapEpoch{1}).status == CoalescingStatus::Dirty);
    REQUIRE(uncertain.accept({root(), path("b"), MonotonicTimestamp{1}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(uncertain.step(root(), MonotonicTimestamp{250001}).paths[0].displayUtf8 == "b");

    EventCoalescer roots(1);
    REQUIRE(roots.accept({root("one"), path("a"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(roots.accept({root("two"), path("b"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Dirty);
    EventCoalescer epoch;
    REQUIRE(epoch.accept({root(), path("c"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(epoch.accept({root(), path("d"), MonotonicTimestamp{1}, GapEpoch{2}}).status == CoalescingStatus::Dirty);

    EventCoalescer capped;
    for (std::size_t count = 0; count < 1024; ++count)
        REQUIRE(capped.accept({root(), path(std::to_string(count)), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Accepted);
    REQUIRE(capped.accept({root(), path("overflow"), MonotonicTimestamp{0}, GapEpoch{1}}).status == CoalescingStatus::Dirty);
}

TEST_CASE("Phase 4 stability requires two equal samples across the interval", "[phase4.u2]")
{
    FileStabilityProbe probe;
    REQUIRE(probe.step({StabilitySampleKind::Metadata, metadata(4), MonotonicTimestamp{0}}).status == StabilityStatus::Retry);
    REQUIRE(probe.step({StabilitySampleKind::Metadata, metadata(4), MonotonicTimestamp{249999}}).status == StabilityStatus::Retry);
    REQUIRE(probe.step({StabilitySampleKind::Metadata, metadata(4), MonotonicTimestamp{250000}}).status == StabilityStatus::Stable);

    FileStabilityProbe missing;
    REQUIRE(missing.step({StabilitySampleKind::NotFound, std::nullopt, MonotonicTimestamp{0}}).status == StabilityStatus::Reconcile);

    FileStabilityProbe cancelled;
    REQUIRE(cancelled.step({StabilitySampleKind::Cancelled, std::nullopt, MonotonicTimestamp{0}}).status == StabilityStatus::Cancelled);

    FileStabilityProbe ambiguous;
    REQUIRE(ambiguous.step({StabilitySampleKind::AccessError, std::nullopt, MonotonicTimestamp{0}}).status == StabilityStatus::Reconcile);
    REQUIRE(ambiguous.step({StabilitySampleKind::TempSaveRenameAmbiguity, std::nullopt, MonotonicTimestamp{1}}).status == StabilityStatus::Reconcile);

    FileStabilityProbe writing;
    for (std::uint64_t attempt = 0; attempt < 19; ++attempt)
        REQUIRE(writing.step({StabilitySampleKind::Metadata, metadata(attempt), MonotonicTimestamp{static_cast<std::int64_t>(attempt)}}).status == StabilityStatus::Retry);
    REQUIRE(writing.step({StabilitySampleKind::Metadata, metadata(20), MonotonicTimestamp{20}}).status == StabilityStatus::Reconcile);

    FileStabilityProbe deadline;
    REQUIRE(deadline.step({StabilitySampleKind::Metadata, metadata(1), MonotonicTimestamp{0}}).status == StabilityStatus::Retry);
    REQUIRE(deadline.step({StabilitySampleKind::Metadata, metadata(2), MonotonicTimestamp{4999999}}).status == StabilityStatus::Retry);
    REQUIRE(deadline.step({StabilitySampleKind::Metadata, metadata(3), MonotonicTimestamp{5000000}}).status == StabilityStatus::Reconcile);
}
