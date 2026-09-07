#include "semantic_fs/monitoring/i_clock.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using namespace semantic_fs::monitoring;

namespace {

class FakeClock final : public IClock {
public:
    UtcTimestamp utcNow() const override { return {1700000000}; }
    MonotonicTimestamp monotonicNow() const override { return {250000}; }
};

} // namespace

TEST_CASE("Phase 4 contracts keep monotonic scheduling distinct from UTC diagnostics", "[phase4.u1]")
{
    static_assert(!std::is_same_v<MonotonicTimestamp, UtcTimestamp>);

    FakeClock clock;
    REQUIRE(clock.monotonicNow().microsecondsSinceOrigin == 250000);
    REQUIRE(clock.utcNow().microsecondsSinceEpoch == 1700000000);
}
