#include "core/sketch/SketchGeometry.h"

#include <cmath>

namespace lcad {

namespace {

constexpr double kPi = 3.14159265358979323846;

Point3D cross3(const Point3D& a, const Point3D& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Point3D normalize3(const Point3D& v) {
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12) return v;
    return {v.x / len, v.y / len, v.z / len};
}

// Builds a base plane from its (un-rotated) normal/xAxis0/yAxis0 and an
// origin direction the offset moves along (always the plane's own
// normal), rotating xAxis0 toward yAxis0 by angleDegrees within the
// plane to apply the attachment angle.
SketchPlane basePlane(Point3D normal, Point3D xAxis0, Point3D yAxis0, double offset, double angleDegrees) {
    SketchPlane plane;
    plane.normal = normal;
    plane.origin = {normal.x * offset, normal.y * offset, normal.z * offset};
    const double rad = angleDegrees * kPi / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);
    plane.xAxis = normalize3({xAxis0.x * c + yAxis0.x * s, xAxis0.y * c + yAxis0.y * s, xAxis0.z * c + yAxis0.z * s});
    return plane;
}

} // namespace

SketchPlane SketchPlane::XY(double offset, double angleDegrees) {
    return basePlane({0, 0, 1}, {1, 0, 0}, {0, 1, 0}, offset, angleDegrees);
}

SketchPlane SketchPlane::XZ(double offset, double angleDegrees) {
    return basePlane({0, -1, 0}, {1, 0, 0}, {0, 0, 1}, offset, angleDegrees);
}

SketchPlane SketchPlane::YZ(double offset, double angleDegrees) {
    return basePlane({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, offset, angleDegrees);
}

Point3D SketchPlane::yAxis() const {
    return normalize3(cross3(normal, xAxis));
}

Point3D SketchPlane::toWorld(const Point2D& local) const {
    const Point3D y = yAxis();
    return {origin.x + xAxis.x * local.x + y.x * local.y, origin.y + xAxis.y * local.x + y.y * local.y,
           origin.z + xAxis.z * local.x + y.z * local.y};
}

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

int Sketch::addArc(int center, int start, int end, double radius, bool ccw, bool construction) {
    m_arcs.push_back({center, start, end, radius, ccw, construction});
    return static_cast<int>(m_arcs.size()) - 1;
}

void Sketch::addConstraint(SketchConstraint constraint) {
    m_constraints.push_back(constraint);
}

} // namespace lcad
