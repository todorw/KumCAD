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

TEST_CASE("meanderSegment stays within the segment's own footprint (longitudinal span) even when it "
         "has to grow the amplitude to reach the target",
         "[lengthtuning]") {
    // Only 10 units long, pitch 2 => at most 5 teeth no matter what, so a
    // target this function's own base amplitude (2.0) can't reach with 5
    // teeth forces amplitude to grow instead -- still only 5 teeth (no
    // more room along the segment), but each swings wider.
    double achieved = 0.0;
    const auto path = meanderSegment(Point2D(0, 0), Point2D(10, 0), /*targetExtraLength=*/50.0, 2.0, 2.0, &achieved);

    // Real gain, not just the old fixed-amplitude cap (5 teeth at
    // amplitude=2.0 would only reach ~12.36 of extra length).
    REQUIRE(achieved >= 50.0);

    // Still exactly 5 teeth (5 peaks), just wider ones -- x stays within
    // the original longitudinal span the whole time.
    int peakCount = 0;
    double maxAbsY = 0.0;
    for (const Point2D& p : path) {
        REQUIRE(p.x >= -1e-6);
        REQUIRE(p.x <= 10.0 + 1e-6);
        if (std::abs(p.y) > 1e-6) {
            ++peakCount;
            maxAbsY = std::max(maxAbsY, std::abs(p.y));
        }
    }
    REQUIRE(peakCount == 5);
    // The grown amplitude must be well past the requested 2.0 -- proof
    // growth actually happened, not just more teeth (there's no more
    // teeth to add here).
    REQUIRE(maxAbsY > 2.0);
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

TEST_CASE("tuneTrackLength grows the amplitude to still meet a target the base amplitude alone "
         "couldn't reach with the available teeth",
         "[lengthtuning]") {
    // Same scenario a fixed-amplitude meander would have fallen short on
    // (5 teeth at amplitude=1.0 over a 10-unit segment can't add anywhere
    // near 1000 units of extra length) -- meanderSegment's own amplitude
    // growth means this now actually meets the target.
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(10, 0)};
    const TuneResult result = tuneTrackLength(path, /*targetLength=*/1000.0, /*amplitude=*/1.0, /*pitch=*/2.0);
    REQUIRE(result.metTarget);
    REQUIRE(result.achievedLength >= 1000.0);
}

TEST_CASE("tuneTrackLength reports metTarget=false only when the segment can't fit even one tooth",
         "[lengthtuning]") {
    // 1-unit segment, pitch=2: no room for a single tooth at all, so no
    // amount of amplitude growth can help -- the one real remaining
    // failure case.
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(1, 0)};
    const TuneResult result = tuneTrackLength(path, /*targetLength=*/100.0, /*amplitude=*/1.0, /*pitch=*/2.0);
    REQUIRE_FALSE(result.metTarget);
    REQUIRE(result.achievedLength == Approx(result.originalLength));
}
