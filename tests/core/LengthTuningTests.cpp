#include "core/pcb/LengthTuning.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace lcad;
using Catch::Approx;

TEST_CASE("pathLength sums consecutive vertex distances", "[lengthtuning]") {
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(3, 0), Point2D(3, 4)};
    REQUIRE(pathLength(path) == Approx(3.0 + 4.0));
    REQUIRE(pathLength({Point2D(0, 0)}) == Approx(0.0));
    REQUIRE(pathLength({}) == Approx(0.0));
}

TEST_CASE("meanderSegment keeps endpoints and footprint fixed while adding length", "[lengthtuning]") {
    const Point2D a(0, 0);
    const Point2D b(20, 0);
    double achieved = 0.0;
    const auto path = meanderSegment(a, b, /*targetExtraLength=*/5.0, /*amplitude=*/1.0, /*pitch=*/2.0, &achieved);

    REQUIRE(path.front().x == Approx(0));
    REQUIRE(path.front().y == Approx(0));
    REQUIRE(path.back().x == Approx(20));
    REQUIRE(path.back().y == Approx(0));

    // Every vertex must stay within the segment's own longitudinal span
    // (the meander doesn't grow the physical footprint).
    for (const Point2D& p : path) {
        REQUIRE(p.x >= -1e-6);
        REQUIRE(p.x <= 20.0 + 1e-6);
    }

    // Achieved length must be a real gain, at least the requested amount
    // (rounds UP to a whole number of teeth, per the header's own contract).
    REQUIRE(achieved >= 5.0);
    REQUIRE(pathLength(path) == Approx(20.0 + achieved).margin(1e-9));

    // Exact tooth math: one tooth over pitch=2, amplitude=1 has two legs of
    // length sqrt(1^2+1^2) = sqrt(2), so extra length per tooth =
    // 2*sqrt(2) - 2 ~= 0.828.
    const double perTooth = 2.0 * std::sqrt(2.0) - 2.0;
    const int expectedTeeth = static_cast<int>(std::ceil(5.0 / perTooth));
    REQUIRE(achieved == Approx(expectedTeeth * perTooth));
}

TEST_CASE("meanderSegment caps at the available footprint", "[lengthtuning]") {
    // Only 10 units long, pitch 2 => at most 5 teeth regardless of how much
    // extra length is requested.
    double achieved = 0.0;
    const auto path = meanderSegment(Point2D(0, 0), Point2D(10, 0), /*targetExtraLength=*/1000.0, 2.0, 2.0, &achieved);
    const double perTooth = 2.0 * std::sqrt(1.0 * 1.0 + 2.0 * 2.0) - 2.0; // pitch/2=1, amplitude=2
    REQUIRE(achieved == Approx(5 * perTooth));
    REQUIRE(path.back().x == Approx(10));
}

TEST_CASE("meanderSegment returns the bare segment for degenerate inputs", "[lengthtuning]") {
    REQUIRE(meanderSegment(Point2D(0, 0), Point2D(10, 0), 0.0, 1.0, 2.0).size() == 2);
    REQUIRE(meanderSegment(Point2D(0, 0), Point2D(10, 0), 5.0, 0.0, 2.0).size() == 2);
    REQUIRE(meanderSegment(Point2D(0, 0), Point2D(10, 0), 5.0, 1.0, 0.0).size() == 2);
    // Segment shorter than one pitch: no room for a tooth.
    REQUIRE(meanderSegment(Point2D(0, 0), Point2D(1, 0), 5.0, 1.0, 2.0).size() == 2);
}

TEST_CASE("tuneTrackLength meanders the longest segment to hit a target", "[lengthtuning]") {
    // Two segments: a short 5-unit leg then a long 20-unit leg -- the
    // tuner must pick the 20-unit one.
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(5, 0), Point2D(25, 0)};
    const TuneResult result = tuneTrackLength(path, /*targetLength=*/30.0, /*amplitude=*/1.5, /*pitch=*/2.0);

    REQUIRE(result.originalLength == Approx(25.0));
    REQUIRE(result.metTarget);
    REQUIRE(result.achievedLength >= 30.0);
    REQUIRE(pathLength(result.path) == Approx(result.achievedLength).margin(1e-9));
    // The short 5-unit leg's own two endpoints must survive untouched.
    REQUIRE(result.path.front().x == Approx(0));
    REQUIRE(result.path[1].x == Approx(5)); // still the original bend point
}

TEST_CASE("tuneTrackLength leaves an already-long-enough path unchanged", "[lengthtuning]") {
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(10, 0)};
    const TuneResult result = tuneTrackLength(path, 5.0, 1.0, 2.0);
    REQUIRE(result.metTarget);
    REQUIRE(result.path.size() == 2);
    REQUIRE(result.achievedLength == Approx(10.0));
}

TEST_CASE("tuneTrackLength reports metTarget=false when the segment can't fit enough meander", "[lengthtuning]") {
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(10, 0)};
    const TuneResult result = tuneTrackLength(path, /*targetLength=*/1000.0, /*amplitude=*/1.0, /*pitch=*/2.0);
    REQUIRE_FALSE(result.metTarget);
    REQUIRE(result.achievedLength < 1000.0);
    REQUIRE(result.achievedLength > result.originalLength); // still did what it could
}
