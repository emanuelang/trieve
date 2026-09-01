#include "filesystem_view_fixture.h"
#include "semantic_fs/monitoring/initial_scanner.h"
#include <catch2/catch_test_macros.hpp>

using namespace semantic_fs::monitoring;
using namespace semantic_fs::monitoring::test;

namespace {
WatchRootConfig root() { return {*RootId::create("root"), {"/root"}, {}}; }
FixtureView populatedView() {
    FixtureView view;
    view.listings["/root"] = std::vector<FileSystemEntry>{{{"/root/z.txt"}, FileSystemEntryKind::RegularFile}, {{{"/root/a"}}, FileSystemEntryKind::Directory}, {{{"/root/link"}}, FileSystemEntryKind::LinkOrReparse}, {{{"/root/denied"}}, FileSystemEntryKind::RegularFile}, {{{"/root/gone"}}, FileSystemEntryKind::RegularFile}};
    view.listings["/root/a"] = std::vector<FileSystemEntry>{{{"/root/a/child.txt"}, FileSystemEntryKind::RegularFile}};
    view.metadataResults["/root/z.txt"] = FileMetadata{1, {}, {}};
    view.metadataResults["/root/a/child.txt"] = FileMetadata{2, {}, {}};
    view.metadataResults["/root/denied"] = FsError{FsErrorCode::AccessDenied};
    view.metadataResults["/root/gone"] = FsError{FsErrorCode::NotFound};
    return view;
}
}

TEST_CASE("monitoring: scanner discovers nested files deterministically and keeps bounded relative errors")
{
    FixturePaths paths; FixtureView view = populatedView(); FixtureClock clock; FixtureSink sink; ForbiddenChangeSink changes;
    InitialScanner scanner(view, paths, clock, sink);
    const auto result = scanner.scan(root(), {.diagnosticCapacity = 1});
    REQUIRE(result.status == ScanStatus::Completed);
    REQUIRE(result.observationCount == 2);
    REQUIRE(result.errorCount == 2);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].relativePath.utf8 == "denied");
    REQUIRE(sink.observations[0].path.relative.utf8 == "a/child.txt");
    REQUIRE(sink.observations[1].path.relative.utf8 == "z.txt");
    REQUIRE(sink.observations[0].kind == ObservationKind::Discovered);
    REQUIRE(sink.observations[0].source == ObservationSource::InitialScan);
    REQUIRE(sink.observations[0].observedAt.microsecondsSinceEpoch == 42);
    REQUIRE(changes.calls == 0);
}

TEST_CASE("monitoring: scanner stops at checkpoints, unavailable roots, and non-accepted sinks")
{
    FixturePaths paths; FixtureClock clock; FixtureSink sink;
    FixtureView missing; missing.listings["/root"] = FsError{FsErrorCode::NotFound};
    REQUIRE(InitialScanner(missing, paths, clock, sink).scan(root()).status == ScanStatus::RootUnavailable);
    FixtureView view = populatedView();
    FixtureSink cancellationSink;
    const auto cancelled = InitialScanner(view, paths, clock, cancellationSink).scan(root(), {.checkpointHook = [](ScanCheckpoint point, const RelativePath& path) { return point != ScanCheckpoint::BeforeMetadata || path.utf8 != "z.txt"; }});
    REQUIRE(cancelled.status == ScanStatus::Cancelled);
    REQUIRE(cancellationSink.observations.size() == 1);
    REQUIRE(cancellationSink.observations[0].path.relative.utf8 == "a/child.txt");
    const auto descentCancelled = InitialScanner(populatedView(), paths, clock, sink).scan(root(), {.checkpointHook = [](ScanCheckpoint point, const RelativePath& path) { return point != ScanCheckpoint::BeforeDescent || path.utf8 != "a"; }});
    REQUIRE(descentCancelled.status == ScanStatus::Cancelled);
    const auto deliveryCancelled = InitialScanner(populatedView(), paths, clock, sink).scan(root(), {.checkpointHook = [](ScanCheckpoint point, const RelativePath&) { return point != ScanCheckpoint::BeforeDelivery; }});
    REQUIRE(deliveryCancelled.status == ScanStatus::Cancelled);
    FixtureSink blocked; blocked.next = ObservationDelivery::Backpressured;
    const auto stopped = InitialScanner(populatedView(), paths, clock, blocked).scan(root());
    REQUIRE(stopped.status == ScanStatus::SinkStopped);
    REQUIRE(stopped.sinkOutcome == ObservationDelivery::Backpressured);
    FixtureSink rejected; rejected.next = ObservationDelivery::Rejected;
    REQUIRE(InitialScanner(populatedView(), paths, clock, rejected).scan(root()).status == ScanStatus::SinkStopped);
}

TEST_CASE("monitoring: scanner records entry errors before a later sink rejection")
{
    FixturePaths paths; FixtureClock clock; FixtureSink sink;
    sink.next = ObservationDelivery::Rejected;
    sink.stopAfterAccepted = 1;
    const auto result = InitialScanner(populatedView(), paths, clock, sink).scan(root(), {.diagnosticCapacity = 2});
    REQUIRE(result.status == ScanStatus::SinkStopped);
    REQUIRE(result.sinkOutcome == ObservationDelivery::Rejected);
    REQUIRE(result.observationCount == 1);
    REQUIRE(result.errorCount == 2);
    REQUIRE(result.diagnostics.size() == 2);
    REQUIRE(sink.observations.size() == 2);
    REQUIRE(sink.observations[0].path.relative.utf8 == "a/child.txt");
    REQUIRE(sink.observations[1].path.relative.utf8 == "z.txt");
}
