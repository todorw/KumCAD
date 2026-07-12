#pragma once

#include "core/geometry/BoundingBox.h"
#include "core/geometry/Entity.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace lcad {

// STRETCH: a clone of the entity with the defining points that fall inside
// window shifted by delta (an entity whose every defining point is inside
// moves rigidly, matching AutoCAD). Returns nullptr when the window touches
// none of the entity's defining points. Entities whose shape can't stretch
// partially (circles, ellipses, text, inserts) translate when their primary
// point is inside and are untouched otherwise. Arcs keep center and radius;
// an endpoint inside the window slides along the arc toward the shifted
// position (angle-only), like a grip edit.
std::unique_ptr<Entity> stretchedClone(const Entity& e, const BoundingBox& window, const Point2D& delta);

// Total curve length, for LENGTHEN's Total/Percent bases: lines, arcs, and
// open polylines. nullopt for closed or unsupported entities.
std::optional<double> curveLength(const Entity& e);

// LENGTHEN: a clone with the end nearer pickPt extended by deltaLen along the
// entity's own direction (negative shortens). Lines and arcs directly; for an
// open polyline, only its terminal segment nearer pickPt changes (straight or
// bulged, keeping a bulged segment's center/radius like the Arc case).
// Returns nullptr when unsupported or the result would be degenerate
// (zero-length line, arc/bulge sweep outside (0, 2pi)).
std::unique_ptr<Entity> lengthenedClone(const Entity& e, const Point2D& pickPt, double deltaLen);

// BREAK: the pieces remaining after removing the stretch between a and b
// (both projected onto the entity's curve). Lines and arcs yield up to two
// pieces; a circle yields one arc running CCW from b around to a (AutoCAD
// removes the CCW stretch from the first to the second point); open
// polylines split into two chains, preserving bulges (arc segments) on
// either side and recomputing the bulge of whichever sub-arc got split. a ==
// b splits at a point without removing material. makeId supplies ids for the
// new pieces. Empty result plus ok=false means the entity type (or a closed
// polyline) isn't breakable.
struct BreakResult {
    bool ok = false;
    std::vector<std::unique_ptr<Entity>> pieces;
};
BreakResult breakEntity(const Entity& e, const Point2D& a, const Point2D& b,
                        const std::function<EntityId()>& makeId);

// The point at arc-length s from the start of the entity's curve (lines,
// arcs, circles from angle 0, polylines including bulges). nullopt when
// unsupported or s is outside the curve.
std::optional<Point2D> pointAtDistance(const Entity& e, double s);

// DIVIDE: the n-1 interior division points splitting the curve into n equal
// arc-length parts (n points around a circle, matching AutoCAD). Empty when
// the entity can't be divided or n < 2.
std::vector<Point2D> divideEntity(const Entity& e, int n);

// MEASURE: points every step along the curve from its start (excluding the
// start itself). Empty when unsupported or step is not positive.
std::vector<Point2D> measureEntity(const Entity& e, double step);

// AutoCAD Express Tools' OVERKILL, simplified: indices into entities whose
// geometry exactly duplicates an earlier entity in the list (same concrete
// type, same layer, same defining geometry within tolerance -- a Line's
// endpoints may be swapped, an Arc's start/end likewise via its sweep
// direction not being compared). Always the LATER occurrence in a duplicate
// pair or chain, so callers keep the first and delete the rest. Only
// Line/Circle/Arc/Polyline are compared, the types where "duplicate" has an
// unambiguous definition without also touching color/linetype/width
// overrides the way real OVERKILL's property-matching options do; every
// other type never comes back flagged, even if genuinely identical.
std::vector<std::size_t> findDuplicateEntities(const std::vector<const Entity*>& entities, double tolerance = 1e-6);

} // namespace lcad
