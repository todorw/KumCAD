#include "core/geometry/RegionBuild.h"

#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("closedCurveToRegionLoop tessellates a circle into the requested segment count", "[regionbuild]") {
    CircleEntity circle(1, 0, Point2D(2, 3), 5.0);
    const auto loop = closedCurveToRegionLoop(circle, 40);
    REQUIRE(loop.has_value());
    REQUIRE(loop->size() == 40);
    for (const Point2D& v : *loop) REQUIRE(v.distanceTo(Point2D(2, 3)) == Approx(5.0).margin(1e-6));
}

TEST_CASE("closedCurveToRegionLoop accepts a closed polyline and flattens any bulges", "[regionbuild]") {
    std::vector<Point2D> verts{Point2D(0, 0), Point2D(10, 0), Point2D(10, 10), Point2D(0, 10)};
    PolylineEntity square(1, 0, verts, /*closed=*/true);
    const auto loop = closedCurveToRegionLoop(square);
    REQUIRE(loop.has_value());
    REQUIRE(loop->size() == 4);
}

TEST_CASE("closedCurveToRegionLoop rejects an open polyline", "[regionbuild]") {
    std::vector<Point2D> verts{Point2D(0, 0), Point2D(10, 0), Point2D(10, 10)};
    PolylineEntity open(1, 0, verts, /*closed=*/false);
    REQUIRE_FALSE(closedCurveToRegionLoop(open).has_value());
}

TEST_CASE("closedCurveToRegionLoop rejects entity types it doesn't model as closed curves", "[regionbuild]") {
    LineEntity line(1, 0, Point2D(0, 0), Point2D(10, 0));
    REQUIRE_FALSE(closedCurveToRegionLoop(line).has_value());
}
