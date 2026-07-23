#include "core/sketch/SketchGeometry.h"

#include <algorithm>
#include <cmath>
#include <optional>

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

int Sketch::addSpline(std::vector<int> controlPoints, bool construction) {
    m_splines.push_back({std::move(controlPoints), construction});
    return static_cast<int>(m_splines.size()) - 1;
}

void Sketch::addConstraint(SketchConstraint constraint) {
    m_constraints.push_back(constraint);
}

namespace {

Point2D normalized2(const Point2D& v) {
    const double len = v.length();
    return len > 1e-12 ? v * (1.0 / len) : Point2D(1, 0);
}

// Intersection of the two INFINITE lines through (a1,a2) and (b1,b2);
// nullopt if parallel -- same math as the 2D FILLET command's own
// infiniteIntersection, kept independent since that one works on
// LineEntity rather than raw points.
std::optional<Point2D> infiniteIntersection(const Point2D& a1, const Point2D& a2, const Point2D& b1,
                                            const Point2D& b2) {
    const Point2D r = a2 - a1;
    const Point2D s = b2 - b1;
    const double denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-12) return std::nullopt;
    const Point2D qp = b1 - a1;
    const double t = (qp.x * s.y - qp.y * s.x) / denom;
    return a1 + r * t;
}

} // namespace

bool sketchFillet(Sketch& sketch, int lineAIndex, int lineBIndex, double radius) {
    if (lineAIndex < 0 || lineBIndex < 0 || lineAIndex == lineBIndex ||
        static_cast<std::size_t>(lineAIndex) >= sketch.lines().size() ||
        static_cast<std::size_t>(lineBIndex) >= sketch.lines().size() || radius <= 1e-9) {
        return false;
    }

    // Copy by value: indices/point positions below get read repeatedly
    // while m_points is also being mutated (new points appended), so a
    // reference into m_lines could dangle across those pushes.
    const SketchLine lineA = sketch.lines()[static_cast<std::size_t>(lineAIndex)];
    const SketchLine lineB = sketch.lines()[static_cast<std::size_t>(lineBIndex)];

    // Which endpoint (p1 or p2, as a bool "isP2") of each line is nearest
    // the corner, and the shared point index if the lines already meet
    // structurally -- checked before computing any geometry so the
    // structural case doesn't depend on floating-point proximity at all.
    int sharedPoint = -1;
    bool nearAIsP2 = false, nearBIsP2 = false;
    if (lineA.p1 == lineB.p1) { sharedPoint = lineA.p1; nearAIsP2 = false; nearBIsP2 = false; }
    else if (lineA.p1 == lineB.p2) { sharedPoint = lineA.p1; nearAIsP2 = false; nearBIsP2 = true; }
    else if (lineA.p2 == lineB.p1) { sharedPoint = lineA.p2; nearAIsP2 = true; nearBIsP2 = false; }
    else if (lineA.p2 == lineB.p2) { sharedPoint = lineA.p2; nearAIsP2 = true; nearBIsP2 = true; }

    Point2D corner;
    int nearAIndex, farAIndex, nearBIndex, farBIndex;
    if (sharedPoint >= 0) {
        corner = sketch.points()[static_cast<std::size_t>(sharedPoint)];
        nearAIndex = sharedPoint;
        farAIndex = nearAIsP2 ? lineA.p1 : lineA.p2;
        nearBIndex = sharedPoint;
        farBIndex = nearBIsP2 ? lineB.p1 : lineB.p2;
    } else {
        const Point2D a1 = sketch.points()[static_cast<std::size_t>(lineA.p1)];
        const Point2D a2 = sketch.points()[static_cast<std::size_t>(lineA.p2)];
        const Point2D b1 = sketch.points()[static_cast<std::size_t>(lineB.p1)];
        const Point2D b2 = sketch.points()[static_cast<std::size_t>(lineB.p2)];
        const auto cornerOpt = infiniteIntersection(a1, a2, b1, b2);
        if (!cornerOpt) return false; // parallel: no corner to round
        corner = *cornerOpt;
        nearAIsP2 = corner.distanceTo(a2) < corner.distanceTo(a1);
        nearAIndex = nearAIsP2 ? lineA.p2 : lineA.p1;
        farAIndex = nearAIsP2 ? lineA.p1 : lineA.p2;
        nearBIsP2 = corner.distanceTo(b2) < corner.distanceTo(b1);
        nearBIndex = nearBIsP2 ? lineB.p2 : lineB.p1;
        farBIndex = nearBIsP2 ? lineB.p1 : lineB.p2;
    }

    const Point2D farA = sketch.points()[static_cast<std::size_t>(farAIndex)];
    const Point2D farB = sketch.points()[static_cast<std::size_t>(farBIndex)];

    const Point2D dA = normalized2(farA - corner);
    const Point2D dB = normalized2(farB - corner);
    const double cosAngle = std::clamp(dA.dot(dB), -1.0, 1.0);
    const double angle = std::acos(cosAngle);
    if (angle < 1e-6 || angle > kPi - 1e-6) return false; // collinear: no corner to round

    const double tangentDist = radius / std::tan(angle / 2.0);
    if (tangentDist > corner.distanceTo(farA) - 1e-9 || tangentDist > corner.distanceTo(farB) - 1e-9) {
        return false; // radius too large for these lines
    }

    const Point2D tA = corner + dA * tangentDist;
    const Point2D tB = corner + dB * tangentDist;
    const Point2D bisector = normalized2(dA + dB);
    const Point2D arcCenter = corner + bisector * (radius / std::sin(angle / 2.0));

    // Move (or split off) each line's near endpoint to its own tangent
    // point. If the corner was structural, lineA keeps the original
    // shared point index (moved to tA) and lineB gets a brand-new point
    // for tB, so the two lines are no longer coincident at that vertex --
    // deliberate: the new arc becomes what connects them instead.
    sketch.points()[static_cast<std::size_t>(nearAIndex)] = tA;
    int tangentBIndex = nearBIndex;
    if (sharedPoint >= 0) {
        tangentBIndex = sketch.addPoint(tB, false);
        if (nearBIsP2) sketch.lines()[static_cast<std::size_t>(lineBIndex)].p2 = tangentBIndex;
        else sketch.lines()[static_cast<std::size_t>(lineBIndex)].p1 = tangentBIndex;
    } else {
        sketch.points()[static_cast<std::size_t>(nearBIndex)] = tB;
    }

    const int centerIndex = sketch.addPoint(arcCenter, false);

    double angleA = std::atan2(tA.y - arcCenter.y, tA.x - arcCenter.x);
    double angleB = std::atan2(tB.y - arcCenter.y, tB.x - arcCenter.x);
    double sweep = std::fmod(angleB - angleA, 2.0 * kPi);
    if (sweep < 0) sweep += 2.0 * kPi;
    // The fillet is the short way around; if the short way runs from tB
    // to tA instead, swap which point is the arc's start so ccw=true
    // still sweeps the short (fillet) arc rather than the long way.
    if (sweep > kPi) sketch.addArc(centerIndex, tangentBIndex, nearAIndex, radius, true, false);
    else sketch.addArc(centerIndex, nearAIndex, tangentBIndex, radius, true, false);

    return true;
}

Point2D evaluateSketchSpline(const std::vector<Point2D>& controlPoints, double t) {
    const int n = static_cast<int>(controlPoints.size());
    if (n == 0) return Point2D();
    if (n == 1) return controlPoints[0];
    if (t <= 0.0) return controlPoints.front();
    if (t >= 1.0) return controlPoints.back(); // sidesteps the zero-width final knot span below

    const int degree = std::min(3, n - 1);
    const int numSpans = n - degree;
    const int knotCount = n + degree + 1;
    std::vector<double> knots(static_cast<std::size_t>(knotCount));
    for (int i = 0; i < knotCount; ++i) {
        if (i <= degree) knots[static_cast<std::size_t>(i)] = 0.0;
        else if (i >= n) knots[static_cast<std::size_t>(i)] = static_cast<double>(numSpans);
        else knots[static_cast<std::size_t>(i)] = static_cast<double>(i - degree);
    }

    const double u = t * static_cast<double>(numSpans);

    // Cox-de Boor basis functions, built bottom-up from degree 0 -- each
    // level p holds N_{i,p}(u) for consecutive i, one shorter than the
    // level before (degree 0 has knotCount-1 entries, matching the
    // number of knot spans). Safe against the final zero-width knot span
    // only because t>=1 is handled above; every remaining span here has
    // positive width, so the half-open interval test is unambiguous.
    std::vector<double> basis(static_cast<std::size_t>(knotCount - 1), 0.0);
    for (int i = 0; i < knotCount - 1; ++i) {
        if (u >= knots[static_cast<std::size_t>(i)] && u < knots[static_cast<std::size_t>(i + 1)]) {
            basis[static_cast<std::size_t>(i)] = 1.0;
        }
    }
    for (int p = 1; p <= degree; ++p) {
        std::vector<double> next(static_cast<std::size_t>(knotCount - 1 - p), 0.0);
        for (int i = 0; i < knotCount - 1 - p; ++i) {
            double left = 0.0, right = 0.0;
            const double denomL = knots[static_cast<std::size_t>(i + p)] - knots[static_cast<std::size_t>(i)];
            if (denomL > 1e-12) {
                left = (u - knots[static_cast<std::size_t>(i)]) / denomL * basis[static_cast<std::size_t>(i)];
            }
            const double denomR = knots[static_cast<std::size_t>(i + p + 1)] - knots[static_cast<std::size_t>(i + 1)];
            if (denomR > 1e-12) {
                right = (knots[static_cast<std::size_t>(i + p + 1)] - u) / denomR * basis[static_cast<std::size_t>(i + 1)];
            }
            next[static_cast<std::size_t>(i)] = left + right;
        }
        basis = std::move(next);
    }

    // basis now holds N_{i,degree}(u) for i in [0,n), one per control point.
    Point2D result(0.0, 0.0);
    for (int i = 0; i < n; ++i) result = result + controlPoints[static_cast<std::size_t>(i)] * basis[static_cast<std::size_t>(i)];
    return result;
}

SketchConstraint makeFixConstraint(const Sketch& sketch, int pointIndex) {
    SketchConstraint c;
    c.type = SketchConstraintType::Fix;
    c.pointA = pointIndex;
    const Point2D& p = sketch.points()[static_cast<std::size_t>(pointIndex)];
    c.value = p.x;
    c.value2 = p.y;
    return c;
}

} // namespace lcad
