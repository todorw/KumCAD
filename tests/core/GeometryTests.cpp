#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("LineEntity distance and bounding box", "[geometry]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));

    REQUIRE(line.distanceTo(lcad::Point2D(5, 3)) == Approx(3.0));
    REQUIRE(line.distanceTo(lcad::Point2D(-5, 0)) == Approx(5.0)); // beyond the start endpoint
    REQUIRE(line.distanceTo(lcad::Point2D(15, 0)) == Approx(5.0)); // beyond the end endpoint

    const auto box = line.boundingBox();
    REQUIRE(box.min.x == Approx(0.0));
    REQUIRE(box.max.x == Approx(10.0));
}

TEST_CASE("CircleEntity distance and bounding box", "[geometry]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(0, 0), 5.0);

    REQUIRE(circle.distanceTo(lcad::Point2D(5, 0)) == Approx(0.0));
    REQUIRE(circle.distanceTo(lcad::Point2D(0, 0)) == Approx(5.0));
    REQUIRE(circle.distanceTo(lcad::Point2D(10, 0)) == Approx(5.0));

    const auto box = circle.boundingBox();
    REQUIRE(box.min.x == Approx(-5.0));
    REQUIRE(box.max.x == Approx(5.0));
    REQUIRE(box.min.y == Approx(-5.0));
    REQUIRE(box.max.y == Approx(5.0));
}

TEST_CASE("ArcEntity sweep containment", "[geometry]") {
    // Quarter arc from 0 to 90 degrees.
    lcad::ArcEntity arc(1, 0, lcad::Point2D(0, 0), 5.0, 0.0, M_PI / 2);

    REQUIRE(arc.distanceTo(lcad::Point2D(5, 0)) == Approx(0.0));   // on the arc, at the start
    REQUIRE(arc.distanceTo(lcad::Point2D(0, 5)) == Approx(0.0));   // on the arc, at the end
    REQUIRE(arc.distanceTo(lcad::Point2D(-5, 0)) > 0.1);           // outside the sweep

    const auto box = arc.boundingBox();
    REQUIRE(box.min.x == Approx(0.0));
    REQUIRE(box.max.x == Approx(5.0));
    REQUIRE(box.min.y == Approx(0.0));
    REQUIRE(box.max.y == Approx(5.0));
}

TEST_CASE("PolylineEntity distance across segments", "[geometry]") {
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    lcad::PolylineEntity pl(1, 0, verts, false);

    REQUIRE(pl.distanceTo(lcad::Point2D(5, 1)) == Approx(1.0));
    REQUIRE(pl.distanceTo(lcad::Point2D(11, 5)) == Approx(1.0));
    REQUIRE(pl.distanceTo(lcad::Point2D(0, 10)) > 5.0); // not near the open end
}

TEST_CASE("LineEntity translate and grip editing", "[geometry]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));

    line.translate(lcad::Point2D(1, 2));
    REQUIRE(line.start().x == Approx(1.0));
    REQUIRE(line.end().x == Approx(11.0));
    REQUIRE(line.start().y == Approx(2.0));

    const auto grips = line.gripPoints();
    REQUIRE(grips.size() == 3); // start, end, midpoint

    line.moveGripPoint(1, lcad::Point2D(20, 20)); // drag the end grip
    REQUIRE(line.end().x == Approx(20.0));
    REQUIRE(line.end().y == Approx(20.0));
    REQUIRE(line.start().x == Approx(1.0)); // start untouched
}

TEST_CASE("CircleEntity translate and grip editing", "[geometry]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(0, 0), 5.0);

    circle.translate(lcad::Point2D(3, 4));
    REQUIRE(circle.center().x == Approx(3.0));
    REQUIRE(circle.center().y == Approx(4.0));

    circle.moveGripPoint(1, lcad::Point2D(3, 14)); // drag the radius grip
    REQUIRE(circle.radius() == Approx(10.0));
    REQUIRE(circle.center().x == Approx(3.0)); // center untouched by resize
}

TEST_CASE("PolylineEntity translate and grip editing", "[geometry]") {
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}};
    lcad::PolylineEntity pl(1, 0, verts, false);

    pl.translate(lcad::Point2D(5, 5));
    REQUIRE(pl.vertices()[0].x == Approx(5.0));
    REQUIRE(pl.vertices()[1].x == Approx(15.0));

    pl.moveGripPoint(0, lcad::Point2D(-1, -1));
    REQUIRE(pl.vertices()[0].x == Approx(-1.0));
    REQUIRE(pl.vertices()[1].x == Approx(15.0)); // other vertex untouched
}

TEST_CASE("LineEntity rotate and scale", "[geometry]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(10, 0), lcad::Point2D(20, 0));

    line.rotate(lcad::Point2D(0, 0), M_PI / 2); // 90 degrees CCW about origin
    REQUIRE(line.start().x == Approx(0.0).margin(1e-9));
    REQUIRE(line.start().y == Approx(10.0));
    REQUIRE(line.end().x == Approx(0.0).margin(1e-9));
    REQUIRE(line.end().y == Approx(20.0));

    lcad::LineEntity line2(2, 0, lcad::Point2D(10, 0), lcad::Point2D(20, 0));
    line2.scale(lcad::Point2D(10, 0), 2.0); // scale about the start point
    REQUIRE(line2.start().x == Approx(10.0)); // fixed point untouched
    REQUIRE(line2.end().x == Approx(30.0));   // 10 units away doubled to 20 units away
}

TEST_CASE("CircleEntity rotate and scale", "[geometry]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(10, 0), 5.0);

    circle.rotate(lcad::Point2D(0, 0), M_PI / 2);
    REQUIRE(circle.center().x == Approx(0.0).margin(1e-9));
    REQUIRE(circle.center().y == Approx(10.0));
    REQUIRE(circle.radius() == Approx(5.0)); // rotation doesn't affect radius

    circle.scale(circle.center(), 2.0);
    REQUIRE(circle.radius() == Approx(10.0));
}

TEST_CASE("ArcEntity rotate carries its sweep with it", "[geometry]") {
    lcad::ArcEntity arc(1, 0, lcad::Point2D(0, 0), 5.0, 0.0, M_PI / 2);

    arc.rotate(lcad::Point2D(0, 0), M_PI / 2); // spin the sweep itself by 90 degrees
    REQUIRE(arc.startAngle() == Approx(M_PI / 2));
    REQUIRE(arc.endAngle() == Approx(M_PI));

    arc.scale(lcad::Point2D(0, 0), 2.0);
    REQUIRE(arc.radius() == Approx(10.0));
}

namespace {
bool hasSnapAt(const std::vector<lcad::SnapPoint>& snaps, lcad::SnapKind kind, lcad::Point2D pt) {
    for (const auto& s : snaps) {
        if (s.kind == kind && s.point.distanceTo(pt) < 1e-6) return true;
    }
    return false;
}
} // namespace

TEST_CASE("LineEntity snap candidates", "[geometry][snap]") {
    lcad::LineEntity line(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    const auto snaps = line.snapCandidates();
    REQUIRE(snaps.size() == 3);
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Endpoint, lcad::Point2D(0, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Endpoint, lcad::Point2D(10, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Midpoint, lcad::Point2D(5, 0)));
}

TEST_CASE("CircleEntity snap candidates", "[geometry][snap]") {
    lcad::CircleEntity circle(1, 0, lcad::Point2D(0, 0), 5.0);
    const auto snaps = circle.snapCandidates();
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Center, lcad::Point2D(0, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(5, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(0, 5)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(-5, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(0, -5)));
}

TEST_CASE("ArcEntity snap candidates include midpoint and in-sweep quadrants only", "[geometry][snap]") {
    // Quarter arc from 0 to 90 degrees: midpoint at 45deg, only the 0 and 90 quadrants are in-sweep.
    lcad::ArcEntity arc(1, 0, lcad::Point2D(0, 0), 5.0, 0.0, M_PI / 2);
    const auto snaps = arc.snapCandidates();
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Endpoint, lcad::Point2D(5, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Endpoint, lcad::Point2D(0, 5)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Center, lcad::Point2D(0, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Midpoint, lcad::Point2D(5 * std::cos(M_PI / 4), 5 * std::sin(M_PI / 4))));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(5, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(0, 5)));
    REQUIRE_FALSE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(-5, 0))); // out of sweep
}

TEST_CASE("PolylineEntity snap candidates", "[geometry][snap]") {
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    lcad::PolylineEntity pl(1, 0, verts, false);
    const auto snaps = pl.snapCandidates();
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Endpoint, lcad::Point2D(0, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Endpoint, lcad::Point2D(10, 10)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Midpoint, lcad::Point2D(5, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Midpoint, lcad::Point2D(10, 5)));
}

TEST_CASE("EllipseEntity distance, bounding box, and snap candidates", "[geometry]") {
    lcad::EllipseEntity ellipse(1, 0, lcad::Point2D(0, 0), 10.0, 5.0);

    REQUIRE(ellipse.distanceTo(lcad::Point2D(10, 0)) == Approx(0.0).margin(0.05));
    REQUIRE(ellipse.distanceTo(lcad::Point2D(0, 5)) == Approx(0.0).margin(0.05));
    REQUIRE(ellipse.distanceTo(lcad::Point2D(0, 0)) > 4.0); // center is well inside, far from the rim

    const auto box = ellipse.boundingBox();
    REQUIRE(box.min.x == Approx(-10.0));
    REQUIRE(box.max.x == Approx(10.0));
    REQUIRE(box.min.y == Approx(-5.0));
    REQUIRE(box.max.y == Approx(5.0));

    const auto snaps = ellipse.snapCandidates();
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Center, lcad::Point2D(0, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(10, 0)));
    REQUIRE(hasSnapAt(snaps, lcad::SnapKind::Quadrant, lcad::Point2D(0, 5)));
}

TEST_CASE("EllipseEntity translate, rotate, scale, and grip editing", "[geometry]") {
    lcad::EllipseEntity ellipse(1, 0, lcad::Point2D(10, 0), 10.0, 5.0);

    ellipse.translate(lcad::Point2D(1, 1));
    REQUIRE(ellipse.center().x == Approx(11.0));
    REQUIRE(ellipse.center().y == Approx(1.0));

    ellipse.rotate(lcad::Point2D(1, 1), M_PI / 2); // only the center moves, matching the axis-aligned simplification
    REQUIRE(ellipse.center().x == Approx(1.0).margin(1e-9));
    REQUIRE(ellipse.center().y == Approx(11.0));
    REQUIRE(ellipse.radiusX() == Approx(10.0));
    REQUIRE(ellipse.radiusY() == Approx(5.0));

    ellipse.scale(ellipse.center(), 2.0);
    REQUIRE(ellipse.radiusX() == Approx(20.0));
    REQUIRE(ellipse.radiusY() == Approx(10.0));

    lcad::EllipseEntity e2(2, 0, lcad::Point2D(0, 0), 10.0, 5.0);
    REQUIRE(e2.gripPoints().size() == 5);
    e2.moveGripPoint(1, lcad::Point2D(20, 0)); // drag the +X radius grip
    REQUIRE(e2.radiusX() == Approx(20.0));
    REQUIRE(e2.radiusY() == Approx(5.0)); // untouched
}

TEST_CASE("TextEntity bounding box, distance, and grip editing (unrotated)", "[geometry]") {
    lcad::TextEntity text(1, 0, lcad::Point2D(0, 0), "HI", 2.0); // 2 chars, height 2 -> approx width 0.6*2*2=2.4

    const auto box = text.boundingBox();
    REQUIRE(box.min.x == Approx(0.0));
    REQUIRE(box.min.y == Approx(0.0));
    REQUIRE(box.max.x == Approx(text.approximateWidth()));
    REQUIRE(box.max.y == Approx(2.0));

    REQUIRE(text.distanceTo(lcad::Point2D(1, 1)) == Approx(0.0)); // inside the bbox
    REQUIRE(text.distanceTo(lcad::Point2D(-1, 0)) == Approx(1.0)); // 1 unit left of the box

    REQUIRE(text.gripPoints().size() == 1);
    text.moveGripPoint(0, lcad::Point2D(5, 5));
    REQUIRE(text.position().x == Approx(5.0));
    REQUIRE(text.position().y == Approx(5.0));
}

TEST_CASE("TextEntity rotate carries its own rotation and translate/scale behave", "[geometry]") {
    lcad::TextEntity text(1, 0, lcad::Point2D(10, 0), "X", 1.0);

    text.rotate(lcad::Point2D(0, 0), M_PI / 2);
    REQUIRE(text.position().x == Approx(0.0).margin(1e-9));
    REQUIRE(text.position().y == Approx(10.0));
    REQUIRE(text.rotation() == Approx(M_PI / 2));

    text.translate(lcad::Point2D(1, 1));
    REQUIRE(text.position().x == Approx(1.0).margin(1e-9));
    REQUIRE(text.position().y == Approx(11.0));

    text.scale(text.position(), 3.0);
    REQUIRE(text.height() == Approx(3.0));
}
