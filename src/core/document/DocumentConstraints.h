#pragma once

#include "core/Ids.h"
#include "core/sketch/SketchGeometry.h"

#include <vector>

namespace lcad {

class Document;

// One constrainable point on a Document entity: a LineEntity's start (0)
// or end (1), a CircleEntity's center (0, its only constrainable point),
// an ArcEntity's start (0), end (1), or center (2), or a PointEntity's own
// position (0).
struct DocumentPointRef {
    EntityId entityId = 0;
    int pointIndex = 0;
};

// A constraint applied directly to Document entities rather than to
// core3d's separate Sketch concept -- see solveDocumentConstraints's own
// comment for why this exists and what it can't do yet. geomA/geomB name
// a Line/Circle entity directly (matching SketchConstraintType's own
// per-type meaning of "geomA (line)"/"geomA (circle)" etc.); pointA/
// pointB name a point on any supported entity, used by the constraint
// types that take point arguments (Distance, PointOnLine/PointOnCircle's
// own point, Midpoint's point, Symmetric's two points).
struct DocumentConstraint {
    SketchConstraintType type = SketchConstraintType::Horizontal;
    EntityId geomA = 0, geomB = 0;
    DocumentPointRef pointA, pointB;
    double value = 0.0;
};

struct DocumentConstraintResult {
    bool converged = false;
    double finalResidualNorm = 0.0;
};

// Applies core/sketch's real constraint solver (ConstraintSolver.h)
// directly to ordinary Document Line/Circle/Point entities -- AutoCAD's
// own "Parametric" ribbon adds geometric/dimensional constraints
// straight onto plain drawing geometry, not just inside a separate
// sketch-for-later-extrusion concept (core3d's Sketch, which
// core/sketch/SketchGeometry.h was originally built for). This reuses
// that exact solver rather than writing a second one.
//
// Builds a throwaway Sketch under the hood: every referenced entity's
// own point(s) become Sketch points, merged into the SAME Sketch point
// index whenever two entities' points already coincide within
// snapTolerance (automatic structural coincidence by position -- the
// same "these are already touching" convention a real 2D CAD's
// parametric solver uses, and consistent with how core3d's own Sketch
// treats coincidence as structural rather than a solved equation). None
// of the built points are fixed, so a constraint set with no absolute
// anchor is free to translate/rotate as a whole while still satisfying
// every relative constraint -- matching how applying a Horizontal
// constraint in a real CAD tool doesn't itself pin the line in place.
// Reuses solveSketch as-is, then writes the solved positions back into
// the SAME Document entities via their own moveGripPoint.
//
// ArcEntity (center, radius, startAngle, endAngle) bridges to Sketch's own
// SketchArc (independent start/end points plus a solver-driven radius DOF,
// kept consistent with center by ConstraintSolver's internal residual) by
// registering all three of the arc's points (start, end, center) and
// writing the solved radius back via ArcEntity::setRadius -- see
// DocumentConstraints.cpp's writeBack for the exact order (center grip
// first, so the start/end angle recompute in terms of the final center).
// This makes ArcRadius, EqualArcRadius, and any other constraint type that
// takes an arc's geomA/geomB usable from the plain-Document "Parametric"
// path, not just core3d's separate Sketch concept.
//
// Any constraint referencing an entity that still isn't Line/Circle/Arc/
// Point is silently skipped, along with the points/entities only it
// referenced.
DocumentConstraintResult solveDocumentConstraints(Document& doc, const std::vector<DocumentConstraint>& constraints,
                                                  double snapTolerance = 1e-6);

} // namespace lcad
