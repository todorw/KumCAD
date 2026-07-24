#include "core/geometry/DonutOps.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("buildDonutLoops builds an outer CCW loop and an inner CW hole loop", "[donutops]") {
    const auto loops = buildDonutLoops(Point2D(0, 0), 3.0, 5.0, 32);
    REQUIRE(loops.size() == 2);
    REQUIRE(loops[0].vertices.size() == 32);
    REQUIRE(loops[1].vertices.size() == 32);

    REQUIRE_FALSE(loops[0].isHole()); // outer: positive (CCW) signed area
    REQUIRE(loops[1].isHole());       // inner: negative (CW) signed area, a real hole

    for (const Point2D& v : loops[0].vertices) REQUIRE(v.distanceTo(Point2D(0, 0)) == Approx(5.0).margin(1e-6));
    for (const Point2D& v : loops[1].vertices) REQUIRE(v.distanceTo(Point2D(0, 0)) == Approx(3.0).margin(1e-6));
}

TEST_CASE("buildDonutLoops with zero inside radius produces a solid disc, no hole loop", "[donutops]") {
    const auto loops = buildDonutLoops(Point2D(1, 1), 0.0, 4.0, 16);
    REQUIRE(loops.size() == 1);
    REQUIRE_FALSE(loops[0].isHole());
    for (const Point2D& v : loops[0].vertices) REQUIRE(v.distanceTo(Point2D(1, 1)) == Approx(4.0).margin(1e-6));
}

TEST_CASE("buildDonutLoops treats an inside radius >= outside radius as a solid disc", "[donutops]") {
    const auto loops = buildDonutLoops(Point2D(0, 0), 5.0, 5.0, 16);
    REQUIRE(loops.size() == 1);
}

TEST_CASE("buildDonutLoops returns empty for a non-positive outside radius", "[donutops]") {
    REQUIRE(buildDonutLoops(Point2D(0, 0), 1.0, 0.0, 16).empty());
    REQUIRE(buildDonutLoops(Point2D(0, 0), 1.0, -3.0, 16).empty());
}
