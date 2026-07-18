#pragma once

#include "core/sketch/SketchGeometry.h"

#include <TopoDS_Shape.hxx>

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
// becomes one SketchLine between two new projected points. A full
// circular edge (GeomAbs_Circle, untrimmed) whose own axis is parallel
// to sketch's normal projects to an exact SketchCircle at the projected
// center with the circle's own true radius -- an oblique circle would
// distort into an ellipse under orthogonal projection, which
// SketchGeometry.h has no representation for. Everything else (arcs,
// non-parallel circles, ellipses, B-splines) is tessellated into a
// polyline of SketchLine segments instead (same sampling technique as
// TechDraw.cpp/Cam3D.cpp/Pick3D.cpp's own curve sampling), a disclosed
// approximation rather than an exact conic projection.
//
// Returns false (adding nothing to sketch) if edgeIndex is out of range.
bool projectExternalEdge(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, int tessellationSegments = 24);

} // namespace lcad
