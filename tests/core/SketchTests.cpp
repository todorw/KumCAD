#include "core/sketch/ConstraintSolver.h"
#include "core/sketch/LinearSolve.h"
#include "core/sketch/SketchGeometry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
