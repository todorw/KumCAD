#include "core/geometry/PolygonOps.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

TEST_CASE("regularPolygonVertices builds an inscribed square with vertices exactly at the given radius",
         "[polygonops]") {
    const auto verts = regularPolygonVertices(Point2D(0, 0), 10.0, 4, true);
    REQUIRE(verts.size() == 4);
    for (const Point2D& v : verts) {
        REQUIRE(v.distanceTo(Point2D(0, 0)) == Approx(10.0).margin(1e-9));
    }
    // startAngle defaults to 0 -- first vertex at world +X.
    REQUIRE(verts[0].x == Approx(10.0).margin(1e-9));
    REQUIRE(verts[0].y == Approx(0.0).margin(1e-9));
}

TEST_CASE("regularPolygonVertices circumscribed mode places each edge midpoint exactly at the given apothem",
         "[polygonops]") {
    // A circumscribed hexagon of apothem 10: each edge's own midpoint
    // must sit exactly 10 units from center -- the vertex radius itself
    // is larger (radius/cos(pi/6)).
    const int sides = 6;
    const double apothem = 10.0;
    const auto verts = regularPolygonVertices(Point2D(0, 0), apothem, sides, false);
    REQUIRE(verts.size() == static_cast<std::size_t>(sides));

    const double expectedVertexRadius = apothem / std::cos(M_PI / sides);
    for (const Point2D& v : verts) {
        REQUIRE(v.distanceTo(Point2D(0, 0)) == Approx(expectedVertexRadius).margin(1e-9));
    }

    for (int i = 0; i < sides; ++i) {
        const Point2D& a = verts[static_cast<std::size_t>(i)];
        const Point2D& b = verts[static_cast<std::size_t>((i + 1) % sides)];
        const Point2D mid = a + (b - a) * 0.5;
        REQUIRE(mid.distanceTo(Point2D(0, 0)) == Approx(apothem).margin(1e-9));
    }
}

TEST_CASE("regularPolygonVertices honors a nonzero start angle and a non-origin center", "[polygonops]") {
    const auto verts = regularPolygonVertices(Point2D(5, 5), 2.0, 3, true, M_PI / 2.0);
    REQUIRE(verts.size() == 3);
    // First vertex at 90 degrees from center: straight up.
    REQUIRE(verts[0].x == Approx(5.0).margin(1e-9));
    REQUIRE(verts[0].y == Approx(7.0).margin(1e-9));
}

TEST_CASE("regularPolygonVertices returns empty for degenerate inputs", "[polygonops]") {
    REQUIRE(regularPolygonVertices(Point2D(0, 0), 10.0, 2, true).empty());  // fewer than 3 sides
    REQUIRE(regularPolygonVertices(Point2D(0, 0), 0.0, 5, true).empty());   // zero radius
    REQUIRE(regularPolygonVertices(Point2D(0, 0), -5.0, 5, true).empty()); // negative radius
}
