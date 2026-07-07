#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

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
