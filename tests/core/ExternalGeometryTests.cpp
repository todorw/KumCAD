#include "core/core3d/ExternalGeometry.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace lcad;

namespace {
int edgeCountOf(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    return edgeMap.Extent();
}
} // namespace

TEST_CASE("projectExternalEdge copies each straight edge of a box as a fixed-point construction line",
         "[core3d][external-geometry]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const int count = edgeCountOf(box);
    REQUIRE(count == 12);

    for (int i = 0; i < count; ++i) {
        Sketch sketch; // defaults to the world XY plane, matching the box's own bottom face
        REQUIRE(projectExternalEdge(sketch, box, i));

        REQUIRE(sketch.points().size() == 2);
        REQUIRE(sketch.pointFixed()[0]);
        REQUIRE(sketch.pointFixed()[1]);
        REQUIRE(sketch.lines().size() == 1);
        REQUIRE(sketch.lines()[0].construction);
        REQUIRE(sketch.circles().empty());

        // Every box edge is exactly 10 units long: a horizontal edge
        // (lying in the XY plane, or parallel to it) projects to a
        // length-10 line; a vertical edge (along Z) collapses to a
        // zero-length line under this orthogonal XY projection.
        const double length = sketch.points()[0].distanceTo(sketch.points()[1]);
        const bool isHorizontalLength = std::abs(length - 10.0) < 1e-6;
        const bool isVerticalCollapse = length < 1e-6;
        REQUIRE((isHorizontalLength || isVerticalCollapse));
    }
}

TEST_CASE("projectExternalEdge returns false and adds nothing for an out-of-range edge index",
         "[core3d][external-geometry]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    Sketch sketch;

    REQUIRE_FALSE(projectExternalEdge(sketch, box, 999));
    REQUIRE_FALSE(projectExternalEdge(sketch, box, -1));
    REQUIRE(sketch.points().empty());
    REQUIRE(sketch.lines().empty());
}

TEST_CASE("projectExternalEdge preserves an exact circle when its plane is parallel to the sketch",
         "[core3d][external-geometry]") {
    // Default BRepPrimAPI_MakeCylinder axis is world Z, parallel to the
    // sketch's own default (world XY, normal Z) plane -- its rim edges
    // should project as exact circles, not a tessellated approximation.
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5.0, 20.0).Shape();
    const int count = edgeCountOf(cylinder);

    Sketch sketch;
    for (int i = 0; i < count; ++i) REQUIRE(projectExternalEdge(sketch, cylinder, i));

    REQUIRE_FALSE(sketch.circles().empty());
    const bool hasExpectedRadius = std::any_of(sketch.circles().begin(), sketch.circles().end(),
                                              [](const SketchCircle& c) { return c.radius == Catch::Approx(5.0); });
    REQUIRE(hasExpectedRadius);
    for (const SketchCircle& c : sketch.circles()) REQUIRE(c.construction);
}

TEST_CASE("projectExternalEdge preserves an exact SketchArc for a trimmed circular edge parallel to the "
         "sketch",
         "[core3d][external-geometry]") {
    // A quarter-circle arc, radius 5, centered at (3,4,0), axis +Z
    // (parallel to the sketch's own default XY plane), XDirection pinned
    // to world +X so the expected projected points are exact -- from
    // angle 0 (world (8,4,0)) to angle PI/2 (world (3,9,0)).
    const gp_Ax2 axis(gp_Pnt(3, 4, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    Handle(Geom_Circle) circle = new Geom_Circle(axis, 5.0);
    Handle(Geom_TrimmedCurve) trimmed = new Geom_TrimmedCurve(circle, 0.0, M_PI / 2.0);
    const TopoDS_Edge arcEdge = BRepBuilderAPI_MakeEdge(trimmed).Edge();

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, arcEdge);

    Sketch sketch;
    REQUIRE(projectExternalEdge(sketch, compound, 0));

    REQUIRE(sketch.arcs().size() == 1);
    REQUIRE(sketch.lines().empty());  // a real arc, not a tessellated polyline
    REQUIRE(sketch.circles().empty()); // trimmed, not a full circle

    const SketchArc& arc = sketch.arcs()[0];
    REQUIRE(arc.construction);
    REQUIRE(arc.radius == Catch::Approx(5.0));
    REQUIRE(arc.ccw); // increasing angle 0 -> PI/2 is the CCW direction

    const Point2D& center = sketch.points()[static_cast<std::size_t>(arc.center)];
    const Point2D& start = sketch.points()[static_cast<std::size_t>(arc.start)];
    const Point2D& end = sketch.points()[static_cast<std::size_t>(arc.end)];
    REQUIRE(center.x == Catch::Approx(3.0));
    REQUIRE(center.y == Catch::Approx(4.0));
    REQUIRE(start.x == Catch::Approx(8.0));
    REQUIRE(start.y == Catch::Approx(4.0));
    REQUIRE(end.x == Catch::Approx(3.0));
    REQUIRE(end.y == Catch::Approx(9.0));
}

TEST_CASE("projectExternalEdge tessellates a circular edge whose plane isn't parallel to the sketch",
         "[core3d][external-geometry]") {
    // Cylinder axis along world X: its rim circles' own plane normal (X)
    // is perpendicular to the sketch's default normal (Z), so an exact
    // circle would distort into an ellipse under orthogonal projection --
    // must fall back to a tessellated polyline instead.
    const gp_Ax2 axis(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(axis, 5.0, 20.0).Shape();

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(cylinder, TopAbs_EDGE, edgeMap);

    bool foundTessellated = false;
    for (int i = 0; i < edgeMap.Extent(); ++i) {
        Sketch sketch;
        REQUIRE(projectExternalEdge(sketch, cylinder, i, 24));
        if (sketch.circles().empty() && sketch.lines().size() == 24) foundTessellated = true;
    }
    REQUIRE(foundTessellated);
}
