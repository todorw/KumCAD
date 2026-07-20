#include "core/pcb/DiffPair.h"
#include "core/pcb/LengthTuning.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace lcad;
using Catch::Approx;

TEST_CASE("routeDiffPair builds two parallel tracks at the requested gap", "[diffpair]") {
    // A straight centerline along X; P pads sit above it, N pads below --
    // matching physical spacing so no length-matching meander is needed.
    const std::vector<Point2D> centerline = {Point2D(0, 0), Point2D(50, 0)};
    const Point2D pStart(0, 0.5), pEnd(50, 0.5);
    const Point2D nStart(0, -0.5), nEnd(50, -0.5);

    const DiffPairResult result = routeDiffPair(centerline, /*gap=*/1.0, pStart, pEnd, nStart, nEnd);
    REQUIRE(result.ok);

    // Both legs start/end exactly at their own real pad positions.
    REQUIRE(result.pPath.front().x == Approx(0));
    REQUIRE(result.pPath.front().y == Approx(0.5));
    REQUIRE(result.pPath.back().y == Approx(0.5));
    REQUIRE(result.nPath.front().y == Approx(-0.5));
    REQUIRE(result.nPath.back().y == Approx(-0.5));

    // With matching pad spacing, no stub or meander was needed: each leg is
    // a plain straight run of length 50, and no length mismatch to fix.
    REQUIRE(result.pLength == Approx(50.0));
    REQUIRE(result.nLength == Approx(50.0));
    REQUIRE(result.lengthMatched);

    // The two tracks stay parallel at the requested gap along their run
    // (checked at the midpoint, away from any stub geometry).
    REQUIRE(result.pPath.size() == 2);
    REQUIRE(result.nPath.size() == 2);
    REQUIRE(std::abs(result.pPath[0].y - result.nPath[0].y) == Approx(1.0));
}

TEST_CASE("routeDiffPair assigns P/N sides correctly regardless of which physical side P pads are on", "[diffpair]") {
    const std::vector<Point2D> centerline = {Point2D(0, 0), Point2D(20, 0)};

    // P pads BELOW the centerline this time (opposite of the previous test).
    const DiffPairResult result =
        routeDiffPair(centerline, 1.0, Point2D(0, -0.5), Point2D(20, -0.5), Point2D(0, 0.5), Point2D(20, 0.5));
    REQUIRE(result.ok);
    REQUIRE(result.pPath.front().y == Approx(-0.5));
    REQUIRE(result.nPath.front().y == Approx(0.5));
}

TEST_CASE("routeDiffPair adds stub segments when pads don't land on the offset endpoints", "[diffpair]") {
    const std::vector<Point2D> centerline = {Point2D(0, 0), Point2D(30, 0)};
    // Pads set back from the centerline's own endpoints by 5 units in X,
    // and offset in Y -- the router must add a stub to reach them.
    const Point2D pStart(-5, 0.5), pEnd(35, 0.5), nStart(-5, -0.5), nEnd(35, -0.5);

    const DiffPairResult result = routeDiffPair(centerline, 1.0, pStart, pEnd, nStart, nEnd);
    REQUIRE(result.ok);
    REQUIRE(result.pPath.size() > 2); // stub vertices were inserted
    REQUIRE(result.pPath.front().x == Approx(-5));
    REQUIRE(result.pPath.back().x == Approx(35));
    REQUIRE(pathLength(result.pPath) == Approx(result.pLength).margin(1e-9));
}

TEST_CASE("routeDiffPair auto-matches leg lengths when pad spacing differs", "[diffpair]") {
    // N pads set back further than P's, so N's raw leg (with stubs) is
    // longer than P's -- the shorter (P) leg should get meandered to match.
    // A long (100-unit) centerline gives the meander plenty of room to
    // fully close a 20-unit gap at gap-derived amplitude/pitch (1.5/3.0).
    const std::vector<Point2D> centerline = {Point2D(0, 0), Point2D(100, 0)};
    const Point2D pStart(0, 0.5), pEnd(100, 0.5);      // flush with the centerline: no stub
    const Point2D nStart(-10, -0.5), nEnd(110, -0.5);  // 10 units of stub on each end: +20 total

    const DiffPairResult result = routeDiffPair(centerline, 1.0, pStart, pEnd, nStart, nEnd);
    REQUIRE(result.ok);
    REQUIRE(result.nLength == Approx(120.0)); // N's raw leg: 100 + 10 + 10

    // After matching, both legs should be within a tooth's worth of each
    // other (can't always land exactly on target -- see LengthTuning's own
    // ceil-to-whole-tooth contract), and P must have actually grown from
    // its raw 100 to approach N's 120.
    REQUIRE(result.lengthMatched);
    REQUIRE(result.pLength >= 119.0);
    REQUIRE(std::abs(result.pLength - result.nLength) < 5.0); // roughly matched, within meander granularity
}

TEST_CASE("routeDiffPair rejects a degenerate centerline", "[diffpair]") {
    REQUIRE_FALSE(routeDiffPair({Point2D(0, 0)}, 1.0, Point2D(0, 0), Point2D(1, 0), Point2D(0, 1), Point2D(1, 1)).ok);
    REQUIRE_FALSE(routeDiffPair({Point2D(0, 0), Point2D(10, 0)}, 0.0, Point2D(0, 0), Point2D(10, 0), Point2D(0, 1),
                                Point2D(10, 1))
                     .ok);
}

TEST_CASE("routeDiffPair's NetClass overload uses the class's own diffPairGap", "[diffpair][netclass]") {
    const std::vector<Point2D> centerline = {Point2D(0, 0), Point2D(50, 0)};
    const Point2D pStart(0, 0.5), pEnd(50, 0.5);
    const Point2D nStart(0, -0.5), nEnd(50, -0.5);

    NetClass usbClass;
    usbClass.name = "USB";
    usbClass.diffPairGap = 0.15;

    const DiffPairResult viaClass = routeDiffPair(centerline, usbClass, pStart, pEnd, nStart, nEnd);
    const DiffPairResult viaRawGap = routeDiffPair(centerline, 0.15, pStart, pEnd, nStart, nEnd);
    REQUIRE(viaClass.ok);
    REQUIRE(viaClass.pPath.size() == viaRawGap.pPath.size());
    for (std::size_t i = 0; i < viaClass.pPath.size(); ++i) {
        REQUIRE(viaClass.pPath[i].x == Approx(viaRawGap.pPath[i].x));
        REQUIRE(viaClass.pPath[i].y == Approx(viaRawGap.pPath[i].y));
    }

    // A different class's own gap really does produce different geometry
    // -- proof the overload isn't just ignoring netClass and falling
    // back to some hardcoded default.
    NetClass wideClass;
    wideClass.diffPairGap = 0.5;
    const DiffPairResult viaWideClass = routeDiffPair(centerline, wideClass, pStart, pEnd, nStart, nEnd);
    REQUIRE(viaWideClass.ok);
    bool anyDifferent = false;
    for (std::size_t i = 0; i < viaWideClass.pPath.size() && i < viaClass.pPath.size(); ++i) {
        if (std::abs(viaWideClass.pPath[i].y - viaClass.pPath[i].y) > 1e-6) anyDifferent = true;
    }
    REQUIRE(anyDifferent);
}
