#include "core/core3d/ExternalGeometry.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
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
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

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

namespace {
TopoDS_Shape translated(const TopoDS_Shape& shape, double dx, double dy, double dz) {
    gp_Trsf move;
    move.SetTranslation(gp_Vec(dx, dy, dz));
    return BRepBuilderAPI_Transform(shape, move, true).Shape();
}
} // namespace

TEST_CASE("projectExternalEdgeTracked records a Line ref that refreshExternalGeometry can re-sync in place",
         "[core3d][external-geometry][refresh]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    // Edge 0 is a straight, horizontal (non-collapsing) edge of the box in
    // every OCCT version this codebase has been built against so far --
    // pick whichever one actually projects to a length-10 line, matching
    // the first test's own "some edges collapse under this XY projection"
    // observation, rather than hard-coding an index.
    Sketch sketch;
    ExternalGeometryRef ref;
    int edgeIndex = -1;
    for (int i = 0; i < edgeCountOf(box); ++i) {
        Sketch probe;
        ExternalGeometryRef probeRef;
        REQUIRE(projectExternalEdgeTracked(probe, box, i, probeRef));
        if (probeRef.kind == ExternalGeometryRef::Kind::Line &&
            probe.points()[0].distanceTo(probe.points()[1]) > 1.0) {
            edgeIndex = i;
            break;
        }
    }
    REQUIRE(edgeIndex >= 0);
    REQUIRE(projectExternalEdgeTracked(sketch, box, edgeIndex, ref));
    REQUIRE(ref.kind == ExternalGeometryRef::Kind::Line);
    REQUIRE(ref.pointIndices.size() == 2);

    const Point2D before0 = sketch.points()[static_cast<std::size_t>(ref.pointIndices[0])];
    const Point2D before1 = sketch.points()[static_cast<std::size_t>(ref.pointIndices[1])];

    // A small move -- TopoNaming.h's own nearest-fingerprint match is a
    // real, disclosed MITIGATION for a moderate edit reshuffling edge
    // order, not a guarantee against a large rigid move landing closer to
    // a DIFFERENT edge's old position on a symmetric shape like a cube
    // (see TopoNaming.h's own comment); this stays well inside what it
    // actually promises.
    const TopoDS_Shape movedBox = translated(box, 1.0, 0.5, 0.0);
    REQUIRE(refreshExternalGeometry(sketch, ref, movedBox));

    // Still exactly 2 points/1 line -- refresh overwrote in place, it
    // didn't append a duplicate.
    REQUIRE(sketch.points().size() == 2);
    REQUIRE(sketch.lines().size() == 1);
    const Point2D after0 = sketch.points()[static_cast<std::size_t>(ref.pointIndices[0])];
    const Point2D after1 = sketch.points()[static_cast<std::size_t>(ref.pointIndices[1])];
    REQUIRE(after0.distanceTo(Point2D(before0.x + 1.0, before0.y + 0.5)) < 1e-6);
    REQUIRE(after1.distanceTo(Point2D(before1.x + 1.0, before1.y + 0.5)) < 1e-6);
}

TEST_CASE("refreshExternalGeometry re-syncs a full circle to a moved cylinder's rim",
         "[core3d][external-geometry][refresh]") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(5.0, 20.0).Shape();
    const int count = edgeCountOf(cylinder);

    Sketch sketch;
    ExternalGeometryRef ref;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        ExternalGeometryRef candidate;
        Sketch probe;
        REQUIRE(projectExternalEdgeTracked(probe, cylinder, i, candidate));
        if (candidate.kind == ExternalGeometryRef::Kind::Circle) {
            REQUIRE(projectExternalEdgeTracked(sketch, cylinder, i, ref));
            found = true;
            break;
        }
    }
    REQUIRE(found);
    REQUIRE(sketch.circles().size() == 1);
    REQUIRE(sketch.circles()[0].radius == Catch::Approx(5.0));

    const TopoDS_Shape movedCylinder = translated(cylinder, 7.0, -3.0, 0.0);
    REQUIRE(refreshExternalGeometry(sketch, ref, movedCylinder));

    REQUIRE(sketch.circles().size() == 1); // still exactly one -- overwritten, not duplicated
    REQUIRE(sketch.circles()[0].radius == Catch::Approx(5.0));
    const Point2D& center = sketch.points()[static_cast<std::size_t>(ref.pointIndices[0])];
    REQUIRE(center.x == Catch::Approx(7.0));
    REQUIRE(center.y == Catch::Approx(-3.0));
}

TEST_CASE("refreshExternalGeometry re-syncs a trimmed arc's center/start/end/radius to a moved edge",
         "[core3d][external-geometry][refresh]") {
    auto makeArcCompound = [](double cx, double cy) {
        const gp_Ax2 axis(gp_Pnt(cx, cy, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
        Handle(Geom_Circle) circle = new Geom_Circle(axis, 5.0);
        Handle(Geom_TrimmedCurve) trimmed = new Geom_TrimmedCurve(circle, 0.0, M_PI / 2.0);
        const TopoDS_Edge arcEdge = BRepBuilderAPI_MakeEdge(trimmed).Edge();
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        builder.Add(compound, arcEdge);
        return compound;
    };

    const TopoDS_Shape original = makeArcCompound(3.0, 4.0);
    Sketch sketch;
    ExternalGeometryRef ref;
    REQUIRE(projectExternalEdgeTracked(sketch, original, 0, ref));
    REQUIRE(ref.kind == ExternalGeometryRef::Kind::Arc);
    REQUIRE(sketch.arcs().size() == 1);

    const TopoDS_Shape moved = makeArcCompound(30.0, 40.0);
    REQUIRE(refreshExternalGeometry(sketch, ref, moved));

    REQUIRE(sketch.arcs().size() == 1); // overwritten, not duplicated
    const SketchArc& arc = sketch.arcs()[0];
    REQUIRE(arc.radius == Catch::Approx(5.0));
    REQUIRE(arc.ccw);
    const Point2D& center = sketch.points()[static_cast<std::size_t>(arc.center)];
    const Point2D& start = sketch.points()[static_cast<std::size_t>(arc.start)];
    const Point2D& end = sketch.points()[static_cast<std::size_t>(arc.end)];
    REQUIRE(center.x == Catch::Approx(30.0));
    REQUIRE(center.y == Catch::Approx(40.0));
    REQUIRE(start.x == Catch::Approx(35.0));
    REQUIRE(start.y == Catch::Approx(40.0));
    REQUIRE(end.x == Catch::Approx(30.0));
    REQUIRE(end.y == Catch::Approx(45.0));
}

TEST_CASE("refreshExternalGeometry re-syncs a tessellated (non-parallel) edge's sample points",
         "[core3d][external-geometry][refresh]") {
    const gp_Ax2 axis(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(axis, 5.0, 20.0).Shape();

    Sketch sketch;
    ExternalGeometryRef ref;
    bool found = false;
    for (int i = 0; i < edgeCountOf(cylinder); ++i) {
        Sketch probe;
        ExternalGeometryRef candidate;
        REQUIRE(projectExternalEdgeTracked(probe, cylinder, i, candidate, 24));
        if (candidate.kind == ExternalGeometryRef::Kind::Tessellated) {
            REQUIRE(projectExternalEdgeTracked(sketch, cylinder, i, ref, 24));
            found = true;
            break;
        }
    }
    REQUIRE(found);
    REQUIRE(sketch.points().size() == 25);
    REQUIRE(sketch.lines().size() == 24);

    const TopoDS_Shape moved = translated(cylinder, 0.0, 11.0, 13.0);
    REQUIRE(refreshExternalGeometry(sketch, ref, moved));

    // Still exactly the same point/line count -- overwritten in place.
    REQUIRE(sketch.points().size() == 25);
    REQUIRE(sketch.lines().size() == 24);
}

TEST_CASE("refreshExternalGeometry fails cleanly on a null shape or a topology mismatch",
         "[core3d][external-geometry][refresh]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    Sketch sketch;
    ExternalGeometryRef ref;
    int edgeIndex = -1;
    for (int i = 0; i < edgeCountOf(box); ++i) {
        Sketch probe;
        ExternalGeometryRef probeRef;
        REQUIRE(projectExternalEdgeTracked(probe, box, i, probeRef));
        if (probeRef.kind == ExternalGeometryRef::Kind::Line &&
            probe.points()[0].distanceTo(probe.points()[1]) > 1.0) {
            edgeIndex = i;
            break;
        }
    }
    REQUIRE(edgeIndex >= 0);
    REQUIRE(projectExternalEdgeTracked(sketch, box, edgeIndex, ref));

    REQUIRE_FALSE(refreshExternalGeometry(sketch, ref, TopoDS_Shape()));

    // A shape with only a circular edge: resolveEdgeIndex still returns
    // SOME nearest match (it never refuses, see its own header comment),
    // but that match is a circle, not a line, so refreshExternalGeometry's
    // own type-mismatch guard must refuse it rather than silently
    // corrupting sketch's Line-kind ref with circle-derived data.
    Handle(Geom_Circle) circle = new Geom_Circle(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 5.0);
    const TopoDS_Edge circleEdge = BRepBuilderAPI_MakeEdge(circle).Edge();
    TopoDS_Compound onlyCircle;
    BRep_Builder builder;
    builder.MakeCompound(onlyCircle);
    builder.Add(onlyCircle, circleEdge);
    REQUIRE_FALSE(refreshExternalGeometry(sketch, ref, onlyCircle));
    // Untouched: still the original pre-refresh values.
    REQUIRE(sketch.points().size() == 2);
}
