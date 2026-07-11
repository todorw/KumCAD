#pragma once

#include "core/geometry/Point2D.h"

#include <optional>
#include <vector>

namespace lcad {

class Entity;

// Geometry behind the cursor-dependent object snaps (NEArest, PERpendicular,
// TANgent). Unlike Entity::snapCandidates(), these points depend on where the
// cursor or the command's anchor point is, so they can't be enumerated up
// front.

// Closest point on the entity's curve to pt. Lines, circles, arcs, polylines
// (straight and bulged segments), and ellipses (sampled); nullopt for other
// types.
std::optional<Point2D> nearestPointOnEntity(const Entity& e, const Point2D& pt);

// Feet of perpendiculars dropped from 'from' onto the entity's curve (lines,
// circles, arcs, polyline segments).
std::vector<Point2D> perpendicularPoints(const Entity& e, const Point2D& from);

// Points where a line from 'from' would touch the entity tangentially
// (circles and arcs; empty when 'from' is inside the circle).
std::vector<Point2D> tangentPoints(const Entity& e, const Point2D& from);

} // namespace lcad
