#pragma once

#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

// A 2D sketch, solved by ConstraintSolver.h and (in a later 3D sprint)
// extruded into a real 3D feature via Pad/Pocket. Scoped to the sketch's
// own local XY plane only -- placing a sketch on an arbitrary 3D plane is
// deeper 3D-sprint territory, matching Feature3D's own position-only (no
// rotation) placement decision.
//
// Coincidence between geometry is structural, not a solved constraint: two
// lines sharing an endpoint literally share the same point index, the way
// most real sketchers actually represent it internally, rather than a
// numeric "distance == 0" equation the solver has to satisfy exactly.

struct SketchLine {
    int p1 = -1;
    int p2 = -1;
    bool construction = false; // construction geometry: solved like real geometry, not extruded
};

struct SketchCircle {
    int center = -1;
    double radius = 10.0;
    bool construction = false;
};

enum class SketchConstraintType {
    Horizontal,   // geomA (line) is horizontal
    Vertical,     // geomA (line) is vertical
    Distance,     // |points[pointA] - points[pointB]| == value -- also how a sketch dimension is represented
    Parallel,     // geomA, geomB (lines) have parallel directions
    Perpendicular, // geomA, geomB (lines) have perpendicular directions
    Equal,        // geomA, geomB (lines) have equal length
    Tangent,      // geomA (line) is tangent to geomB (circle) -- line-circle only, not circle-circle, in this pass
    Radius,       // geomA (circle) has radius == value -- how a circle gets dimensioned
};

struct SketchConstraint {
    SketchConstraintType type = SketchConstraintType::Horizontal;
    int geomA = -1;
    int geomB = -1;
    int pointA = -1;
    int pointB = -1;
    double value = 0.0;
};

class Sketch {
public:
    int addPoint(Point2D p, bool fixed = false);
    int addLine(int p1, int p2, bool construction = false);
    int addCircle(int center, double radius, bool construction = false);
    void addConstraint(SketchConstraint constraint);

    const std::vector<Point2D>& points() const { return m_points; }
    std::vector<Point2D>& points() { return m_points; }
    const std::vector<bool>& pointFixed() const { return m_fixed; }
    const std::vector<SketchLine>& lines() const { return m_lines; }
    std::vector<SketchLine>& lines() { return m_lines; }
    const std::vector<SketchCircle>& circles() const { return m_circles; }
    std::vector<SketchCircle>& circles() { return m_circles; }
    const std::vector<SketchConstraint>& constraints() const { return m_constraints; }
    std::vector<SketchConstraint>& constraints() { return m_constraints; }

private:
    std::vector<Point2D> m_points;
    std::vector<bool> m_fixed;
    std::vector<SketchLine> m_lines;
    std::vector<SketchCircle> m_circles;
    std::vector<SketchConstraint> m_constraints;
};

} // namespace lcad
