#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Image.h"
#include "core/geometry/Line.h"
#include "core/geometry/ModifyOps.h"
#include "core/geometry/PointCloud.h"
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

TEST_CASE("stretchedClone translates an image only when its position is inside", "[modifyops][regression]") {
    // Regression: IMAGE had no case in stretchedClone's switch, so STRETCH
    // silently no-opped on images even when their insertion point was
    // inside the crossing window.
    lcad::ImageEntity image(1, 0, "photo.png", lcad::Point2D(5, 5), 10.0, 8.0);

    REQUIRE(lcad::stretchedClone(image, box(20, 20, 30, 30), lcad::Point2D(1, 1)) == nullptr);

    const auto moved = lcad::stretchedClone(image, box(4, 4, 6, 6), lcad::Point2D(1, 1));
    REQUIRE(moved);
    REQUIRE(static_cast<const lcad::ImageEntity&>(*moved).position().x == Approx(6.0));
}

TEST_CASE("stretchedClone translates a point cloud only when it's inside", "[modifyops][regression]") {
    lcad::PointCloudEntity cloud(1, 0, "scan.xyz", {{5, 5}, {6, 6}});

    REQUIRE(lcad::stretchedClone(cloud, box(20, 20, 30, 30), lcad::Point2D(1, 1)) == nullptr);

    const auto moved = lcad::stretchedClone(cloud, box(4, 4, 6, 6), lcad::Point2D(1, 1));
    REQUIRE(moved);
    REQUIRE(static_cast<const lcad::PointCloudEntity&>(*moved).points()[0].x == Approx(6.0));
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

TEST_CASE("lengthenedClone extends only a polyline's terminal segment", "[modifyops]") {
    // Straight chain: (0,0)-(10,0)-(10,10).
    lcad::PolylineEntity straight(1, 0, {{0, 0}, {10, 0}, {10, 10}}, false);

    const auto longer = lcad::lengthenedClone(straight, lcad::Point2D(10, 9), 5.0);
    REQUIRE(longer);
    const auto& result = static_cast<const lcad::PolylineEntity&>(*longer);
    REQUIRE(result.vertices()[0].x == Approx(0.0).margin(1e-9)); // untouched
    REQUIRE(result.vertices()[1].x == Approx(10.0));             // untouched
    REQUIRE(result.vertices()[1].y == Approx(0.0).margin(1e-9));
    REQUIRE(result.vertices()[2].y == Approx(15.0)); // extended

    const auto shorterAtStart = lcad::lengthenedClone(straight, lcad::Point2D(1, 1), -3.0);
    REQUIRE(shorterAtStart);
    const auto& result2 = static_cast<const lcad::PolylineEntity&>(*shorterAtStart);
    REQUIRE(result2.vertices()[0].x == Approx(3.0));
    REQUIRE(result2.vertices()[2].y == Approx(10.0)); // far end untouched

    // A bulged terminal segment: the same CCW semicircle from earlier tests,
    // center (10,5) radius 5, spanning -90..+90 degrees.
    lcad::PolylineEntity bulged(2, 0, {{0, 0}, {10, 0}, {10, 10}}, {0.0, 1.0}, false);
    const double quarterLen = 5.0 * M_PI / 2.0;
    const auto grown = lcad::lengthenedClone(bulged, lcad::Point2D(10, 9), quarterLen);
    REQUIRE(grown);
    const auto& result3 = static_cast<const lcad::PolylineEntity&>(*grown);
    REQUIRE(result3.vertices()[1].x == Approx(10.0)); // near end untouched
    REQUIRE(result3.vertices()[1].y == Approx(0.0).margin(1e-9));
    REQUIRE(result3.vertices()[2].x == Approx(5.0).margin(1e-6)); // swept a further quarter turn
    REQUIRE(result3.vertices()[2].y == Approx(5.0).margin(1e-6));
    REQUIRE(result3.bulgeAt(1) == Approx(std::tan(3.0 * M_PI / 8.0)));
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

TEST_CASE("breakEntity splits a polyline arc segment, recomputing sub-bulges", "[modifyops]") {
    // Line (0,0)-(10,0) then a CCW semicircle from (10,0) to (10,10):
    // center (10,5), radius 5, spanning -90..+90 degrees.
    lcad::PolylineEntity pl(1, 0, {{0, 0}, {10, 0}, {10, 10}}, {0.0, 1.0}, false);
    lcad::EntityId next = 100;
    const auto makeId = [&next]() { return next++; };

    // Break at the arc's -45 and +45 degree points (quarter and
    // three-quarter along the sweep).
    const lcad::Point2D a(10.0 + 5.0 * std::cos(-M_PI / 4), 5.0 + 5.0 * std::sin(-M_PI / 4));
    const lcad::Point2D b(10.0 + 5.0 * std::cos(M_PI / 4), 5.0 + 5.0 * std::sin(M_PI / 4));

    const auto broken = lcad::breakEntity(pl, a, b, makeId);
    REQUIRE(broken.ok);
    REQUIRE(broken.pieces.size() == 2);

    const auto& head = static_cast<const lcad::PolylineEntity&>(*broken.pieces[0]);
    REQUIRE(head.vertices().size() == 3);
    REQUIRE(head.vertices()[0].x == Approx(0.0).margin(1e-9));
    REQUIRE(head.vertices()[1].x == Approx(10.0));
    REQUIRE(head.vertices()[1].y == Approx(0.0).margin(1e-9));
    REQUIRE(head.vertices()[2].x == Approx(a.x));
    REQUIRE(head.vertices()[2].y == Approx(a.y));
    REQUIRE(head.bulgeAt(0) == Approx(0.0).margin(1e-9));       // still the straight segment
    REQUIRE(head.bulgeAt(1) == Approx(std::tan(M_PI / 16)));    // quarter of the original sweep

    const auto& tail = static_cast<const lcad::PolylineEntity&>(*broken.pieces[1]);
    REQUIRE(tail.vertices().size() == 2);
    REQUIRE(tail.vertices()[0].x == Approx(b.x));
    REQUIRE(tail.vertices()[0].y == Approx(b.y));
    REQUIRE(tail.vertices()[1].x == Approx(10.0));
    REQUIRE(tail.vertices()[1].y == Approx(10.0));
    REQUIRE(tail.bulgeAt(0) == Approx(std::tan(M_PI / 16)));

    // The two sub-arcs plus the untouched middle quarter should still trace
    // through the pole of the original semicircle -- confirms the split
    // points really landed on the arc, not just near it.
    const auto headArc = lcad::bulgeToArc(head.vertices()[1], head.vertices()[2], head.bulgeAt(1));
    REQUIRE(headArc.has_value());
    REQUIRE(headArc->center.x == Approx(10.0));
    REQUIRE(headArc->center.y == Approx(5.0));
    REQUIRE(headArc->radius == Approx(5.0));
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

TEST_CASE("divideEntity and measureEntity place points along curves", "[modifyops][divide]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    const auto thirds = lcad::divideEntity(line, 4);
    REQUIRE(thirds.size() == 3);
    REQUIRE(thirds[0].x == Approx(2.5));
    REQUIRE(thirds[1].x == Approx(5.0));
    REQUIRE(thirds[2].x == Approx(7.5));

    const auto steps = lcad::measureEntity(line, 4.0);
    REQUIRE(steps.size() == 2);
    REQUIRE(steps[0].x == Approx(4.0));
    REQUIRE(steps[1].x == Approx(8.0));

    // Circles divide into n points around the full perimeter.
    lcad::CircleEntity circle(2, 0, lcad::Point2D(0, 0), 5.0);
    const auto quarters = lcad::divideEntity(circle, 4);
    REQUIRE(quarters.size() == 4);
    REQUIRE(quarters[0].x == Approx(5.0)); // angle 0
    REQUIRE(quarters[1].y == Approx(5.0).margin(1e-9)); // angle 90

    // Polylines walk their segments.
    lcad::PolylineEntity pl(3, 0, {{0, 0}, {10, 0}, {10, 10}}, false);
    const auto halves = lcad::divideEntity(pl, 2);
    REQUIRE(halves.size() == 1);
    REQUIRE(halves[0].x == Approx(10.0));
    REQUIRE(halves[0].y == Approx(0.0).margin(1e-9));
}

TEST_CASE("findDuplicateEntities flags exact repeats, keeping the first occurrence", "[modifyops][overkill]") {
    lcad::LineEntity a(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    lcad::LineEntity aReversed(2, 0, lcad::Point2D(10, 0), lcad::Point2D(0, 0)); // same line, endpoints swapped
    lcad::LineEntity differentLayer(3, 1, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    lcad::LineEntity unrelated(4, 0, lcad::Point2D(0, 0), lcad::Point2D(5, 5));
    lcad::CircleEntity circle(5, 0, lcad::Point2D(0, 0), 3.0);

    const std::vector<const lcad::Entity*> entities{&a, &aReversed, &differentLayer, &unrelated, &circle};
    const auto duplicates = lcad::findDuplicateEntities(entities);
    REQUIRE(duplicates == std::vector<std::size_t>{1}); // only aReversed duplicates a; nothing else matches
}

TEST_CASE("findDuplicateEntities compares circles, arcs, and polylines by their own geometry", "[modifyops][overkill]") {
    lcad::CircleEntity c1(1, 0, lcad::Point2D(1, 1), 5.0);
    lcad::CircleEntity c2(2, 0, lcad::Point2D(1, 1), 5.0);       // duplicate
    lcad::CircleEntity c3(3, 0, lcad::Point2D(1, 1), 5.1);       // different radius: not a duplicate
    lcad::ArcEntity a1(4, 0, lcad::Point2D(0, 0), 2.0, 0.0, M_PI / 2);
    lcad::ArcEntity a2(5, 0, lcad::Point2D(0, 0), 2.0, 0.0, M_PI / 2); // duplicate
    lcad::PolylineEntity p1(6, 0, {{0, 0}, {1, 0}, {1, 1}}, false);
    lcad::PolylineEntity p2(7, 0, {{0, 0}, {1, 0}, {1, 1}}, false); // duplicate
    lcad::PolylineEntity p3(8, 0, {{0, 0}, {1, 0}, {1, 1}}, true);  // closed instead of open: not a duplicate

    const std::vector<const lcad::Entity*> entities{&c1, &c2, &c3, &a1, &a2, &p1, &p2, &p3};
    const auto duplicates = lcad::findDuplicateEntities(entities);
    REQUIRE(duplicates == std::vector<std::size_t>{1, 4, 6});
}

TEST_CASE("findDuplicateEntities never flags a solo entity or mismatched types", "[modifyops][overkill]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    lcad::CircleEntity circle(2, 0, lcad::Point2D(0, 0), 5.0);
    const std::vector<const lcad::Entity*> entities{&line, &circle};
    REQUIRE(lcad::findDuplicateEntities(entities).empty());
}
