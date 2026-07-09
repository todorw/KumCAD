#pragma once

#include <cmath>

namespace lcad {

struct Point2D {
    double x = 0.0;
    double y = 0.0;

    Point2D() = default;
    Point2D(double x_, double y_) : x(x_), y(y_) {}

    Point2D operator+(const Point2D& o) const { return {x + o.x, y + o.y}; }
    Point2D operator-(const Point2D& o) const { return {x - o.x, y - o.y}; }
    Point2D operator*(double s) const { return {x * s, y * s}; }

    double dot(const Point2D& o) const { return x * o.x + y * o.y; }
    double length() const { return std::sqrt(x * x + y * y); }
    double distanceTo(const Point2D& o) const { return (*this - o).length(); }
};

inline Point2D rotateAround(const Point2D& p, const Point2D& center, double angleRadians) {
    const Point2D d = p - center;
    const double c = std::cos(angleRadians);
    const double s = std::sin(angleRadians);
    return center + Point2D(d.x * c - d.y * s, d.x * s + d.y * c);
}

inline Point2D scaleAround(const Point2D& p, const Point2D& center, double factor) {
    return center + (p - center) * factor;
}

// Reflects p across the (infinite) line through a and b. If a == b the line is
// degenerate; p is returned unchanged.
inline Point2D mirrorAcross(const Point2D& p, const Point2D& a, const Point2D& b) {
    const Point2D d = b - a;
    const double lenSq = d.dot(d);
    if (lenSq < 1e-12) return p;
    const Point2D proj = a + d * ((p - a).dot(d) / lenSq);
    return proj * 2.0 - p;
}

} // namespace lcad
