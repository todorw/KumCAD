#pragma once

#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

class Entity;

// Intersection points between the finite curves of two entities. Supported
// types: Line, Polyline (per segment), Circle, Arc; other types yield no
// points. Tangencies report a single point; overlapping collinear segments
// report none (no unique point).
std::vector<Point2D> intersectEntities(const Entity& a, const Entity& b);

// Intersections of the INFINITE line through a and b with an entity's finite
// curves -- what EXTEND needs to find a boundary beyond an endpoint.
std::vector<Point2D> intersectInfiniteLineEntity(const Point2D& a, const Point2D& b, const Entity& entity);

// Primitive helpers, exposed for tests.
std::vector<Point2D> intersectSegmentSegment(const Point2D& a1, const Point2D& a2, const Point2D& b1,
                                             const Point2D& b2);
std::vector<Point2D> intersectSegmentCircle(const Point2D& a1, const Point2D& a2, const Point2D& center,
                                            double radius);
std::vector<Point2D> intersectCircleCircle(const Point2D& c1, double r1, const Point2D& c2, double r2);

} // namespace lcad
