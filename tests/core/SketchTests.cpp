#include "core/sketch/ConstraintSolver.h"
#include "core/sketch/LinearSolve.h"
#include "core/sketch/SketchGeometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>

using namespace lcad;
using Catch::Approx;

TEST_CASE("solveLinearSystem solves a simple dense system", "[sketch][linearsolve]") {
    // 2x + y = 5, x - y = 1  =>  x=2, y=1
    std::vector<std::vector<double>> a = {{2, 1}, {1, -1}};
    std::vector<double> b = {5, 1};
    std::vector<double> x;
    REQUIRE(solveLinearSystem(a, b, x));
    REQUIRE(x[0] == Approx(2.0));
    REQUIRE(x[1] == Approx(1.0));
}

TEST_CASE("solveLinearSystem reports failure for a singular system", "[sketch][linearsolve]") {
    std::vector<std::vector<double>> a = {{1, 2}, {2, 4}}; // row2 = 2*row1, singular
    std::vector<double> b = {1, 2};
    std::vector<double> x;
    REQUIRE_FALSE(solveLinearSystem(a, b, x));
}

TEST_CASE("solveSketch converges a fully-constrained rectangle to its exact expected corners",
         "[sketch][solver]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true); // fixed anchor
    const int p1 = sketch.addPoint(Point2D(18, 2));      // near (20,0)
    const int p2 = sketch.addPoint(Point2D(19, 9));      // near (20,10)
    const int p3 = sketch.addPoint(Point2D(1, 11));      // near (0,10)

    const int l0 = sketch.addLine(p0, p1); // bottom
    const int l1 = sketch.addLine(p1, p2); // right
    const int l2 = sketch.addLine(p2, p3); // top
    const int l3 = sketch.addLine(p3, p0); // left

    sketch.addConstraint({SketchConstraintType::Horizontal, l0});
    sketch.addConstraint({SketchConstraintType::Vertical, l1});
    sketch.addConstraint({SketchConstraintType::Horizontal, l2});
    sketch.addConstraint({SketchConstraintType::Vertical, l3});
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p0, p1, 20.0});
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p1, p2, 10.0});

    const SolveResult result = solveSketch(sketch);
    REQUIRE(result.converged);

    REQUIRE(sketch.points()[static_cast<std::size_t>(p0)].x == Approx(0.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p0)].y == Approx(0.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p1)].x == Approx(20.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p1)].y == Approx(0.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p2)].x == Approx(20.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p2)].y == Approx(10.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p3)].x == Approx(0.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p3)].y == Approx(10.0).margin(1e-6));
}

TEST_CASE("solveSketch re-solves a dimension-driven rectangle when its Distance value changes",
         "[sketch][solver]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true);
    const int p1 = sketch.addPoint(Point2D(9, 1));
    const int p2 = sketch.addPoint(Point2D(11, 6));
    const int p3 = sketch.addPoint(Point2D(1, 5));
    const int l0 = sketch.addLine(p0, p1);
    const int l1 = sketch.addLine(p1, p2);
    const int l2 = sketch.addLine(p2, p3);
    const int l3 = sketch.addLine(p3, p0);
    sketch.addConstraint({SketchConstraintType::Horizontal, l0});
    sketch.addConstraint({SketchConstraintType::Vertical, l1});
    sketch.addConstraint({SketchConstraintType::Horizontal, l2});
    sketch.addConstraint({SketchConstraintType::Vertical, l3});
    const int widthIdx = static_cast<int>(sketch.constraints().size());
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p0, p1, 10.0});
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p1, p2, 5.0});

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.points()[static_cast<std::size_t>(p1)].x == Approx(10.0).margin(1e-6));

    // Change the width dimension, like editing it in a real sketcher, and re-solve the same sketch in place.
    sketch.constraints()[static_cast<std::size_t>(widthIdx)].value = 30.0;
    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.points()[static_cast<std::size_t>(p1)].x == Approx(30.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p2)].x == Approx(30.0).margin(1e-6)); // right side follows too
}

TEST_CASE("solveSketch satisfies Parallel and Perpendicular constraints", "[sketch][solver]") {
    Sketch sketch;
    const int a1 = sketch.addPoint(Point2D(0, 0), true);
    const int a2 = sketch.addPoint(Point2D(10, 0), true); // line A fixed, horizontal
    const int b1 = sketch.addPoint(Point2D(0, 5));
    const int b2 = sketch.addPoint(Point2D(8, 9)); // not parallel to A yet
    const int lineA = sketch.addLine(a1, a2);
    const int lineB = sketch.addLine(b1, b2);
    sketch.addConstraint({SketchConstraintType::Parallel, lineA, lineB});

    REQUIRE(solveSketch(sketch).converged);
    const Point2D dirA = sketch.points()[static_cast<std::size_t>(a2)] - sketch.points()[static_cast<std::size_t>(a1)];
    const Point2D dirB = sketch.points()[static_cast<std::size_t>(b2)] - sketch.points()[static_cast<std::size_t>(b1)];
    const double cross = dirA.x * dirB.y - dirA.y * dirB.x;
    REQUIRE(cross == Approx(0.0).margin(1e-6));

    Sketch perp;
    const int c1 = perp.addPoint(Point2D(0, 0), true);
    const int c2 = perp.addPoint(Point2D(10, 0), true);
    const int d1 = perp.addPoint(Point2D(0, 5));
    const int d2 = perp.addPoint(Point2D(8, 9));
    const int lineC = perp.addLine(c1, c2);
    const int lineD = perp.addLine(d1, d2);
    perp.addConstraint({SketchConstraintType::Perpendicular, lineC, lineD});

    REQUIRE(solveSketch(perp).converged);
    const Point2D dirC = perp.points()[static_cast<std::size_t>(c2)] - perp.points()[static_cast<std::size_t>(c1)];
    const Point2D dirD = perp.points()[static_cast<std::size_t>(d2)] - perp.points()[static_cast<std::size_t>(d1)];
    const double dot = dirC.x * dirD.x + dirC.y * dirD.y;
    REQUIRE(dot == Approx(0.0).margin(1e-6));
}

TEST_CASE("solveSketch satisfies DistanceX and DistanceY independently of the diagonal distance",
         "[sketch][solver]") {
    Sketch sketch;
    const int a = sketch.addPoint(Point2D(0, 0), true);
    const int b = sketch.addPoint(Point2D(3, 1)); // free, starts far from the target offsets
    sketch.addConstraint({SketchConstraintType::DistanceX, -1, -1, a, b, 12.0});
    sketch.addConstraint({SketchConstraintType::DistanceY, -1, -1, a, b, 5.0});

    REQUIRE(solveSketch(sketch).converged);
    const Point2D pa = sketch.points()[static_cast<std::size_t>(a)];
    const Point2D pb = sketch.points()[static_cast<std::size_t>(b)];
    REQUIRE((pb.x - pa.x) == Approx(12.0).margin(1e-6));
    REQUIRE((pb.y - pa.y) == Approx(5.0).margin(1e-6));
}

TEST_CASE("solveSketch dimensions a circle's diameter directly", "[sketch][solver]") {
    Sketch sketch;
    const int center = sketch.addPoint(Point2D(5, 5), true);
    const int circle = sketch.addCircle(center, 2.0); // starts at radius 2
    sketch.addConstraint({SketchConstraintType::Diameter, circle, -1, -1, -1, 15.0});

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.circles()[static_cast<std::size_t>(circle)].radius == Approx(7.5).margin(1e-6));
}

TEST_CASE("solveSketch satisfies an Equal-length constraint between two lines", "[sketch][solver]") {
    Sketch sketch;
    const int a1 = sketch.addPoint(Point2D(0, 0), true);
    const int a2 = sketch.addPoint(Point2D(10, 0), true); // fixed, length 10
    const int b1 = sketch.addPoint(Point2D(0, 5), true);
    const int b2 = sketch.addPoint(Point2D(3, 5)); // free end, starts length 3
    const int lineA = sketch.addLine(a1, a2);
    const int lineB = sketch.addLine(b1, b2);
    sketch.addConstraint({SketchConstraintType::Equal, lineA, lineB});

    REQUIRE(solveSketch(sketch).converged);
    const double lenA = sketch.points()[static_cast<std::size_t>(a1)].distanceTo(sketch.points()[static_cast<std::size_t>(a2)]);
    const double lenB = sketch.points()[static_cast<std::size_t>(b1)].distanceTo(sketch.points()[static_cast<std::size_t>(b2)]);
    REQUIRE(lenB == Approx(lenA).margin(1e-6));
}

TEST_CASE("solveSketch satisfies EqualCircleRadius between two circles", "[sketch][solver]") {
    Sketch sketch;
    const int centerA = sketch.addPoint(Point2D(0, 0), true);
    const int centerB = sketch.addPoint(Point2D(20, 0), true);
    const int circleA = sketch.addCircle(centerA, 5.0);
    const int circleB = sketch.addCircle(centerB, 2.0); // starts unequal
    sketch.addConstraint({SketchConstraintType::EqualCircleRadius, circleA, circleB});

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.circles()[static_cast<std::size_t>(circleA)].radius ==
            Approx(sketch.circles()[static_cast<std::size_t>(circleB)].radius).margin(1e-6));
}

TEST_CASE("solveSketch satisfies EqualArcRadius between two arcs", "[sketch][solver][arc]") {
    Sketch sketch;
    const int centerA = sketch.addPoint(Point2D(0, 0), true);
    const int startA = sketch.addPoint(Point2D(5, 0), true);
    const int endA = sketch.addPoint(Point2D(0, 5), true);
    const int arcA = sketch.addArc(centerA, startA, endA, 5.0);

    const int centerB = sketch.addPoint(Point2D(20, 0), true);
    const int startB = sketch.addPoint(Point2D(22, 0)); // radius 2 initially, free to move
    const int endB = sketch.addPoint(Point2D(20, 2));
    const int arcB = sketch.addArc(centerB, startB, endB, 2.0);

    sketch.addConstraint({SketchConstraintType::EqualArcRadius, arcA, arcB});

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.arcs()[static_cast<std::size_t>(arcB)].radius == Approx(5.0).margin(1e-6));
}

TEST_CASE("solveSketch satisfies a Tangent constraint between a line and a circle", "[sketch][solver]") {
    Sketch sketch;
    const int p1 = sketch.addPoint(Point2D(0, 0), true);
    const int p2 = sketch.addPoint(Point2D(20, 0), true); // fixed horizontal line along y=0
    const int center = sketch.addPoint(Point2D(10, 4));   // circle center, not yet tangent
    const int line = sketch.addLine(p1, p2);
    const int circle = sketch.addCircle(center, 3.0);
    sketch.addConstraint({SketchConstraintType::Tangent, line, circle});

    REQUIRE(solveSketch(sketch).converged);
    // Tangent means the perpendicular distance from center to the line equals the radius.
    const Point2D c = sketch.points()[static_cast<std::size_t>(center)];
    const double radius = sketch.circles()[static_cast<std::size_t>(circle)].radius;
    REQUIRE(std::abs(c.y) == Approx(radius).margin(1e-6));
}

TEST_CASE("solveSketch dimensions a circle's radius directly", "[sketch][solver]") {
    Sketch sketch;
    const int center = sketch.addPoint(Point2D(5, 5), true);
    const int circle = sketch.addCircle(center, 2.0); // starts at radius 2
    sketch.addConstraint({SketchConstraintType::Radius, circle, -1, -1, -1, 7.5});

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.circles()[static_cast<std::size_t>(circle)].radius == Approx(7.5).margin(1e-6));
}

TEST_CASE("solveSketch enforces an arc's implicit radius consistency even with no explicit constraints",
         "[sketch][solver][arc]") {
    Sketch sketch;
    const int center = sketch.addPoint(Point2D(0, 0), true);
    const int start = sketch.addPoint(Point2D(6, 0)); // should settle at distance == radius (5) from center
    const int end = sketch.addPoint(Point2D(0, 4));   // should settle at distance == radius (5) from center
    sketch.addArc(center, start, end, 5.0);

    REQUIRE(solveSketch(sketch).converged);
    const Point2D c = sketch.points()[static_cast<std::size_t>(center)];
    REQUIRE(sketch.points()[static_cast<std::size_t>(start)].distanceTo(c) == Approx(5.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(end)].distanceTo(c) == Approx(5.0).margin(1e-6));
}

TEST_CASE("solveSketch dimensions an arc's radius, moving its free start/end to match", "[sketch][solver][arc]") {
    Sketch sketch;
    const int center = sketch.addPoint(Point2D(0, 0), true);
    const int start = sketch.addPoint(Point2D(5, 0)); // radius 5 initially
    const int end = sketch.addPoint(Point2D(0, 5));
    const int arc = sketch.addArc(center, start, end, 5.0);
    sketch.addConstraint({SketchConstraintType::ArcRadius, arc, -1, -1, -1, 8.0});

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.arcs()[static_cast<std::size_t>(arc)].radius == Approx(8.0).margin(1e-6));
    const Point2D c = sketch.points()[static_cast<std::size_t>(center)];
    REQUIRE(sketch.points()[static_cast<std::size_t>(start)].distanceTo(c) == Approx(8.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(end)].distanceTo(c) == Approx(8.0).margin(1e-6));
}

TEST_CASE("solveSketch satisfies external circle-circle tangency", "[sketch][solver]") {
    Sketch sketch;
    const int centerA = sketch.addPoint(Point2D(0, 0), true);
    const int centerB = sketch.addPoint(Point2D(10, 3)); // not yet tangent
    const int circleA = sketch.addCircle(centerA, 4.0);
    const int circleB = sketch.addCircle(centerB, 3.0);
    sketch.addConstraint({SketchConstraintType::TangentCircleCircle, circleA, circleB});

    REQUIRE(solveSketch(sketch).converged);
    const double dist =
        sketch.points()[static_cast<std::size_t>(centerA)].distanceTo(sketch.points()[static_cast<std::size_t>(centerB)]);
    const double radiusA = sketch.circles()[static_cast<std::size_t>(circleA)].radius;
    const double radiusB = sketch.circles()[static_cast<std::size_t>(circleB)].radius;
    REQUIRE(dist == Approx(radiusA + radiusB).margin(1e-6));
}

TEST_CASE("analyzeDof reports remaining freedom for an under-constrained sketch", "[sketch][dof]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true); // fixed: 0 DOF
    const int p1 = sketch.addPoint(Point2D(10, 0));       // free: 2 DOF
    sketch.addLine(p0, p1);
    // No constraints at all -- the free point's 2 DOF are entirely open.

    const DofReport report = analyzeDof(sketch);
    REQUIRE(report.totalDof == 2);
    REQUIRE(report.constraintEquations == 0);
    REQUIRE(report.remainingDof == 2);
    REQUIRE_FALSE(report.likelyOverConstrained);
}

TEST_CASE("analyzeDof reports zero remaining freedom for the exactly-constrained rectangle", "[sketch][dof]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true);
    const int p1 = sketch.addPoint(Point2D(18, 2));
    const int p2 = sketch.addPoint(Point2D(19, 9));
    const int p3 = sketch.addPoint(Point2D(1, 11));
    const int l0 = sketch.addLine(p0, p1);
    const int l1 = sketch.addLine(p1, p2);
    const int l2 = sketch.addLine(p2, p3);
    const int l3 = sketch.addLine(p3, p0);
    sketch.addConstraint({SketchConstraintType::Horizontal, l0});
    sketch.addConstraint({SketchConstraintType::Horizontal, l2});
    sketch.addConstraint({SketchConstraintType::Vertical, l1});
    sketch.addConstraint({SketchConstraintType::Vertical, l3});
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p0, p1, 20.0});
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p1, p2, 10.0});

    // 6 free points' worth of DOF (3 free points x 2) == 6 constraint equations.
    const DofReport report = analyzeDof(sketch);
    REQUIRE(report.totalDof == 6);
    REQUIRE(report.constraintEquations == 6);
    REQUIRE(report.remainingDof == 0);
    REQUIRE_FALSE(report.likelyOverConstrained);
}

TEST_CASE("analyzeDof flags likelyOverConstrained when equations outnumber free variables", "[sketch][dof]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true);
    const int p1 = sketch.addPoint(Point2D(10, 0), true); // both fixed: 0 total DOF
    const int line = sketch.addLine(p0, p1);
    sketch.addConstraint({SketchConstraintType::Horizontal, line});
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p0, p1, 10.0});

    const DofReport report = analyzeDof(sketch);
    REQUIRE(report.totalDof == 0);
    REQUIRE(report.constraintEquations == 2);
    REQUIRE(report.remainingDof == 0);
    REQUIRE(report.likelyOverConstrained);
}

TEST_CASE("solveSketch satisfies a general Angle constraint between two lines", "[sketch][solver]") {
    Sketch sketch;
    const int a1 = sketch.addPoint(Point2D(0, 0), true);
    const int a2 = sketch.addPoint(Point2D(10, 0), true); // fixed, horizontal
    const int b1 = sketch.addPoint(Point2D(0, 0), true);
    const int b2 = sketch.addPoint(Point2D(8, 2)); // not yet at 60 degrees from A
    const int lineA = sketch.addLine(a1, a2);
    const int lineB = sketch.addLine(b1, b2);
    SketchConstraint c;
    c.type = SketchConstraintType::Angle;
    c.geomA = lineA;
    c.geomB = lineB;
    c.value = 60.0 * M_PI / 180.0;
    sketch.addConstraint(c);

    REQUIRE(solveSketch(sketch).converged);
    const Point2D dirA = sketch.points()[static_cast<std::size_t>(a2)] - sketch.points()[static_cast<std::size_t>(a1)];
    const Point2D dirB = sketch.points()[static_cast<std::size_t>(b2)] - sketch.points()[static_cast<std::size_t>(b1)];
    const double cosAngle = (dirA.x * dirB.x + dirA.y * dirB.y) / (dirA.length() * dirB.length());
    const double angleDegrees = std::acos(std::clamp(cosAngle, -1.0, 1.0)) * 180.0 / M_PI;
    REQUIRE(angleDegrees == Approx(60.0).margin(1e-4));
}

TEST_CASE("solveSketch pins a free point onto a line via PointOnLine", "[sketch][solver]") {
    Sketch sketch;
    const int p1 = sketch.addPoint(Point2D(0, 0), true);
    const int p2 = sketch.addPoint(Point2D(10, 0), true); // fixed horizontal line along y=0
    const int loose = sketch.addPoint(Point2D(5, 3));     // off the line
    const int line = sketch.addLine(p1, p2);
    SketchConstraint c;
    c.type = SketchConstraintType::PointOnLine;
    c.geomA = line;
    c.pointA = loose;
    sketch.addConstraint(c);

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.points()[static_cast<std::size_t>(loose)].y == Approx(0.0).margin(1e-6));
}

TEST_CASE("solveSketch pins a free point onto a circle via PointOnCircle", "[sketch][solver]") {
    Sketch sketch;
    const int center = sketch.addPoint(Point2D(0, 0), true);
    const int circle = sketch.addCircle(center, 5.0);
    sketch.addConstraint({SketchConstraintType::Radius, circle, -1, -1, -1, 5.0}); // keep the radius fixed at 5
    const int loose = sketch.addPoint(Point2D(3, 3));                             // distance ~4.24, not on the circle
    SketchConstraint c;
    c.type = SketchConstraintType::PointOnCircle;
    c.geomA = circle;
    c.pointA = loose;
    sketch.addConstraint(c);

    REQUIRE(solveSketch(sketch).converged);
    const Point2D centerPos = sketch.points()[static_cast<std::size_t>(center)];
    REQUIRE(sketch.points()[static_cast<std::size_t>(loose)].distanceTo(centerPos) == Approx(5.0).margin(1e-6));
}

TEST_CASE("solveSketch pins a free point to a line's midpoint via Midpoint", "[sketch][solver]") {
    Sketch sketch;
    const int p1 = sketch.addPoint(Point2D(0, 0), true);
    const int p2 = sketch.addPoint(Point2D(10, 0), true);
    const int loose = sketch.addPoint(Point2D(2, 7));
    const int line = sketch.addLine(p1, p2);
    SketchConstraint c;
    c.type = SketchConstraintType::Midpoint;
    c.geomA = line;
    c.pointA = loose;
    sketch.addConstraint(c);

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.points()[static_cast<std::size_t>(loose)].x == Approx(5.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(loose)].y == Approx(0.0).margin(1e-6));
}

TEST_CASE("solveSketch mirrors a free point across an axis line via Symmetric", "[sketch][solver]") {
    Sketch sketch;
    // Axis: the Y axis (x == 0).
    const int axisP1 = sketch.addPoint(Point2D(0, -10), true);
    const int axisP2 = sketch.addPoint(Point2D(0, 10), true);
    const int axis = sketch.addLine(axisP1, axisP2);
    const int fixedPoint = sketch.addPoint(Point2D(3, 2), true);
    const int loose = sketch.addPoint(Point2D(1, 1)); // should settle at (-3, 2)
    SketchConstraint c;
    c.type = SketchConstraintType::Symmetric;
    c.geomA = axis;
    c.pointA = fixedPoint;
    c.pointB = loose;
    sketch.addConstraint(c);

    REQUIRE(solveSketch(sketch).converged);
    REQUIRE(sketch.points()[static_cast<std::size_t>(loose)].x == Approx(-3.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(loose)].y == Approx(2.0).margin(1e-6));
}

TEST_CASE("analyzeDof counts Midpoint and Symmetric as 2 equations each, not 1", "[sketch][dof]") {
    Sketch sketch;
    const int p1 = sketch.addPoint(Point2D(0, 0), true);
    const int p2 = sketch.addPoint(Point2D(10, 0), true);
    const int loose = sketch.addPoint(Point2D(2, 7));
    const int line = sketch.addLine(p1, p2);
    SketchConstraint c;
    c.type = SketchConstraintType::Midpoint;
    c.geomA = line;
    c.pointA = loose;
    sketch.addConstraint(c);

    const DofReport report = analyzeDof(sketch);
    REQUIRE(report.totalDof == 2); // the one free point
    REQUIRE(report.constraintEquations == 2);
    REQUIRE(report.remainingDof == 0);
    REQUIRE_FALSE(report.likelyOverConstrained);
}

TEST_CASE("SketchPlane base planes form correct orthonormal right-handed frames", "[sketch][plane]") {
    auto checkOrthonormalRightHanded = [](const SketchPlane& p) {
        const Point3D y = p.yAxis();
        auto len = [](const Point3D& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
        auto dot = [](const Point3D& a, const Point3D& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        REQUIRE(len(p.xAxis) == Approx(1.0));
        REQUIRE(len(y) == Approx(1.0));
        REQUIRE(len(p.normal) == Approx(1.0));
        REQUIRE(dot(p.xAxis, y) == Approx(0.0).margin(1e-9));
        REQUIRE(dot(p.xAxis, p.normal) == Approx(0.0).margin(1e-9));
        REQUIRE(dot(y, p.normal) == Approx(0.0).margin(1e-9));
        // Right-handed: normal x xAxis == yAxis (by construction), so a
        // local (1,0) then (0,1) point should sweep the same handedness
        // as normal itself when crossed.
        const Point3D cross = {p.normal.y * p.xAxis.z - p.normal.z * p.xAxis.y,
                               p.normal.z * p.xAxis.x - p.normal.x * p.xAxis.z,
                               p.normal.x * p.xAxis.y - p.normal.y * p.xAxis.x};
        REQUIRE(dot(cross, y) == Approx(1.0));
    };

    checkOrthonormalRightHanded(SketchPlane::XY());
    checkOrthonormalRightHanded(SketchPlane::XZ());
    checkOrthonormalRightHanded(SketchPlane::YZ());
    checkOrthonormalRightHanded(SketchPlane::XY(5.0, 37.0));
    checkOrthonormalRightHanded(SketchPlane::XZ(-2.0, 90.0));
    checkOrthonormalRightHanded(SketchPlane::YZ(1.0, 180.0));
}

TEST_CASE("SketchPlane::XY defaults exactly match the pre-existing implicit flat behavior", "[sketch][plane]") {
    const SketchPlane plane; // default-constructed, no factory call
    REQUIRE(plane.origin.x == Approx(0.0));
    REQUIRE(plane.origin.y == Approx(0.0));
    REQUIRE(plane.origin.z == Approx(0.0));
    REQUIRE(plane.normal.z == Approx(1.0));
    REQUIRE(plane.xAxis.x == Approx(1.0));

    const Point3D w = plane.toWorld(Point2D(3, 4));
    REQUIRE(w.x == Approx(3.0));
    REQUIRE(w.y == Approx(4.0));
    REQUIRE(w.z == Approx(0.0));
}

TEST_CASE("SketchPlane toWorld maps local points correctly for XZ and YZ base planes", "[sketch][plane]") {
    // XZ plane: local (x,y) -> world (x, -offset, y) -- offset moves along
    // the plane's own normal, which for XZ points toward world -Y (the
    // choice that keeps (xAxis, yAxis, normal) right-handed with
    // yAxis == world +Z; see SketchPlane::XZ's own implementation).
    const SketchPlane xz = SketchPlane::XZ(7.0);
    const Point3D wxz = xz.toWorld(Point2D(2, 3));
    REQUIRE(wxz.x == Approx(2.0));
    REQUIRE(wxz.y == Approx(-7.0));
    REQUIRE(wxz.z == Approx(3.0));

    // YZ plane: local (x,y) -> world (offset, x, y).
    const SketchPlane yz = SketchPlane::YZ(-4.0);
    const Point3D wyz = yz.toWorld(Point2D(2, 3));
    REQUIRE(wyz.x == Approx(-4.0));
    REQUIRE(wyz.y == Approx(2.0));
    REQUIRE(wyz.z == Approx(3.0));
}

TEST_CASE("SketchPlane angle rotates the local X axis within the plane", "[sketch][plane]") {
    // A 90-degree attachment angle on the XY plane should swap the
    // effective local X/Y directions (xAxis becomes world +Y).
    const SketchPlane rotated = SketchPlane::XY(0.0, 90.0);
    REQUIRE(rotated.xAxis.x == Approx(0.0).margin(1e-9));
    REQUIRE(rotated.xAxis.y == Approx(1.0));
    const Point3D y = rotated.yAxis();
    REQUIRE(y.x == Approx(-1.0));
    REQUIRE(y.y == Approx(0.0).margin(1e-9));
}

namespace {
double distanceToInfiniteLine(const Point2D& p1, const Point2D& p2, const Point2D& c) {
    const Point2D dir = p2 - p1;
    const double len = dir.length();
    return std::abs(dir.x * (c.y - p1.y) - dir.y * (c.x - p1.x)) / len;
}
} // namespace

TEST_CASE("sketchFillet rounds a right-angle corner shared structurally by two lines", "[sketch][fillet]") {
    Sketch sketch;
    const int corner = sketch.addPoint(Point2D(0, 0));
    const int farA = sketch.addPoint(Point2D(10, 0));
    const int farB = sketch.addPoint(Point2D(0, 10));
    const int lineA = sketch.addLine(farA, corner); // corner is lineA.p2
    const int lineB = sketch.addLine(corner, farB); // corner is lineB.p1

    REQUIRE(sketchFillet(sketch, lineA, lineB, 2.0));
    REQUIRE(sketch.arcs().size() == 1);

    const SketchArc& arc = sketch.arcs()[0];
    REQUIRE(arc.radius == Approx(2.0));
    const Point2D center = sketch.points()[static_cast<std::size_t>(arc.center)];
    const Point2D start = sketch.points()[static_cast<std::size_t>(arc.start)];
    const Point2D end = sketch.points()[static_cast<std::size_t>(arc.end)];
    REQUIRE(center.distanceTo(start) == Approx(2.0).margin(1e-6));
    REQUIRE(center.distanceTo(end) == Approx(2.0).margin(1e-6));

    // The lines' new near-endpoints must coincide with the arc's own
    // start/end -- structural coincidence, not just numeric proximity.
    const SketchLine& lA = sketch.lines()[static_cast<std::size_t>(lineA)];
    const SketchLine& lB = sketch.lines()[static_cast<std::size_t>(lineB)];
    REQUIRE((lA.p2 == arc.start || lA.p2 == arc.end));
    REQUIRE((lB.p1 == arc.start || lB.p1 == arc.end));
    REQUIRE(lA.p2 != lB.p1); // the shared vertex was split, not reused by both

    // The arc must be tangent to both (untouched, far-side) line directions.
    const Point2D farAPos = sketch.points()[static_cast<std::size_t>(farA)];
    const Point2D farBPos = sketch.points()[static_cast<std::size_t>(farB)];
    const Point2D nearAPos = sketch.points()[static_cast<std::size_t>(lA.p2)];
    const Point2D nearBPos = sketch.points()[static_cast<std::size_t>(lB.p1)];
    REQUIRE(distanceToInfiniteLine(farAPos, nearAPos, center) == Approx(2.0).margin(1e-6));
    REQUIRE(distanceToInfiniteLine(farBPos, nearBPos, center) == Approx(2.0).margin(1e-6));
}

TEST_CASE("sketchFillet rounds a corner between two lines that only meet when extended", "[sketch][fillet]") {
    Sketch sketch;
    // Two collinear-ish but offset lines whose infinite extensions meet at (10,10),
    // not sharing any point index.
    const int a1 = sketch.addPoint(Point2D(0, 0));
    const int a2 = sketch.addPoint(Point2D(8, 0));
    const int b1 = sketch.addPoint(Point2D(10, 2));
    const int b2 = sketch.addPoint(Point2D(10, 10));
    const int lineA = sketch.addLine(a1, a2);
    const int lineB = sketch.addLine(b1, b2);

    REQUIRE(sketchFillet(sketch, lineA, lineB, 1.5));
    REQUIRE(sketch.arcs().size() == 1);
    const SketchArc& arc = sketch.arcs()[0];
    REQUIRE(arc.radius == Approx(1.5));
    // No new points needed when there's no shared vertex to split.
    REQUIRE(sketch.points().size() == 4 /* original */ + 1 /* arc center */);
}

TEST_CASE("sketchFillet fails cleanly for parallel lines, oversized radius, or bad indices",
         "[sketch][fillet]") {
    Sketch sketch;
    const int corner = sketch.addPoint(Point2D(0, 0));
    const int farA = sketch.addPoint(Point2D(10, 0));
    const int farB = sketch.addPoint(Point2D(0, 10));
    const int lineA = sketch.addLine(farA, corner);
    const int lineB = sketch.addLine(corner, farB);

    REQUIRE_FALSE(sketchFillet(sketch, lineA, lineB, 0.0));   // non-positive radius
    REQUIRE_FALSE(sketchFillet(sketch, lineA, lineB, 100.0)); // too large to fit
    REQUIRE_FALSE(sketchFillet(sketch, lineA, lineA, 1.0));   // same line twice
    REQUIRE_FALSE(sketchFillet(sketch, lineA, 99, 1.0));      // out-of-range index
    REQUIRE(sketch.arcs().empty());

    Sketch parallelSketch;
    const int p1 = parallelSketch.addPoint(Point2D(0, 0));
    const int p2 = parallelSketch.addPoint(Point2D(10, 0));
    const int p3 = parallelSketch.addPoint(Point2D(0, 5));
    const int p4 = parallelSketch.addPoint(Point2D(10, 5));
    const int pl1 = parallelSketch.addLine(p1, p2);
    const int pl2 = parallelSketch.addLine(p3, p4);
    REQUIRE_FALSE(sketchFillet(parallelSketch, pl1, pl2, 1.0));
    REQUIRE(parallelSketch.arcs().empty());
}

TEST_CASE("Sketch::addSpline stores its control points and returns a growing index", "[sketch][spline]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0));
    const int p1 = sketch.addPoint(Point2D(5, 5));
    const int p2 = sketch.addPoint(Point2D(10, 0));
    const int spline = sketch.addSpline({p0, p1, p2});
    REQUIRE(spline == 0);
    REQUIRE(sketch.splines().size() == 1);
    REQUIRE(sketch.splines()[0].controlPoints == std::vector<int>{p0, p1, p2});
    REQUIRE_FALSE(sketch.splines()[0].construction);
}

TEST_CASE("evaluateSketchSpline linearly interpolates between exactly two control points", "[sketch][spline]") {
    const std::vector<Point2D> ctrl = {Point2D(0, 0), Point2D(10, 20)};
    REQUIRE(evaluateSketchSpline(ctrl, 0.0).x == Approx(0.0));
    REQUIRE(evaluateSketchSpline(ctrl, 1.0).x == Approx(10.0));
    const Point2D mid = evaluateSketchSpline(ctrl, 0.5);
    REQUIRE(mid.x == Approx(5.0));
    REQUIRE(mid.y == Approx(10.0));
}

TEST_CASE("evaluateSketchSpline matches the closed-form quadratic Bezier for exactly three control points",
         "[sketch][spline]") {
    const std::vector<Point2D> ctrl = {Point2D(0, 0), Point2D(10, 20), Point2D(20, 0)};
    // Single-span (no interior knots) degree-2 clamped B-spline through 3
    // poles is mathematically identical to a quadratic Bezier through the
    // same 3 points: B(t) = (1-t)^2*P0 + 2t(1-t)*P1 + t^2*P2.
    for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const double bx = (1 - t) * (1 - t) * ctrl[0].x + 2 * t * (1 - t) * ctrl[1].x + t * t * ctrl[2].x;
        const double by = (1 - t) * (1 - t) * ctrl[0].y + 2 * t * (1 - t) * ctrl[1].y + t * t * ctrl[2].y;
        const Point2D got = evaluateSketchSpline(ctrl, t);
        REQUIRE(got.x == Approx(bx).margin(1e-9));
        REQUIRE(got.y == Approx(by).margin(1e-9));
    }
}

TEST_CASE("evaluateSketchSpline endpoints always match the first/last control points exactly",
         "[sketch][spline]") {
    const std::vector<Point2D> ctrl = {Point2D(0, 0), Point2D(3, 8), Point2D(7, -4), Point2D(10, 1), Point2D(15, 6)};
    REQUIRE(evaluateSketchSpline(ctrl, 0.0).x == Approx(0.0));
    REQUIRE(evaluateSketchSpline(ctrl, 0.0).y == Approx(0.0));
    REQUIRE(evaluateSketchSpline(ctrl, 1.0).x == Approx(15.0));
    REQUIRE(evaluateSketchSpline(ctrl, 1.0).y == Approx(6.0));
}

TEST_CASE("evaluateSketchSpline degenerates to the single point for fewer than 2 control points",
         "[sketch][spline]") {
    REQUIRE(evaluateSketchSpline({}, 0.5).x == Approx(0.0));
    REQUIRE(evaluateSketchSpline({Point2D(4, 7)}, 0.9).x == Approx(4.0));
    REQUIRE(evaluateSketchSpline({Point2D(4, 7)}, 0.9).y == Approx(7.0));
}

TEST_CASE("Fix constraint pins an otherwise-free point to an absolute position", "[sketch][constraint][fix]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(5.0, 5.0)); // free, deliberately off-target
    sketch.addConstraint(SketchConstraint{SketchConstraintType::Fix, -1, -1, p0, -1, 12.0, -3.0});

    const SolveResult result = solveSketch(sketch);
    REQUIRE(result.converged);
    REQUIRE(sketch.points()[static_cast<std::size_t>(p0)].x == Approx(12.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p0)].y == Approx(-3.0).margin(1e-6));
}

TEST_CASE("makeFixConstraint pins a point to its own current position", "[sketch][constraint][fix]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(7.0, -2.5));
    const int p1 = sketch.addPoint(Point2D(1.0, 1.0)); // free, no constraint touches it directly
    const int l0 = sketch.addLine(p0, p1);
    sketch.addConstraint(makeFixConstraint(sketch, p0));
    sketch.addConstraint({SketchConstraintType::Distance, -1, -1, p0, p1, 15.0});
    sketch.addConstraint({SketchConstraintType::Horizontal, l0});

    const SolveResult result = solveSketch(sketch);
    REQUIRE(result.converged);
    // p0 stayed exactly where it was fixed.
    REQUIRE(sketch.points()[static_cast<std::size_t>(p0)].x == Approx(7.0).margin(1e-6));
    REQUIRE(sketch.points()[static_cast<std::size_t>(p0)].y == Approx(-2.5).margin(1e-6));
    // p1 moved to satisfy Distance + Horizontal relative to the fixed p0.
    REQUIRE(sketch.points()[static_cast<std::size_t>(p1)].y == Approx(-2.5).margin(1e-6));
    REQUIRE(std::abs(sketch.points()[static_cast<std::size_t>(p1)].x - 7.0) == Approx(15.0).margin(1e-6));
}

TEST_CASE("Fix constraint counts as 2 equations in analyzeDof", "[sketch][constraint][fix]") {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(3.0, 4.0));
    sketch.addConstraint(makeFixConstraint(sketch, p0));
    const DofReport report = analyzeDof(sketch);
    REQUIRE(report.totalDof == 2);
    REQUIRE(report.constraintEquations == 2);
    REQUIRE(report.remainingDof == 0);
    REQUIRE_FALSE(report.likelyOverConstrained);
}
