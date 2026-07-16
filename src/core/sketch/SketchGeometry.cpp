#include "core/sketch/SketchGeometry.h"

namespace lcad {

int Sketch::addPoint(Point2D p, bool fixed) {
    m_points.push_back(p);
    m_fixed.push_back(fixed);
    return static_cast<int>(m_points.size()) - 1;
}

int Sketch::addLine(int p1, int p2, bool construction) {
    m_lines.push_back({p1, p2, construction});
    return static_cast<int>(m_lines.size()) - 1;
}

int Sketch::addCircle(int center, double radius, bool construction) {
    m_circles.push_back({center, radius, construction});
    return static_cast<int>(m_circles.size()) - 1;
}

void Sketch::addConstraint(SketchConstraint constraint) {
    m_constraints.push_back(constraint);
}

} // namespace lcad
