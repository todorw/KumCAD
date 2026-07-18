#include "core/core3d/ExternalGeometry.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
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
