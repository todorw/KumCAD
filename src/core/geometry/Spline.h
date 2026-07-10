#pragma once

#include "core/geometry/Entity.h"

#include <vector>

namespace lcad {

// Non-rational B-spline curve, the AutoCAD SPLINE entity. Stored in the same
// form DXF uses: degree + control points + clamped knot vector, plus the fit
// points the user picked (when the spline was drawn through points rather
// than defined by its control polygon).
//
// The SPLINE command builds one from fit points via fromFitPoints(), which
// computes the degree-3 (lower for few points) interpolating B-spline with
// chord-length parameterization -- the same construction AutoCAD uses.
class SplineEntity : public Entity {
public:
    SplineEntity(EntityId id, LayerId layer, int degree, std::vector<Point2D> controlPoints,
                 std::vector<double> knots, std::vector<Point2D> fitPoints = {});

    // Interpolating spline through the given points (needs >= 2, degree
    // min(3, count-1)). Returns nullptr when the input is degenerate.
    static std::unique_ptr<SplineEntity> fromFitPoints(EntityId id, LayerId layer, std::vector<Point2D> fitPoints);

    int degree() const { return m_degree; }
    const std::vector<Point2D>& controlPoints() const { return m_controlPoints; }
    const std::vector<double>& knots() const { return m_knots; }
    const std::vector<Point2D>& fitPoints() const { return m_fitPoints; }

    // Point on the curve at parameter u (clamped to the knot range).
    Point2D evaluate(double u) const;

    // The curve approximated as a polyline with `count` points, for drawing,
    // distance queries, and intersections.
    std::vector<Point2D> sample(int count = 64) const;

    EntityType type() const override { return EntityType::Spline; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    void mirror(const Point2D& a, const Point2D& b) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::vector<SnapPoint> snapCandidates() const override;
    std::unique_ptr<Entity> clone() const override;

private:
    // Re-runs fit-point interpolation after a grip edit.
    void refitFromFitPoints();

    int m_degree;
    std::vector<Point2D> m_controlPoints;
    std::vector<double> m_knots; // clamped, size = controlPoints + degree + 1
    std::vector<Point2D> m_fitPoints;
};

} // namespace lcad
