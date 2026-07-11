#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/ModifyOps.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/SnapGeometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;

namespace {

lcad::BoundingBox box(double x0, double y0, double x1, double y1) {
    lcad::BoundingBox b;
    b.expand(lcad::Point2D(x0, y0));
    b.expand(lcad::Point2D(x1, y1));
    return b;
}

} // namespace

TEST_CASE("stretchedClone moves only the line endpoint inside the window", "[modifyops]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));

    const auto stretched = lcad::stretchedClone(line, box(8, -2, 12, 2), lcad::Point2D(5, 3));
    REQUIRE(stretched);
    const auto& result = static_cast<const lcad::LineEntity&>(*stretched);
    REQUIRE(result.start().x == Approx(0.0)); // outside the window: untouched
    REQUIRE(result.end().x == Approx(15.0));
    REQUIRE(result.end().y == Approx(3.0));

    // Window touching nothing: no clone at all.
    REQUIRE(lcad::stretchedClone(line, box(20, 20, 30, 30), lcad::Point2D(5, 3)) == nullptr);

    // Both endpoints inside: rigid move.
    const auto moved = lcad::stretchedClone(line, box(-1, -1, 11, 1), lcad::Point2D(2, 2));
    REQUIRE(moved);
    const auto& rigid = static_cast<const lcad::LineEntity&>(*moved);
    REQUIRE(rigid.start().x == Approx(2.0));
    REQUIRE(rigid.end().x == Approx(12.0));
}

TEST_CASE("stretchedClone moves polyline vertices inside the window", "[modifyops]") {
    lcad::PolylineEntity pl(1, 0, {{0, 0}, {10, 0}, {10, 10}}, false);

    const auto stretched = lcad::stretchedClone(pl, box(5, -5, 15, 5), lcad::Point2D(0, -2));
    REQUIRE(stretched);
    const auto& result = static_cast<const lcad::PolylineEntity&>(*stretched);
    REQUIRE(result.vertices()[0].y == Approx(0.0));
    REQUIRE(result.vertices()[1].y == Approx(-2.0)); // the only vertex in the window
    REQUIRE(result.vertices()[2].y == Approx(10.0));
}

TEST_CASE("stretchedClone translates a circle only when its center is inside", "[modifyops]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(5, 5), 2.0);

    REQUIRE(lcad::stretchedClone(circle, box(6, 6, 8, 8), lcad::Point2D(1, 1)) == nullptr);

    const auto moved = lcad::stretchedClone(circle, box(4, 4, 6, 6), lcad::Point2D(1, 1));
    REQUIRE(moved);
    REQUIRE(static_cast<const lcad::CircleEntity&>(*moved).center().x == Approx(6.0));
}

TEST_CASE("curveLength measures lines, arcs, and open polylines", "[modifyops]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(3, 4));
    REQUIRE(lcad::curveLength(line).value() == Approx(5.0));

    lcad::ArcEntity arc(2, 0, lcad::Point2D(0, 0), 2.0, 0.0, M_PI); // half circle
    REQUIRE(lcad::curveLength(arc).value() == Approx(2.0 * M_PI));

    lcad::PolylineEntity pl(3, 0, {{0, 0}, {10, 0}, {10, 5}}, false);
    REQUIRE(lcad::curveLength(pl).value() == Approx(15.0));

    lcad::CircleEntity circle(4, 0, lcad::Point2D(0, 0), 1.0);
    REQUIRE_FALSE(lcad::curveLength(circle).has_value());
}

TEST_CASE("lengthenedClone extends the picked end of a line", "[modifyops]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));

    const auto longer = lcad::lengthenedClone(line, lcad::Point2D(9, 1), 5.0);
    REQUIRE(longer);
    const auto& result = static_cast<const lcad::LineEntity&>(*longer);
    REQUIRE(result.start().x == Approx(0.0));
    REQUIRE(result.end().x == Approx(15.0));

    const auto shorterAtStart = lcad::lengthenedClone(line, lcad::Point2D(1, 1), -3.0);
    REQUIRE(shorterAtStart);
    const auto& result2 = static_cast<const lcad::LineEntity&>(*shorterAtStart);
    REQUIRE(result2.start().x == Approx(3.0));
    REQUIRE(result2.end().x == Approx(10.0));

    // Shortening past zero length degenerates.
    REQUIRE(lcad::lengthenedClone(line, lcad::Point2D(9, 1), -12.0) == nullptr);
}

TEST_CASE("lengthenedClone extends an arc's sweep at the picked end", "[modifyops]") {
    lcad::ArcEntity arc(1, 0, lcad::Point2D(0, 0), 10.0, 0.0, M_PI / 2);

    // Extend by a quarter-circle's length at the end (which is at angle 90).
    const double quarterLen = 10.0 * M_PI / 2;
    const auto longer = lcad::lengthenedClone(arc, lcad::Point2D(0, 10), quarterLen);
    REQUIRE(longer);
    const auto& result = static_cast<const lcad::ArcEntity&>(*longer);
    REQUIRE(result.startAngle() == Approx(0.0));
    REQUIRE(std::fmod(result.endAngle() + 2 * M_PI, 2 * M_PI) == Approx(M_PI));

    // Growing past a full circle degenerates.
    REQUIRE(lcad::lengthenedClone(arc, lcad::Point2D(0, 10), 10.0 * 2 * M_PI) == nullptr);
}

TEST_CASE("breakEntity removes the middle of a line", "[modifyops]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    lcad::EntityId next = 100;
    const auto makeId = [&next]() { return next++; };

    const auto broken = lcad::breakEntity(line, lcad::Point2D(3, 1), lcad::Point2D(7, -1), makeId);
    REQUIRE(broken.ok);
    REQUIRE(broken.pieces.size() == 2);
    const auto& a = static_cast<const lcad::LineEntity&>(*broken.pieces[0]);
    const auto& b = static_cast<const lcad::LineEntity&>(*broken.pieces[1]);
    REQUIRE(a.start().x == Approx(0.0));
    REQUIRE(a.end().x == Approx(3.0));
    REQUIRE(b.start().x == Approx(7.0));
    REQUIRE(b.end().x == Approx(10.0));
}

TEST_CASE("breakEntity at a single point splits without removing", "[modifyops]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    lcad::EntityId next = 100;
    const auto makeId = [&next]() { return next++; };

    const auto broken = lcad::breakEntity(line, lcad::Point2D(4, 0), lcad::Point2D(4, 0), makeId);
    REQUIRE(broken.ok);
    REQUIRE(broken.pieces.size() == 2);
    REQUIRE(static_cast<const lcad::LineEntity&>(*broken.pieces[0]).end().x == Approx(4.0));
    REQUIRE(static_cast<const lcad::LineEntity&>(*broken.pieces[1]).start().x == Approx(4.0));
}

TEST_CASE("breakEntity turns a circle into an arc, removing CCW first-to-second", "[modifyops]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(0, 0), 5.0);
    lcad::EntityId next = 100;
    const auto makeId = [&next]() { return next++; };

    // First point at 0 deg, second at 90 deg: the quarter from 0 to 90 goes,
    // leaving the arc from 90 around to 360.
    const auto broken = lcad::breakEntity(circle, lcad::Point2D(5, 0), lcad::Point2D(0, 5), makeId);
    REQUIRE(broken.ok);
    REQUIRE(broken.pieces.size() == 1);
    const auto& arc = static_cast<const lcad::ArcEntity&>(*broken.pieces[0]);
    REQUIRE(arc.startAngle() == Approx(M_PI / 2));
    REQUIRE(std::fmod(arc.endAngle() + 2 * M_PI, 2 * M_PI) == Approx(0.0).margin(1e-9));
}

TEST_CASE("breakEntity splits a straight polyline into two chains", "[modifyops]") {
    lcad::PolylineEntity pl(1, 0, {{0, 0}, {10, 0}, {10, 10}}, false);
    lcad::EntityId next = 100;
    const auto makeId = [&next]() { return next++; };

    const auto broken = lcad::breakEntity(pl, lcad::Point2D(4, 0), lcad::Point2D(10, 3), makeId);
    REQUIRE(broken.ok);
    REQUIRE(broken.pieces.size() == 2);
    const auto& head = static_cast<const lcad::PolylineEntity&>(*broken.pieces[0]);
    const auto& tail = static_cast<const lcad::PolylineEntity&>(*broken.pieces[1]);
    REQUIRE(head.vertices().size() == 2);
    REQUIRE(head.vertices().back().x == Approx(4.0));
    REQUIRE(tail.vertices().front().y == Approx(3.0));
    REQUIRE(tail.vertices().back().y == Approx(10.0));
}

TEST_CASE("nearestPointOnEntity projects onto lines, circles, and arcs", "[snapgeometry]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    auto p = lcad::nearestPointOnEntity(line, lcad::Point2D(4, 3));
    REQUIRE(p);
    REQUIRE(p->x == Approx(4.0));
    REQUIRE(p->y == Approx(0.0));

    lcad::CircleEntity circle(2, 0, lcad::Point2D(0, 0), 5.0);
    p = lcad::nearestPointOnEntity(circle, lcad::Point2D(10, 0));
    REQUIRE(p);
    REQUIRE(p->x == Approx(5.0));

    // A point outside the arc's sweep snaps to the nearer endpoint.
    lcad::ArcEntity arc(3, 0, lcad::Point2D(0, 0), 5.0, 0.0, M_PI / 2);
    p = lcad::nearestPointOnEntity(arc, lcad::Point2D(4, -1));
    REQUIRE(p);
    REQUIRE(p->x == Approx(5.0));
    REQUIRE(p->y == Approx(0.0).margin(1e-9));
}

TEST_CASE("perpendicularPoints finds feet on lines and circles", "[snapgeometry]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    auto feet = lcad::perpendicularPoints(line, lcad::Point2D(6, 4));
    REQUIRE(feet.size() == 1);
    REQUIRE(feet[0].x == Approx(6.0));
    REQUIRE(feet[0].y == Approx(0.0));

    // Beyond the segment: no foot.
    REQUIRE(lcad::perpendicularPoints(line, lcad::Point2D(20, 4)).empty());

    lcad::CircleEntity circle(2, 0, lcad::Point2D(0, 0), 5.0);
    feet = lcad::perpendicularPoints(circle, lcad::Point2D(10, 0));
    REQUIRE(feet.size() == 2);
    REQUIRE(feet[0].x == Approx(5.0));
    REQUIRE(feet[1].x == Approx(-5.0));
}

TEST_CASE("tangentPoints from an external point touch the circle", "[snapgeometry]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(0, 0), 5.0);

    const auto tangents = lcad::tangentPoints(circle, lcad::Point2D(10, 0));
    REQUIRE(tangents.size() == 2);
    for (const auto& t : tangents) {
        REQUIRE(t.distanceTo(lcad::Point2D(0, 0)) == Approx(5.0));
        // Tangent line is perpendicular to the radius at the touch point.
        const lcad::Point2D radial = t; // center is the origin
        const lcad::Point2D toEye = lcad::Point2D(10, 0) - t;
        REQUIRE(radial.dot(toEye) == Approx(0.0).margin(1e-9));
    }

    // From inside: none.
    REQUIRE(lcad::tangentPoints(circle, lcad::Point2D(1, 0)).empty());
}
