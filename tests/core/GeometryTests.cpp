#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/BoundaryTrace.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/PointCloud.h"
#include "core/geometry/Intersect.h"
#include "core/geometry/Line.h"
#include "core/geometry/MText.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/PolylineOps.h"
#include "core/geometry/Spline.h"
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

TEST_CASE("HatchEntity containment and distance", "[geometry][hatch]") {
    std::vector<lcad::Point2D> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    lcad::HatchEntity hatch(1, 0, square);

    REQUIRE(hatch.containsPoint(lcad::Point2D(5, 5)));
    REQUIRE_FALSE(hatch.containsPoint(lcad::Point2D(15, 5)));
    REQUIRE(hatch.distanceTo(lcad::Point2D(5, 5)) == Approx(0.0)); // interior picks count
    REQUIRE(hatch.distanceTo(lcad::Point2D(12, 5)) == Approx(2.0));

    const auto box = hatch.boundingBox();
    REQUIRE(box.max.x == Approx(10.0));
    REQUIRE(box.max.y == Approx(10.0));
}

TEST_CASE("ImageEntity hit-tests, rotates, and scales its rectangle", "[geometry][image]") {
    lcad::ImageEntity image(1, 0, "photo.png", lcad::Point2D(0, 0), 10.0, 5.0);

    REQUIRE(image.boundingBox().max.x == Approx(10.0));
    REQUIRE(image.boundingBox().max.y == Approx(5.0));
    REQUIRE(image.distanceTo(lcad::Point2D(5, 2)) == Approx(0.0)); // inside
    REQUIRE(image.distanceTo(lcad::Point2D(15, 2)) == Approx(5.0)); // 5 units right of the edge

    image.scale(lcad::Point2D(0, 0), 2.0);
    REQUIRE(image.width() == Approx(20.0));
    REQUIRE(image.height() == Approx(10.0));

    lcad::ImageEntity rotated(2, 0, "photo.png", lcad::Point2D(0, 0), 10.0, 5.0);
    rotated.rotate(lcad::Point2D(0, 0), M_PI / 2);
    // Rotated 90 degrees CCW about its own corner: the rectangle now
    // extends up the +Y axis instead of along +X.
    REQUIRE(rotated.distanceTo(lcad::Point2D(-2, 2)) == Approx(0.0));
}

TEST_CASE("PointCloudEntity hit-tests by bounding box and moves as one via its single grip", "[geometry][pointcloud]") {
    std::vector<lcad::Point2D> pts{{0, 0}, {10, 0}, {5, 5}};
    lcad::PointCloudEntity cloud(1, 0, "scan.xyz", pts);

    REQUIRE(cloud.boundingBox().min.x == Approx(0.0));
    REQUIRE(cloud.boundingBox().max.x == Approx(10.0));
    REQUIRE(cloud.distanceTo(lcad::Point2D(5, 2)) == Approx(0.0)); // inside the bbox
    REQUIRE(cloud.distanceTo(lcad::Point2D(20, 0)) == Approx(10.0));

    REQUIRE(cloud.gripPoints().size() == 1);
    const lcad::Point2D grip = cloud.gripPoints().front();
    cloud.moveGripPoint(0, grip + lcad::Point2D(3, 4));
    REQUIRE(cloud.points()[0].x == Approx(3.0));
    REQUIRE(cloud.points()[0].y == Approx(4.0));
    REQUIRE(cloud.points()[1].x == Approx(13.0));
}

TEST_CASE("InsertEntity transforms its block's children", "[geometry][block]") {
    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> children;
    children.push_back(
        std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0), lcad::Point2D(1, 0)));
    const lcad::BlockDefinition* block = doc.addBlock("unit-line", std::move(children));

    // Placed at (10, 5), doubled, rotated 90 degrees CCW: the unit X line
    // becomes a length-2 line pointing up from the insertion point.
    lcad::InsertEntity insert(doc.reserveEntityId(), 0, block, lcad::Point2D(10, 5), 2.0, M_PI / 2);

    const auto instances = insert.instantiate();
    REQUIRE(instances.size() == 1);
    const auto* line = static_cast<const lcad::LineEntity*>(instances[0].get());
    REQUIRE(line->start().x == Approx(10.0));
    REQUIRE(line->start().y == Approx(5.0));
    REQUIRE(line->end().x == Approx(10.0).margin(1e-9));
    REQUIRE(line->end().y == Approx(7.0));

    const auto box = insert.boundingBox();
    REQUIRE(box.max.y == Approx(7.0));
    REQUIRE(insert.distanceTo(lcad::Point2D(10, 6)) == Approx(0.0).margin(1e-9));

    // Uniform scale about a point scales the placement too.
    insert.scale(lcad::Point2D(10, 5), 0.5);
    REQUIRE(insert.scaleFactor() == Approx(1.0));
    REQUIRE(insert.boundingBox().max.y == Approx(6.0));
}

TEST_CASE("InsertEntity dynamic linear parameter stretches per-instance", "[geometry][block][dynamic]") {
    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> children;
    children.push_back(
        std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0)));
    doc.addBlock("bar", std::move(children));
    lcad::BlockDefinition* block = doc.findBlock("bar");

    lcad::DynamicLinearParameter dp;
    dp.basePoint = lcad::Point2D(0, 0);
    dp.endPoint = lcad::Point2D(10, 0);
    dp.frameMin = lcad::Point2D(8, -1);
    dp.frameMax = lcad::Point2D(12, 1);
    block->dynamicParam = dp;
    REQUIRE(block->isDynamic());

    // Default stretch (0): unchanged from the plain-block case.
    lcad::InsertEntity plain(doc.reserveEntityId(), 0, block, lcad::Point2D(0, 0));
    auto plainInstances = plain.instantiate();
    REQUIRE(plainInstances.size() == 1);
    const auto* plainLine = static_cast<const lcad::LineEntity*>(plainInstances[0].get());
    REQUIRE(plainLine->end().x == Approx(10.0));

    // Two independent instances of the same dynamic block, stretched differently.
    lcad::InsertEntity stretched(doc.reserveEntityId(), 0, block, lcad::Point2D(0, 0));
    stretched.setDynamicStretch(5.0);
    const auto stretchedInstances = stretched.instantiate();
    const auto* longLine = static_cast<const lcad::LineEntity*>(stretchedInstances[0].get());
    REQUIRE(longLine->start().x == Approx(0.0).margin(1e-9)); // base point untouched: outside the frame
    REQUIRE(longLine->end().x == Approx(15.0));

    lcad::InsertEntity shrunk(doc.reserveEntityId(), 0, block, lcad::Point2D(0, 0));
    shrunk.setDynamicStretch(-3.0);
    const auto shrunkInstances = shrunk.instantiate();
    const auto* shortLine = static_cast<const lcad::LineEntity*>(shrunkInstances[0].get());
    REQUIRE(shortLine->end().x == Approx(7.0));

    // The original (unstretched) instance is unaffected by the others.
    REQUIRE(static_cast<const lcad::LineEntity*>(plain.instantiate()[0].get())->end().x == Approx(10.0));

    // Grip round trip: the dynamic grip sits at the parameter's endpoint by
    // default, and dragging it re-derives the stretch distance.
    REQUIRE(plain.gripPoints().size() == 2);
    REQUIRE(plain.gripPoints()[1].x == Approx(10.0));
    plain.moveGripPoint(1, lcad::Point2D(14, 0));
    REQUIRE(plain.dynamicStretch() == Approx(4.0));
}

TEST_CASE("bulgeToArc matches the DXF bulge convention", "[geometry][bulge]") {
    // Values cross-checked against ezdxf's bulge_to_arc.
    const lcad::Point2D a(0, 0);
    const lcad::Point2D b(2, 0);

    SECTION("bulge 1 is a semicircle, CCW from start (dipping below the chord)") {
        const auto arc = lcad::bulgeToArc(a, b, 1.0);
        REQUIRE(arc.has_value());
        REQUIRE(arc->center.x == Approx(1.0));
        REQUIRE(arc->center.y == Approx(0.0).margin(1e-9));
        REQUIRE(arc->radius == Approx(1.0));
        REQUIRE(arc->sweep == Approx(M_PI));
    }
    SECTION("minor arc, bulge 0.5") {
        const auto arc = lcad::bulgeToArc(a, b, 0.5);
        REQUIRE(arc.has_value());
        REQUIRE(arc->center.x == Approx(1.0));
        REQUIRE(arc->center.y == Approx(0.75));
        REQUIRE(arc->radius == Approx(1.25));
        REQUIRE(arc->startAngle == Approx(-2.4981).margin(1e-4));
        REQUIRE(arc->startAngle + arc->sweep == Approx(-0.6435).margin(1e-4));
    }
    SECTION("negative bulge mirrors across the chord and sweeps clockwise") {
        const auto arc = lcad::bulgeToArc(a, b, -0.5);
        REQUIRE(arc.has_value());
        REQUIRE(arc->center.y == Approx(-0.75));
        REQUIRE(arc->sweep < 0);
    }
    SECTION("major arc, bulge 2") {
        const auto arc = lcad::bulgeToArc(a, b, 2.0);
        REQUIRE(arc.has_value());
        REQUIRE(arc->center.y == Approx(-0.75));
        REQUIRE(arc->radius == Approx(1.25));
        REQUIRE(std::abs(arc->sweep) > M_PI);
    }
    SECTION("zero bulge and degenerate chords are straight") {
        REQUIRE_FALSE(lcad::bulgeToArc(a, b, 0.0).has_value());
        REQUIRE_FALSE(lcad::bulgeToArc(a, a, 1.0).has_value());
    }
}

TEST_CASE("PolylineEntity with bulges: distance, bbox, mirror, flatten", "[geometry][bulge]") {
    // One semicircular segment from (0,0) to (2,0), bulging down through (1,-1).
    std::vector<lcad::Point2D> verts{{0, 0}, {2, 0}};
    std::vector<double> bulges{1.0, 0.0};
    lcad::PolylineEntity pl(1, 0, verts, bulges, false);

    REQUIRE(pl.hasArcs());
    REQUIRE(pl.distanceTo(lcad::Point2D(1, -1)) == Approx(0.0).margin(1e-9)); // on the arc
    REQUIRE(pl.distanceTo(lcad::Point2D(1, 0)) == Approx(1.0));               // at the center
    REQUIRE(pl.distanceTo(lcad::Point2D(1, 1)) == Approx(std::sqrt(2.0)));    // outside the sweep -> endpoint

    const auto box = pl.boundingBox();
    REQUIRE(box.min.y == Approx(-1.0)); // arc extreme, below both vertices
    REQUIRE(box.max.y == Approx(0.0).margin(1e-9));

    // Flattening approximates the arc closely.
    const auto flat = pl.flattenedVertices();
    REQUIRE(flat.size() > 4);
    for (const auto& p : flat) {
        REQUIRE(p.distanceTo(lcad::Point2D(1, 0)) == Approx(1.0).margin(1e-6));
    }

    // Mirroring reverses arc orientation: the bulge flips sign.
    pl.mirror(lcad::Point2D(0, 0), lcad::Point2D(1, 0)); // mirror across the X axis
    REQUIRE(pl.bulgeAt(0) == Approx(-1.0));
    REQUIRE(pl.distanceTo(lcad::Point2D(1, 1)) == Approx(0.0).margin(1e-9)); // arc now bulges up
}

TEST_CASE("Bulged polyline segments intersect as true arcs", "[geometry][bulge]") {
    // Semicircle from (0,0) to (2,0) through (1,-1), crossed by a vertical line at x=1.
    std::vector<lcad::Point2D> verts{{0, 0}, {2, 0}};
    std::vector<double> bulges{1.0, 0.0};
    lcad::PolylineEntity pl(1, 0, verts, bulges, false);
    lcad::LineEntity cutter(2, 0, lcad::Point2D(1, -5), lcad::Point2D(1, 5));

    const auto pts = lcad::intersectEntities(pl, cutter);
    REQUIRE(pts.size() == 1);
    REQUIRE(pts[0].x == Approx(1.0));
    REQUIRE(pts[0].y == Approx(-1.0));
}

TEST_CASE("Spline interpolation passes through its fit points", "[geometry][spline]") {
    std::vector<lcad::Point2D> fit{{0, 0}, {10, 8}, {20, -3}, {30, 5}, {40, 0}};
    auto spline = lcad::SplineEntity::fromFitPoints(1, 0, fit);
    REQUIRE(spline != nullptr);
    REQUIRE(spline->degree() == 3);
    REQUIRE(spline->controlPoints().size() == fit.size());

    // The interpolating curve must pass through every fit point.
    for (const auto& q : fit) {
        REQUIRE(spline->distanceTo(q) == Approx(0.0).margin(1e-2));
    }
    // Curve endpoints coincide with the first/last fit points exactly.
    const auto pts = spline->sample(16);
    REQUIRE(pts.front().distanceTo(fit.front()) == Approx(0.0).margin(1e-9));
    REQUIRE(pts.back().distanceTo(fit.back()) == Approx(0.0).margin(1e-9));

    // Two points degenerate to a straight (degree-1) spline.
    auto straight = lcad::SplineEntity::fromFitPoints(2, 0, {{0, 0}, {10, 0}});
    REQUIRE(straight != nullptr);
    REQUIRE(straight->distanceTo(lcad::Point2D(5, 0)) == Approx(0.0).margin(1e-9));

    // Degenerate input refuses politely.
    REQUIRE(lcad::SplineEntity::fromFitPoints(3, 0, {{1, 1}}) == nullptr);
    REQUIRE(lcad::SplineEntity::fromFitPoints(4, 0, {{1, 1}, {1, 1}}) == nullptr);
}

TEST_CASE("Spline transforms move fit and control points together", "[geometry][spline]") {
    std::vector<lcad::Point2D> fit{{0, 0}, {5, 5}, {10, 0}};
    auto spline = lcad::SplineEntity::fromFitPoints(1, 0, fit);
    REQUIRE(spline != nullptr);

    spline->translate(lcad::Point2D(100, 0));
    REQUIRE(spline->distanceTo(lcad::Point2D(105, 5)) == Approx(0.0).margin(1e-2));

    // Grip-editing a fit point re-fits the curve through the new point.
    spline->moveGripPoint(1, lcad::Point2D(105, 10));
    REQUIRE(spline->distanceTo(lcad::Point2D(105, 10)) == Approx(0.0).margin(1e-2));
}

TEST_CASE("Hatch pattern lines are clipped to the boundary", "[geometry][hatch]") {
    // Unit square 10x10, ANSI31 (45-degree lines every 0.125), scale 8 ->
    // spacing sqrt(2) along the normal, lines cross the square diagonally.
    std::vector<lcad::Point2D> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    lcad::HatchEntity hatch(1, 0, square, lcad::HatchPattern::Ansi31, 8.0, 0.0);

    const auto segs = hatch.patternSegments();
    REQUIRE_FALSE(segs.empty());
    for (const auto& [a, b] : segs) {
        // Endpoints on/inside the square...
        REQUIRE(a.x >= -1e-6); REQUIRE(a.x <= 10 + 1e-6);
        REQUIRE(a.y >= -1e-6); REQUIRE(a.y <= 10 + 1e-6);
        REQUIRE(b.x >= -1e-6); REQUIRE(b.x <= 10 + 1e-6);
        // ...and every segment runs at 45 degrees.
        const lcad::Point2D d = b - a;
        REQUIRE(d.x == Approx(d.y).margin(1e-6));
        // Midpoints are inside the region.
        REQUIRE(hatch.containsPoint(a + d * 0.5));
    }

    // Solid hatches produce no pattern line work.
    lcad::HatchEntity solid(2, 0, square);
    REQUIRE(solid.patternSegments().empty());

    // Rotating the hatch rotates its pattern.
    hatch.rotate(lcad::Point2D(5, 5), M_PI / 4);
    const auto rotated = hatch.patternSegments();
    REQUIRE_FALSE(rotated.empty());
    const lcad::Point2D d0 = rotated[0].second - rotated[0].first;
    REQUIRE(std::abs(d0.x) == Approx(0.0).margin(1e-6)); // 45 + 45 = vertical
}

TEST_CASE("MText wraps, measures, and decodes content codes", "[geometry][mtext]") {
    // 0.6 * height(2) = 1.2 per char; width 24 -> 20 chars per line.
    lcad::MTextEntity mtext(1, 0, lcad::Point2D(0, 0), "hello world this is a long line", 2.0, 24.0);
    const auto lines = mtext.wrappedLines();
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "hello world this is");
    REQUIRE(lines[1] == "a long line");
    REQUIRE(mtext.blockHeight() == Approx(2 * 1.6 * 2.0));

    // The block extends down from the top-left anchor.
    const auto box = mtext.boundingBox();
    REQUIRE(box.max.y == Approx(0.0).margin(1e-9));
    REQUIRE(box.min.y == Approx(-6.4));
    REQUIRE(mtext.distanceTo(lcad::Point2D(5, -3)) == Approx(0.0).margin(1e-9));

    // Explicit newlines always break.
    lcad::MTextEntity plain(2, 0, lcad::Point2D(0, 0), "a\nb", 2.0, 0.0);
    REQUIRE(plain.wrappedLines().size() == 2);

    // DXF/DWG content decoding.
    REQUIRE(lcad::decodeMTextContent("first\\Psecond") == "first\nsecond");
    REQUIRE(lcad::decodeMTextContent("{\\fArial|b0|i0;styled} plain") == "styled plain");
    REQUIRE(lcad::decodeMTextContent("\\H2.5;big\\~gap") == "big gap");
    REQUIRE(lcad::decodeMTextContent("\\S1^2;") == "1/2");
    REQUIRE(lcad::encodeMTextContent("a\nb\\c") == "a\\Pb\\\\c");
}

TEST_CASE("Radial, diameter, and angular dimension geometry", "[geometry][dimension]") {
    SECTION("radius: single arrow, R-prefixed label along the pick ray") {
        lcad::DimensionEntity dim(1, 0, lcad::DimensionKind::Radius, lcad::Point2D(0, 0), lcad::Point2D(5, 0),
                                  lcad::Point2D(3, 0));
        const auto geo = dim.geometry();
        REQUIRE(geo.value == Approx(5.0));
        REQUIRE(geo.label == "R5.00");
        REQUIRE_FALSE(geo.arrow1);
        REQUIRE(geo.arrow2);
        REQUIRE(geo.dimA.x == Approx(0.0));
        REQUIRE(geo.dimB.x == Approx(5.0));
    }
    SECTION("diameter spans the full chord") {
        lcad::DimensionEntity dim(1, 0, lcad::DimensionKind::Diameter, lcad::Point2D(10, 10),
                                  lcad::Point2D(15, 10), lcad::Point2D(12, 10));
        const auto geo = dim.geometry();
        REQUIRE(geo.value == Approx(10.0));
        REQUIRE(geo.dimA.x == Approx(5.0));
        REQUIRE(geo.dimB.x == Approx(15.0));
        REQUIRE(geo.arrow1);
        REQUIRE(geo.arrow2);
    }
    SECTION("angular measures the sweep containing the arc pick") {
        // Rays along +X and +Y from the origin; arc picked inside the 90-degree side.
        lcad::DimensionEntity dim(1, 0, lcad::DimensionKind::Angular, lcad::Point2D(10, 0), lcad::Point2D(0, 10),
                                  lcad::Point2D(4, 4), lcad::Point2D(0, 0));
        const auto geo = dim.geometry();
        REQUIRE(geo.angular);
        REQUIRE(geo.value == Approx(90.0));
        REQUIRE(geo.arcRadius == Approx(std::sqrt(32.0)));
        // Picking the other side measures the 270-degree reflex angle.
        lcad::DimensionEntity reflex(2, 0, lcad::DimensionKind::Angular, lcad::Point2D(10, 0),
                                     lcad::Point2D(0, 10), lcad::Point2D(-4, -4), lcad::Point2D(0, 0));
        REQUIRE(reflex.geometry().value == Approx(270.0));
    }
    SECTION("style decimals shape the label") {
        lcad::DimensionEntity dim(1, 0, lcad::Point2D(0, 0), lcad::Point2D(7, 0), lcad::Point2D(3, 5), false);
        dim.setStyle(3.0, 1.5, 3);
        REQUIRE(dim.geometry().label == "7.000");
        REQUIRE(dim.arrowSize() == Approx(1.5));
    }
}

TEST_CASE("offsetPolyline makes a parallel copy with preserved bulges", "[geometry][polyline][offset]") {
    // L-shape: (0,0) -> (10,0) -> (10,10), offset 1 to the upper-left side.
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    lcad::PolylineEntity pl(1, 0, verts, false);

    auto inside = lcad::offsetPolyline(pl, 2, 1.0, lcad::Point2D(5, 5));
    REQUIRE(inside != nullptr);
    const auto& v = inside->vertices();
    REQUIRE(v.size() == 3);
    REQUIRE(v[0].x == Approx(0.0));
    REQUIRE(v[0].y == Approx(1.0)); // parallel to the horizontal leg
    REQUIRE(v[1].x == Approx(9.0)); // miter corner
    REQUIRE(v[1].y == Approx(1.0));
    REQUIRE(v[2].x == Approx(9.0)); // parallel to the vertical leg
    REQUIRE(v[2].y == Approx(10.0));

    // Other side.
    auto outside = lcad::offsetPolyline(pl, 3, 1.0, lcad::Point2D(5, -5));
    REQUIRE(outside != nullptr);
    REQUIRE(outside->vertices()[0].y == Approx(-1.0));
    REQUIRE(outside->vertices()[1].x == Approx(11.0));

    // Bulges survive: a semicircular segment keeps its included angle.
    std::vector<lcad::Point2D> arcVerts{{0, 0}, {10, 0}};
    std::vector<double> arcBulges{1.0, 0.0};
    lcad::PolylineEntity arcPl(4, 0, arcVerts, arcBulges, false);
    auto arcOffset = lcad::offsetPolyline(arcPl, 5, 1.0, lcad::Point2D(5, -10));
    REQUIRE(arcOffset != nullptr);
    REQUIRE(arcOffset->bulgeAt(0) == Approx(1.0));
    // Offsetting away from the center grows the radius: the new chord spans wider.
    REQUIRE(arcOffset->vertices()[1].x - arcOffset->vertices()[0].x == Approx(12.0));

    // An inward offset larger than the arc radius refuses.
    REQUIRE(lcad::offsetPolyline(arcPl, 6, 6.0, lcad::Point2D(5, -2)) == nullptr);
}

TEST_CASE("joinToPolyline chains lines and arcs into one polyline", "[geometry][polyline][join]") {
    lcad::LineEntity l1(1, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0));
    // Semicircle from (10,0) up to (10,10): center (10,5), CCW from -90 to +90.
    lcad::ArcEntity arc(2, 0, lcad::Point2D(10, 5), 5.0, -M_PI / 2, M_PI / 2);
    lcad::LineEntity l2(3, 0, lcad::Point2D(10, 10), lcad::Point2D(0, 10));

    auto joined = lcad::joinToPolyline(10, 0, {&l1, &arc, &l2});
    REQUIRE(joined != nullptr);
    REQUIRE(joined->vertices().size() == 4);
    REQUIRE_FALSE(joined->closed());
    REQUIRE(joined->hasArcs());
    REQUIRE(joined->bulgeAt(1) == Approx(1.0)); // the semicircle, CCW

    // A closing fourth segment produces a closed polyline.
    lcad::LineEntity l3(4, 0, lcad::Point2D(0, 10), lcad::Point2D(0, 0));
    auto closed = lcad::joinToPolyline(11, 0, {&l1, &arc, &l2, &l3});
    REQUIRE(closed != nullptr);
    REQUIRE(closed->closed());
    REQUIRE(closed->vertices().size() == 4);

    // Disjoint pieces refuse.
    lcad::LineEntity far(5, 0, lcad::Point2D(100, 100), lcad::Point2D(110, 100));
    REQUIRE(lcad::joinToPolyline(12, 0, {&l1, &far}) == nullptr);
}

TEST_CASE("traceBoundary finds the enclosing loop from disjoint segments", "[geometry][boundary]") {
    using Seg = std::pair<lcad::Point2D, lcad::Point2D>;

    // A plain square, one segment per side (as HATCH's pick-point would see
    // four independent LINE entities, not a pre-closed polyline).
    std::vector<Seg> square{{{0, 0}, {10, 0}}, {{10, 0}, {10, 10}}, {{10, 10}, {0, 10}}, {{0, 10}, {0, 0}}};

    auto loop = lcad::traceBoundary(square, lcad::Point2D(5, 5));
    REQUIRE(loop.has_value());
    REQUIRE(loop->size() == 4);

    // Outside the square: nothing to enclose it.
    REQUIRE_FALSE(lcad::traceBoundary(square, lcad::Point2D(20, 20)).has_value());

    // Open geometry (missing the top side): no enclosed face at all.
    std::vector<Seg> openSquare{{{0, 0}, {10, 0}}, {{10, 0}, {10, 10}}, {{0, 10}, {0, 0}}};
    REQUIRE_FALSE(lcad::traceBoundary(openSquare, lcad::Point2D(5, 5)).has_value());

    // A diagonal splits the square into two triangles; picking one side
    // should trace just that triangle, not the whole square.
    std::vector<Seg> split = square;
    split.push_back({{0, 0}, {10, 10}});
    auto lower = lcad::traceBoundary(split, lcad::Point2D(7, 2));
    REQUIRE(lower.has_value());
    REQUIRE(lower->size() == 3);
    double area = 0.0;
    for (std::size_t i = 0, j = lower->size() - 1; i < lower->size(); j = i++) {
        area += (*lower)[j].x * (*lower)[i].y - (*lower)[i].x * (*lower)[j].y;
    }
    REQUIRE(std::abs(area) / 2.0 == Approx(50.0));
}
