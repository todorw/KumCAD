#pragma once

#include "core/core3d/Fingerprint.h"
#include "core/sketch/SketchGeometry.h"

#include <TopoDS_Shape.hxx>

#include <vector>

namespace lcad {

// FreeCAD's "External Geometry" sketch tool: projects one edge of an
// existing 3D feature's shape onto sketch's own plane, appending it as
// construction geometry (SketchLine/SketchCircle, matching whichever the
// edge's own underlying curve type is) with its points FIXED. Real,
// disclosed simplification: this is a one-shot copy, not FreeCAD's own
// live-linked reference that automatically re-projects if the source
// feature changes later -- re-run this after editing the source to
// refresh it.
//
// edgeIndex indexes TopExp::MapShapes(shape, TopAbs_EDGE, ...)'s own
// ordering -- the same "typed index instead of interactive sub-pick"
// convention Pick3D.h's own EdgePickResult::edgeIndex already documents
// (this codebase's selection system can't click INTO another feature's
// shape from inside the sketch editor).
//
// The projection is orthogonal onto sketch's plane (drop the
// out-of-plane component along its normal), matching FreeCAD's own
// external-geometry projection behavior. A straight edge (GeomAbs_Line)
// becomes one SketchLine between two new projected points. A circular
// edge (GeomAbs_Circle) whose own axis is parallel to sketch's normal
// projects to an exact SketchCircle (if untrimmed/a full circle) or
// SketchArc (if trimmed) at the projected center with the circle's own
// true radius -- an oblique circle would distort into an ellipse under
// orthogonal projection, which SketchGeometry.h has no representation
// for, so both of these cases require that parallel check. A trimmed
// arc's ccw direction is determined empirically from its own projected
// midpoint rather than the 3D axis's sign (isParallel allows the axis to
// point either way relative to sketch's own normal). Everything else
// (non-parallel circles/arcs, ellipses, B-splines) is tessellated into a
// polyline of SketchLine segments instead (same sampling technique as
// TechDraw.cpp/Cam3D.cpp/Pick3D.cpp's own curve sampling), a disclosed
// approximation rather than an exact conic projection.
//
// Returns false (adding nothing to sketch) if edgeIndex is out of range.
bool projectExternalEdge(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, int tessellationSegments = 24);

// Provenance for one projectExternalEdgeTracked call: which source edge it
// came from (a TopoNaming.h EdgeFingerprint, the same recompute-survival
// re-identification Fillet/Chamfer/sketch-plane-attachment already rely
// on -- see Sketch::attachedFaceFingerprint's own comment for the same
// idea applied to a face instead of an edge) and exactly which sketch
// entities it produced, so refreshExternalGeometry can overwrite them in
// place after the source feature's shape changes instead of leaving stale
// geometry behind (this is what turns projectExternalEdge's own disclosed
// "one-shot copy, not live-linked" limitation into something explicitly
// re-syncable). Real, disclosed scope: this ref is a plain value the
// caller holds onto for the current editing session -- it is NOT stored
// inside Sketch itself or persisted by Persistence3D.h, so the link is
// lost across a save/reload, the same "session-only unless a caller
// chooses to persist it separately" scope every other typed-index
// (rather than interactively picked) reference in this codebase already
// has.
struct ExternalGeometryRef {
    EdgeFingerprint fingerprint;
    int tessellationSegments = 24;
    enum class Kind { Line, Circle, Arc, Tessellated };
    Kind kind = Kind::Line;
    // The sketch point indices this call created, in the exact order
    // projectExternalEdgeTracked itself created them (Line: [a, b]. Circle:
    // [center]. Arc: [center, start, end]. Tessellated: one per sample,
    // size == tessellationSegments + 1).
    std::vector<int> pointIndices;
    // The one SketchLine/SketchCircle/SketchArc index this call created
    // (Line/Circle/Arc kinds only; -1 for Tessellated, which instead
    // creates one SketchLine PER SEGMENT, recorded in lineIndices below in
    // the same point-to-point order tessellateIntoSketch itself walks).
    int geomIndex = -1;
    std::vector<int> lineIndices; // Tessellated kind only, size == tessellationSegments
};

// Same as projectExternalEdge, but additionally fills outRef with enough
// provenance for a later refreshExternalGeometry call to find this exact
// projection again. Returns false (touching neither sketch nor outRef)
// under the same conditions projectExternalEdge itself does.
bool projectExternalEdgeTracked(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, ExternalGeometryRef& outRef,
                                int tessellationSegments = 24);

// Re-resolves ref's original edge in currentShape (a LATER, possibly
// different, version of the same source feature's own shape) via
// TopoNaming::resolveEdgeIndex, then re-projects it onto sketch's own
// plane and overwrites the EXISTING points/radius ref recorded -- rather
// than appending new geometry the way re-running projectExternalEdge from
// scratch would. Fails, leaving sketch untouched, if currentShape has no
// edges to resolve against, or if the re-resolved edge's curve no longer
// matches ref.kind exactly (a full circle becoming trimmed, or vice
// versa, counts as a mismatch too, since SketchCircle and SketchArc are
// different entities) -- the same "don't guess, disclose and refuse"
// philosophy projectExternalEdge's own out-of-range handling already
// uses, rather than silently corrupting the sketch with a half-updated
// or wrongly-typed result.
bool refreshExternalGeometry(Sketch& sketch, const ExternalGeometryRef& ref, const TopoDS_Shape& currentShape);

} // namespace lcad
