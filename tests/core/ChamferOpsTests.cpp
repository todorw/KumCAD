#include "core/geometry/ChamferOps.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("computeChamferGeometry cuts back an equal distance along two perpendicular lines meeting at a corner",
         "[chamferops]") {
    // A right-angle corner at the origin: line1 along +X, line2 along +Y,
    // both starting AT the corner (so their own "far" endpoint is the one
    // away from it).
    LineEntity l1(1, 0, Point2D(0, 0), Point2D(10, 0));
    LineEntity l2(2, 0, Point2D(0, 0), Point2D(0, 10));

    const auto result = computeChamferGeometry(l1, l2, 3.0, 3.0);
    REQUIRE(result.has_value());
    REQUIRE(result->keepEnd1); // line1's start (0,0) is nearest the corner -- start gets trimmed, END(10,0) is kept
    REQUIRE(result->keepEnd2);
    REQUIRE(result->trim1.x == Approx(3.0).margin(1e-6));
    REQUIRE(result->trim1.y == Approx(0.0).margin(1e-6));
    REQUIRE(result->trim2.x == Approx(0.0).margin(1e-6));
    REQUIRE(result->trim2.y == Approx(3.0).margin(1e-6));
}

TEST_CASE("computeChamferGeometry supports two independent distances", "[chamferops]") {
    LineEntity l1(1, 0, Point2D(0, 0), Point2D(20, 0));
    LineEntity l2(2, 0, Point2D(0, 0), Point2D(0, 20));

    const auto result = computeChamferGeometry(l1, l2, 4.0, 7.0);
    REQUIRE(result.has_value());
    REQUIRE(result->trim1.x == Approx(4.0).margin(1e-6));
    REQUIRE(result->trim2.y == Approx(7.0).margin(1e-6));
}

TEST_CASE("computeChamferGeometry trims from whichever endpoint is actually nearest the corner", "[chamferops]") {
    // Same corner, but line1's endpoints are swapped (END is now at the
    // corner) -- the kept/far endpoint must flip to match.
    LineEntity l1(1, 0, Point2D(10, 0), Point2D(0, 0));
    LineEntity l2(2, 0, Point2D(0, 0), Point2D(0, 10));

    const auto result = computeChamferGeometry(l1, l2, 3.0, 3.0);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->keepEnd1); // line1's START (10,0) is now the far/kept one
    REQUIRE(result->trim1.x == Approx(3.0).margin(1e-6));
}

TEST_CASE("computeChamferGeometry rejects parallel/collinear lines", "[chamferops]") {
    LineEntity l1(1, 0, Point2D(0, 0), Point2D(10, 0));
    LineEntity parallel(2, 0, Point2D(0, 5), Point2D(10, 5));
    REQUIRE_FALSE(computeChamferGeometry(l1, parallel, 1.0, 1.0).has_value());

    LineEntity collinear(3, 0, Point2D(20, 0), Point2D(30, 0));
    REQUIRE_FALSE(computeChamferGeometry(l1, collinear, 1.0, 1.0).has_value());
}

TEST_CASE("computeChamferGeometry rejects a negative or too-large distance", "[chamferops]") {
    LineEntity l1(1, 0, Point2D(0, 0), Point2D(10, 0));
    LineEntity l2(2, 0, Point2D(0, 0), Point2D(0, 10));

    REQUIRE_FALSE(computeChamferGeometry(l1, l2, -1.0, 1.0).has_value());
    REQUIRE_FALSE(computeChamferGeometry(l1, l2, 1.0, -1.0).has_value());
    // Distance longer than the line itself, from the corner to the far end.
    REQUIRE_FALSE(computeChamferGeometry(l1, l2, 15.0, 1.0).has_value());
}

TEST_CASE("computeChamferGeometry at distance 0 trims both lines exactly to the corner", "[chamferops]") {
    LineEntity l1(1, 0, Point2D(0, 0), Point2D(10, 0));
    LineEntity l2(2, 0, Point2D(0, 0), Point2D(0, 10));

    const auto result = computeChamferGeometry(l1, l2, 0.0, 0.0);
    REQUIRE(result.has_value());
    REQUIRE(result->trim1.x == Approx(0.0).margin(1e-9));
    REQUIRE(result->trim1.y == Approx(0.0).margin(1e-9));
    REQUIRE(result->trim2.x == Approx(0.0).margin(1e-9));
    REQUIRE(result->trim2.y == Approx(0.0).margin(1e-9));
}
