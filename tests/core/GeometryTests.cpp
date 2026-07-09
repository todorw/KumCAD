#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Intersect.h"
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

TEST_CASE("Entity mirror across a line", "[geometry][mirror]") {
    SECTION("line reflects both endpoints across a vertical axis") {
        lcad::LineEntity line(1, 0, lcad::Point2D(1, 0), lcad::Point2D(3, 4));
        line.mirror(lcad::Point2D(0, -1), lcad::Point2D(0, 1)); // x = 0 axis
        REQUIRE(line.start().x == Approx(-1.0));
        REQUIRE(line.start().y == Approx(0.0));
        REQUIRE(line.end().x == Approx(-3.0));
        REQUIRE(line.end().y == Approx(4.0));
    }

    SECTION("circle reflects its center only") {
        lcad::CircleEntity circle(1, 0, lcad::Point2D(2, 3), 5.0);
        circle.mirror(lcad::Point2D(-1, 0), lcad::Point2D(1, 0)); // y = 0 axis
        REQUIRE(circle.center().x == Approx(2.0));
        REQUIRE(circle.center().y == Approx(-3.0));
        REQUIRE(circle.radius() == Approx(5.0));
    }

    SECTION("arc keeps its start/end points (swapped) and CCW sweep") {
        // Quarter arc in the first quadrant, mirrored across y = 0.
        lcad::ArcEntity arc(1, 0, lcad::Point2D(0, 0), 5.0, 0.0, M_PI / 2);
        const lcad::Point2D oldStart = arc.startPoint();
        const lcad::Point2D oldEnd = arc.endPoint();
        arc.mirror(lcad::Point2D(-1, 0), lcad::Point2D(1, 0));

        // Mirrored arc runs through the fourth quadrant: old start (5,0)
        // stays on the arc as the new end, old end (0,5) maps to (0,-5).
        REQUIRE(arc.endPoint().x == Approx(oldStart.x));
        REQUIRE(arc.endPoint().y == Approx(-oldStart.y).margin(1e-9));
        REQUIRE(arc.startPoint().x == Approx(oldEnd.x).margin(1e-9));
        REQUIRE(arc.startPoint().y == Approx(-oldEnd.y));
        // A point squarely inside the mirrored sweep is on the arc.
        REQUIRE(arc.distanceTo(lcad::Point2D(5.0 * std::cos(-M_PI / 4), 5.0 * std::sin(-M_PI / 4))) ==
                Approx(0.0).margin(1e-9));
    }

    SECTION("degenerate mirror line leaves the entity unchanged") {
        lcad::LineEntity line(1, 0, lcad::Point2D(1, 2), lcad::Point2D(3, 4));
        line.mirror(lcad::Point2D(5, 5), lcad::Point2D(5, 5));
        REQUIRE(line.start().x == Approx(1.0));
        REQUIRE(line.end().y == Approx(4.0));
    }
}

TEST_CASE("EllipseEntity rotation", "[geometry][ellipse]") {
    lcad::EllipseEntity ellipse(1, 0, lcad::Point2D(0, 0), 10.0, 4.0);

    SECTION("rotate() carries the rotation angle, not just the center") {
        ellipse.rotate(lcad::Point2D(0, 0), M_PI / 2);
        REQUIRE(ellipse.rotation() == Approx(M_PI / 2));
        // Rotated 90 degrees, the long axis now runs along Y.
        const auto box = ellipse.boundingBox();
        REQUIRE(box.max.x == Approx(4.0));
        REQUIRE(box.max.y == Approx(10.0));
        // The world point that was (10, 0) is now (0, 10) and still on the ellipse.
        REQUIRE(ellipse.distanceTo(lcad::Point2D(0, 10)) == Approx(0.0).margin(1e-2));
    }

    SECTION("grip points follow the rotated axes") {
        ellipse.rotate(lcad::Point2D(0, 0), M_PI / 2);
        const auto grips = ellipse.gripPoints();
        REQUIRE(grips.size() == 5);
        REQUIRE(grips[1].x == Approx(0.0).margin(1e-9)); // +X axis grip now points up
        REQUIRE(grips[1].y == Approx(10.0));
    }

    SECTION("axis grips resize along the rotated axis") {
        ellipse.rotate(lcad::Point2D(0, 0), M_PI / 2);
        ellipse.moveGripPoint(1, lcad::Point2D(0, 12)); // drag the +X grip further out
        REQUIRE(ellipse.radiusX() == Approx(12.0));
        REQUIRE(ellipse.radiusY() == Approx(4.0));
    }

    SECTION("mirror reflects center and rotation") {
        lcad::EllipseEntity rotated(1, 0, lcad::Point2D(5, 5), 10.0, 4.0, M_PI / 6);
        rotated.mirror(lcad::Point2D(0, -1), lcad::Point2D(0, 1)); // x = 0 axis
        REQUIRE(rotated.center().x == Approx(-5.0));
        REQUIRE(rotated.center().y == Approx(5.0));
        // Reflection across a vertical line maps angle t to pi - t.
        const double expected = M_PI - M_PI / 6;
        REQUIRE(std::fmod(rotated.rotation() - expected + 4 * M_PI, 2 * M_PI) == Approx(0.0).margin(1e-9));
    }
}

TEST_CASE("Intersection primitives", "[geometry][intersect]") {
    SECTION("crossing segments meet at one point") {
        const auto pts = lcad::intersectSegmentSegment({0, 0}, {10, 10}, {0, 10}, {10, 0});
        REQUIRE(pts.size() == 1);
        REQUIRE(pts[0].x == Approx(5.0));
        REQUIRE(pts[0].y == Approx(5.0));
    }

    SECTION("parallel segments do not intersect") {
        REQUIRE(lcad::intersectSegmentSegment({0, 0}, {10, 0}, {0, 1}, {10, 1}).empty());
    }

    SECTION("segments whose infinite lines cross beyond their ends do not intersect") {
        REQUIRE(lcad::intersectSegmentSegment({0, 0}, {1, 1}, {5, 10}, {10, 5}).empty());
    }

    SECTION("secant segment hits a circle twice") {
        const auto pts = lcad::intersectSegmentCircle({-10, 0}, {10, 0}, {0, 0}, 5.0);
        REQUIRE(pts.size() == 2);
    }

    SECTION("tangent segment touches a circle once") {
        const auto pts = lcad::intersectSegmentCircle({-10, 5}, {10, 5}, {0, 0}, 5.0);
        REQUIRE(pts.size() == 1);
        REQUIRE(pts[0].y == Approx(5.0));
    }

    SECTION("two overlapping circles intersect twice") {
        const auto pts = lcad::intersectCircleCircle({0, 0}, 5.0, {6, 0}, 5.0);
        REQUIRE(pts.size() == 2);
        REQUIRE(pts[0].x == Approx(3.0));
    }

    SECTION("distant circles do not intersect") {
        REQUIRE(lcad::intersectCircleCircle({0, 0}, 2.0, {10, 0}, 2.0).empty());
    }
}

TEST_CASE("Entity intersections respect arc sweeps", "[geometry][intersect]") {
    // Vertical line through x=0 crosses the full circle twice but misses a
    // right-side arc (-60..60 degrees) entirely (its nearest points are at
    // x = 2.5).
    lcad::LineEntity line(1, 0, lcad::Point2D(0, -10), lcad::Point2D(0, 10));
    lcad::CircleEntity circle(2, 0, lcad::Point2D(0, 0), 5.0);
    lcad::ArcEntity rightArc(3, 0, lcad::Point2D(0, 0), 5.0, -M_PI / 3, M_PI / 3);

    REQUIRE(lcad::intersectEntities(line, circle).size() == 2);
    REQUIRE(lcad::intersectEntities(line, rightArc).empty());

    lcad::LineEntity horizontal(4, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    const auto pts = lcad::intersectEntities(horizontal, rightArc);
    REQUIRE(pts.size() == 1);
    REQUIRE(pts[0].x == Approx(5.0));
}

TEST_CASE("Infinite line intersections reach beyond segment ends", "[geometry][intersect]") {
    // The finite segment (0,0)-(1,0) does not reach the boundary at x=10, but
    // its infinite extension does.
    lcad::LineEntity boundary(1, 0, lcad::Point2D(10, -5), lcad::Point2D(10, 5));
    const auto pts = lcad::intersectInfiniteLineEntity(lcad::Point2D(0, 0), lcad::Point2D(1, 0), boundary);
    REQUIRE(pts.size() == 1);
    REQUIRE(pts[0].x == Approx(10.0));
    REQUIRE(pts[0].y == Approx(0.0).margin(1e-9));
}

TEST_CASE("DimensionEntity geometry", "[geometry][dimension]") {
    SECTION("aligned dimension measures point-to-point distance") {
        lcad::DimensionEntity dim(1, 0, lcad::Point2D(0, 0), lcad::Point2D(3, 4), lcad::Point2D(0, 2), true);
        const auto geo = dim.geometry();
        REQUIRE(geo.value == Approx(5.0));
        // The dimension line is parallel to p1-p2.
        const lcad::Point2D span = geo.dimB - geo.dimA;
        REQUIRE(span.length() == Approx(5.0));
    }

    SECTION("linear dimension dragged up measures the X delta") {
        lcad::DimensionEntity dim(1, 0, lcad::Point2D(1, 1), lcad::Point2D(7, 3), lcad::Point2D(4, 10), false);
        const auto geo = dim.geometry();
        REQUIRE(geo.value == Approx(6.0));
        REQUIRE(geo.dimA.y == Approx(10.0)); // dimension line sits at the drag height
        REQUIRE(geo.dimB.y == Approx(10.0));
    }

    SECTION("linear dimension dragged sideways measures the Y delta") {
        lcad::DimensionEntity dim(1, 0, lcad::Point2D(1, 1), lcad::Point2D(3, 9), lcad::Point2D(12, 5), false);
        const auto geo = dim.geometry();
        REQUIRE(geo.value == Approx(8.0));
        REQUIRE(geo.dimA.x == Approx(12.0));
    }

    SECTION("scale scales the measured value and text height together") {
        lcad::DimensionEntity dim(1, 0, lcad::Point2D(0, 0), lcad::Point2D(4, 0), lcad::Point2D(2, 3), true, 2.5);
        dim.scale(lcad::Point2D(0, 0), 2.0);
        REQUIRE(dim.geometry().value == Approx(8.0));
        REQUIRE(dim.textHeight() == Approx(5.0));
    }
}
