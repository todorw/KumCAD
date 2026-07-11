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
// entity's own direction (negative shortens). Lines and arcs only. Returns
// nullptr when unsupported or the result would be degenerate (zero-length
// line, arc sweep outside (0, 2pi)).
std::unique_ptr<Entity> lengthenedClone(const Entity& e, const Point2D& pickPt, double deltaLen);

// BREAK: the pieces remaining after removing the stretch between a and b
// (both projected onto the entity's curve). Lines and arcs yield up to two
// pieces; a circle yields one arc running CCW from b around to a (AutoCAD
// removes the CCW stretch from the first to the second point); straight
// polylines split into two chains. a == b splits at a point without removing
// material. makeId supplies ids for the new pieces. Empty result plus
// ok=false means the entity type (or an arc-segment polyline) isn't
// breakable.
struct BreakResult {
    bool ok = false;
    std::vector<std::unique_ptr<Entity>> pieces;
};
BreakResult breakEntity(const Entity& e, const Point2D& a, const Point2D& b,
                        const std::function<EntityId()>& makeId);

} // namespace lcad
