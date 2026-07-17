#pragma once

#include "core/Ids.h"
#include "core/sketch/SketchGeometry.h"

#include <vector>

namespace lcad {

class Document;

// One constrainable point on a Document entity: a LineEntity's start (0)
// or end (1), a CircleEntity's center (0, its only constrainable point),
// or a PointEntity's own position (0).
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
// Disclosed scope cut: Arc entities aren't supported. ArcEntity stores
// (center, radius, startAngle, endAngle) with no direct radius setter in
// its own public grip API, while Sketch's own SketchArc models start/end
// as independent points with their own solver-driven radius DOF --
// bridging the two representations would need a new ArcEntity setter
// this pass doesn't add. Any constraint referencing an unsupported
// entity (Arc, or anything that isn't Line/Circle/Point) is silently
// skipped, along with the points/entities only it referenced.
DocumentConstraintResult solveDocumentConstraints(Document& doc, const std::vector<DocumentConstraint>& constraints,
                                                  double snapTolerance = 1e-6);

} // namespace lcad
