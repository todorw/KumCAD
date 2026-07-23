#include "core/sketch/ConstraintSolver.h"

#include "core/sketch/LinearSolve.h"

#include <algorithm>
#include <cmath>

namespace lcad {

namespace {

// Maps each free (non-fixed) point to a pair of consecutive variable slots
// (x, y) and each circle to one variable slot (radius); fixed points have
// no slot at all and always read from the sketch's stored value.
class VariableMap {
public:
    explicit VariableMap(const Sketch& sketch)
        : m_pointVar(sketch.points().size(), -1), m_circleVar(sketch.circles().size(), -1),
          m_arcVar(sketch.arcs().size(), -1) {
        int next = 0;
        for (std::size_t i = 0; i < sketch.points().size(); ++i) {
            if (!sketch.pointFixed()[i]) {
                m_pointVar[i] = next;
                next += 2;
            }
        }
        for (std::size_t i = 0; i < sketch.circles().size(); ++i) {
            m_circleVar[i] = next;
            ++next;
        }
        for (std::size_t i = 0; i < sketch.arcs().size(); ++i) {
            m_arcVar[i] = next;
            ++next;
        }
        m_size = next;
    }

    int size() const { return m_size; }
    int pointVar(int pointIndex) const { return m_pointVar[static_cast<std::size_t>(pointIndex)]; }
    int circleVar(int circleIndex) const { return m_circleVar[static_cast<std::size_t>(circleIndex)]; }
    int arcVar(int arcIndex) const { return m_arcVar[static_cast<std::size_t>(arcIndex)]; }

private:
    std::vector<int> m_pointVar;
    std::vector<int> m_circleVar;
    std::vector<int> m_arcVar;
    int m_size = 0;
};

std::vector<double> initialVector(const Sketch& sketch, const VariableMap& vars) {
    std::vector<double> x(static_cast<std::size_t>(vars.size()));
    for (std::size_t i = 0; i < sketch.points().size(); ++i) {
        const int v = vars.pointVar(static_cast<int>(i));
        if (v >= 0) {
            x[static_cast<std::size_t>(v)] = sketch.points()[i].x;
            x[static_cast<std::size_t>(v) + 1] = sketch.points()[i].y;
        }
    }
    for (std::size_t i = 0; i < sketch.circles().size(); ++i) {
        const int v = vars.circleVar(static_cast<int>(i));
        if (v >= 0) x[static_cast<std::size_t>(v)] = sketch.circles()[i].radius;
    }
    for (std::size_t i = 0; i < sketch.arcs().size(); ++i) {
        const int v = vars.arcVar(static_cast<int>(i));
        if (v >= 0) x[static_cast<std::size_t>(v)] = sketch.arcs()[i].radius;
    }
    return x;
}

void applyVector(Sketch& sketch, const VariableMap& vars, const std::vector<double>& x) {
    for (std::size_t i = 0; i < sketch.points().size(); ++i) {
        const int v = vars.pointVar(static_cast<int>(i));
        if (v >= 0) sketch.points()[i] = Point2D(x[static_cast<std::size_t>(v)], x[static_cast<std::size_t>(v) + 1]);
    }
    for (std::size_t i = 0; i < sketch.circles().size(); ++i) {
        const int v = vars.circleVar(static_cast<int>(i));
        if (v >= 0) sketch.circles()[i].radius = x[static_cast<std::size_t>(v)];
    }
    for (std::size_t i = 0; i < sketch.arcs().size(); ++i) {
        const int v = vars.arcVar(static_cast<int>(i));
        if (v >= 0) sketch.arcs()[i].radius = x[static_cast<std::size_t>(v)];
    }
}

Point2D pointAt(const Sketch& sketch, const VariableMap& vars, int index, const std::vector<double>& x) {
    const int v = vars.pointVar(index);
    if (v >= 0) return Point2D(x[static_cast<std::size_t>(v)], x[static_cast<std::size_t>(v) + 1]);
    return sketch.points()[static_cast<std::size_t>(index)];
}

double radiusAt(const Sketch& sketch, const VariableMap& vars, int index, const std::vector<double>& x) {
    const int v = vars.circleVar(index);
    if (v >= 0) return x[static_cast<std::size_t>(v)];
    return sketch.circles()[static_cast<std::size_t>(index)].radius;
}

double arcRadiusAt(const Sketch& sketch, const VariableMap& vars, int index, const std::vector<double>& x) {
    const int v = vars.arcVar(index);
    if (v >= 0) return x[static_cast<std::size_t>(v)];
    return sketch.arcs()[static_cast<std::size_t>(index)].radius;
}

// Perpendicular distance from c to the infinite line through p1-p2.
double pointToLineDistance(const Point2D& p1, const Point2D& p2, const Point2D& c) {
    const Point2D dir = p2 - p1;
    const double len = dir.length();
    if (len < 1e-12) return c.distanceTo(p1);
    return std::abs(dir.x * (c.y - p1.y) - dir.y * (c.x - p1.x)) / len;
}

// Every constraint type contributes exactly 1 scalar residual, except
// Midpoint and Symmetric which pin down a full 2D relationship (x and y,
// or on-axis and perpendicular) and so need 2 -- computeResidual, the m
// count in solveSketch, and analyzeDof must all agree with this.
int equationCount(SketchConstraintType type) {
    return (type == SketchConstraintType::Midpoint || type == SketchConstraintType::Symmetric ||
            type == SketchConstraintType::Fix)
               ? 2
               : 1;
}

int totalEquationCount(const Sketch& sketch) {
    int total = 0;
    for (const SketchConstraint& c : sketch.constraints()) total += equationCount(c.type);
    return total + 2 * static_cast<int>(sketch.arcs().size());
}

std::vector<double> computeResidual(const Sketch& sketch, const VariableMap& vars, const std::vector<double>& x) {
    std::vector<double> r;
    r.reserve(sketch.constraints().size());

    for (const SketchConstraint& c : sketch.constraints()) {
        switch (c.type) {
        case SketchConstraintType::Horizontal: {
            const SketchLine& line = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const Point2D p1 = pointAt(sketch, vars, line.p1, x);
            const Point2D p2 = pointAt(sketch, vars, line.p2, x);
            r.push_back(p2.y - p1.y);
            break;
        }
        case SketchConstraintType::Vertical: {
            const SketchLine& line = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const Point2D p1 = pointAt(sketch, vars, line.p1, x);
            const Point2D p2 = pointAt(sketch, vars, line.p2, x);
            r.push_back(p2.x - p1.x);
            break;
        }
        case SketchConstraintType::Distance: {
            const Point2D a = pointAt(sketch, vars, c.pointA, x);
            const Point2D b = pointAt(sketch, vars, c.pointB, x);
            r.push_back(a.distanceTo(b) - c.value);
            break;
        }
        case SketchConstraintType::DistanceX: {
            const Point2D a = pointAt(sketch, vars, c.pointA, x);
            const Point2D b = pointAt(sketch, vars, c.pointB, x);
            r.push_back((b.x - a.x) - c.value);
            break;
        }
        case SketchConstraintType::DistanceY: {
            const Point2D a = pointAt(sketch, vars, c.pointA, x);
            const Point2D b = pointAt(sketch, vars, c.pointB, x);
            r.push_back((b.y - a.y) - c.value);
            break;
        }
        case SketchConstraintType::Parallel: {
            const SketchLine& la = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const SketchLine& lb = sketch.lines()[static_cast<std::size_t>(c.geomB)];
            const Point2D a1 = pointAt(sketch, vars, la.p1, x), a2 = pointAt(sketch, vars, la.p2, x);
            const Point2D b1 = pointAt(sketch, vars, lb.p1, x), b2 = pointAt(sketch, vars, lb.p2, x);
            const Point2D da = a2 - a1, db = b2 - b1;
            r.push_back(da.x * db.y - da.y * db.x);
            break;
        }
        case SketchConstraintType::Perpendicular: {
            const SketchLine& la = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const SketchLine& lb = sketch.lines()[static_cast<std::size_t>(c.geomB)];
            const Point2D a1 = pointAt(sketch, vars, la.p1, x), a2 = pointAt(sketch, vars, la.p2, x);
            const Point2D b1 = pointAt(sketch, vars, lb.p1, x), b2 = pointAt(sketch, vars, lb.p2, x);
            const Point2D da = a2 - a1, db = b2 - b1;
            r.push_back(da.x * db.x + da.y * db.y);
            break;
        }
        case SketchConstraintType::Equal: {
            const SketchLine& la = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const SketchLine& lb = sketch.lines()[static_cast<std::size_t>(c.geomB)];
            const Point2D a1 = pointAt(sketch, vars, la.p1, x), a2 = pointAt(sketch, vars, la.p2, x);
            const Point2D b1 = pointAt(sketch, vars, lb.p1, x), b2 = pointAt(sketch, vars, lb.p2, x);
            r.push_back(a1.distanceTo(a2) - b1.distanceTo(b2));
            break;
        }
        case SketchConstraintType::EqualCircleRadius: {
            r.push_back(radiusAt(sketch, vars, c.geomA, x) - radiusAt(sketch, vars, c.geomB, x));
            break;
        }
        case SketchConstraintType::EqualArcRadius: {
            r.push_back(arcRadiusAt(sketch, vars, c.geomA, x) - arcRadiusAt(sketch, vars, c.geomB, x));
            break;
        }
        case SketchConstraintType::Radius: {
            r.push_back(radiusAt(sketch, vars, c.geomA, x) - c.value);
            break;
        }
        case SketchConstraintType::Diameter: {
            r.push_back(2.0 * radiusAt(sketch, vars, c.geomA, x) - c.value);
            break;
        }
        case SketchConstraintType::Tangent: {
            const SketchLine& line = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const SketchCircle& circle = sketch.circles()[static_cast<std::size_t>(c.geomB)];
            const Point2D p1 = pointAt(sketch, vars, line.p1, x);
            const Point2D p2 = pointAt(sketch, vars, line.p2, x);
            const Point2D center = pointAt(sketch, vars, circle.center, x);
            const double radius = radiusAt(sketch, vars, c.geomB, x);
            r.push_back(pointToLineDistance(p1, p2, center) - radius);
            break;
        }
        case SketchConstraintType::ArcRadius: {
            r.push_back(arcRadiusAt(sketch, vars, c.geomA, x) - c.value);
            break;
        }
        case SketchConstraintType::TangentCircleCircle: {
            const SketchCircle& a = sketch.circles()[static_cast<std::size_t>(c.geomA)];
            const SketchCircle& b = sketch.circles()[static_cast<std::size_t>(c.geomB)];
            const Point2D centerA = pointAt(sketch, vars, a.center, x);
            const Point2D centerB = pointAt(sketch, vars, b.center, x);
            const double radiusA = radiusAt(sketch, vars, c.geomA, x);
            const double radiusB = radiusAt(sketch, vars, c.geomB, x);
            r.push_back(centerA.distanceTo(centerB) - (radiusA + radiusB));
            break;
        }
        case SketchConstraintType::Angle: {
            const SketchLine& la = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const SketchLine& lb = sketch.lines()[static_cast<std::size_t>(c.geomB)];
            const Point2D a1 = pointAt(sketch, vars, la.p1, x), a2 = pointAt(sketch, vars, la.p2, x);
            const Point2D b1 = pointAt(sketch, vars, lb.p1, x), b2 = pointAt(sketch, vars, lb.p2, x);
            const Point2D da = a2 - a1, db = b2 - b1;
            // Same dot-product form Perpendicular uses (a special case at
            // value == pi/2), scaled by the two lengths so it's a genuine
            // "cosine of the angle" comparison rather than blowing up with
            // line length -- avoids atan2's branch-cut discontinuities.
            r.push_back(da.x * db.x + da.y * db.y - da.length() * db.length() * std::cos(c.value));
            break;
        }
        case SketchConstraintType::PointOnLine: {
            const SketchLine& line = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const Point2D p1 = pointAt(sketch, vars, line.p1, x);
            const Point2D p2 = pointAt(sketch, vars, line.p2, x);
            const Point2D p = pointAt(sketch, vars, c.pointA, x);
            r.push_back(pointToLineDistance(p1, p2, p));
            break;
        }
        case SketchConstraintType::PointOnCircle: {
            const SketchCircle& circle = sketch.circles()[static_cast<std::size_t>(c.geomA)];
            const Point2D center = pointAt(sketch, vars, circle.center, x);
            const Point2D p = pointAt(sketch, vars, c.pointA, x);
            const double radius = radiusAt(sketch, vars, c.geomA, x);
            r.push_back(p.distanceTo(center) - radius);
            break;
        }
        case SketchConstraintType::Midpoint: {
            const SketchLine& line = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const Point2D p1 = pointAt(sketch, vars, line.p1, x);
            const Point2D p2 = pointAt(sketch, vars, line.p2, x);
            const Point2D p = pointAt(sketch, vars, c.pointA, x);
            r.push_back(p.x - (p1.x + p2.x) / 2.0);
            r.push_back(p.y - (p1.y + p2.y) / 2.0);
            break;
        }
        case SketchConstraintType::Fix: {
            const Point2D p = pointAt(sketch, vars, c.pointA, x);
            r.push_back(p.x - c.value);
            r.push_back(p.y - c.value2);
            break;
        }
        case SketchConstraintType::Symmetric: {
            const SketchLine& axis = sketch.lines()[static_cast<std::size_t>(c.geomA)];
            const Point2D axisP1 = pointAt(sketch, vars, axis.p1, x);
            const Point2D axisP2 = pointAt(sketch, vars, axis.p2, x);
            const Point2D a = pointAt(sketch, vars, c.pointA, x);
            const Point2D b = pointAt(sketch, vars, c.pointB, x);
            const Point2D mid((a.x + b.x) / 2.0, (a.y + b.y) / 2.0);
            const Point2D axisDir = axisP2 - axisP1;
            const Point2D ab = b - a;
            r.push_back(pointToLineDistance(axisP1, axisP2, mid));
            r.push_back(axisDir.x * ab.x + axisDir.y * ab.y);
            break;
        }
        }
    }

    // Every arc's start/end must stay exactly radius away from its center
    // -- not a constraint the user can add or remove, the same way a
    // circle's own shape isn't (see SketchArc's own comment).
    for (std::size_t i = 0; i < sketch.arcs().size(); ++i) {
        const SketchArc& arc = sketch.arcs()[i];
        const Point2D center = pointAt(sketch, vars, arc.center, x);
        const Point2D start = pointAt(sketch, vars, arc.start, x);
        const Point2D end = pointAt(sketch, vars, arc.end, x);
        const double radius = arcRadiusAt(sketch, vars, static_cast<int>(i), x);
        r.push_back(start.distanceTo(center) - radius);
        r.push_back(end.distanceTo(center) - radius);
    }
    return r;
}

double normSquared(const std::vector<double>& v) {
    double sum = 0.0;
    for (double value : v) sum += value * value;
    return sum;
}

} // namespace

SolveResult solveSketch(Sketch& sketch, int maxIterations, double tolerance) {
    SolveResult result;
    const VariableMap vars(sketch);
    const int n = vars.size();
    // Every arc contributes 2 always-on internal-consistency residuals
    // (start/end pinned to its own radius around its center) in addition
    // to whatever explicit constraints exist -- see computeResidual's own
    // arc loop, which computeResidual's return size must always match.
    // Most constraint types contribute exactly 1 equation; see
    // equationCount's own comment for the (disclosed) exceptions.
    const int m = totalEquationCount(sketch);

    if (n == 0 || m == 0) {
        result.converged = true;
        return result;
    }

    std::vector<double> x = initialVector(sketch, vars);
    std::vector<double> residual = computeResidual(sketch, vars, x);
    double currentNormSq = normSquared(residual);

    double lambda = 1e-3;
    constexpr double kStep = 1e-6;

    int iter = 0;
    for (; iter < maxIterations; ++iter) {
        if (std::sqrt(currentNormSq) < tolerance) {
            result.converged = true;
            break;
        }

        // Numerically-differentiated Jacobian: column j is d(residual)/d(x[j]).
        std::vector<std::vector<double>> jacobian(static_cast<std::size_t>(m), std::vector<double>(static_cast<std::size_t>(n), 0.0));
        for (int j = 0; j < n; ++j) {
            std::vector<double> xPerturbed = x;
            xPerturbed[static_cast<std::size_t>(j)] += kStep;
            const std::vector<double> rPerturbed = computeResidual(sketch, vars, xPerturbed);
            for (int i = 0; i < m; ++i) {
                jacobian[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                    (rPerturbed[static_cast<std::size_t>(i)] - residual[static_cast<std::size_t>(i)]) / kStep;
            }
        }

        // Normal equations (J^T J + lambda * diag(J^T J)) dx = -J^T r.
        std::vector<std::vector<double>> jtj(static_cast<std::size_t>(n), std::vector<double>(static_cast<std::size_t>(n), 0.0));
        std::vector<double> jtr(static_cast<std::size_t>(n), 0.0);
        for (int a = 0; a < n; ++a) {
            for (int b = 0; b < n; ++b) {
                double sum = 0.0;
                for (int i = 0; i < m; ++i) {
                    sum += jacobian[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)] *
                          jacobian[static_cast<std::size_t>(i)][static_cast<std::size_t>(b)];
                }
                jtj[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = sum;
            }
            double sum = 0.0;
            for (int i = 0; i < m; ++i) sum += jacobian[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)] * residual[static_cast<std::size_t>(i)];
            jtr[static_cast<std::size_t>(a)] = sum;
        }

        bool accepted = false;
        for (int retry = 0; retry < 30 && !accepted; ++retry) {
            std::vector<std::vector<double>> damped = jtj;
            for (int a = 0; a < n; ++a) {
                const double diag = jtj[static_cast<std::size_t>(a)][static_cast<std::size_t>(a)];
                damped[static_cast<std::size_t>(a)][static_cast<std::size_t>(a)] += lambda * (diag > 1e-12 ? diag : 1.0);
            }
            std::vector<double> negJtr(static_cast<std::size_t>(n));
            for (int a = 0; a < n; ++a) negJtr[static_cast<std::size_t>(a)] = -jtr[static_cast<std::size_t>(a)];

            std::vector<double> dx;
            if (!solveLinearSystem(damped, negJtr, dx)) {
                lambda *= 10.0;
                continue;
            }

            std::vector<double> trialX = x;
            for (int a = 0; a < n; ++a) trialX[static_cast<std::size_t>(a)] += dx[static_cast<std::size_t>(a)];
            const std::vector<double> trialResidual = computeResidual(sketch, vars, trialX);
            const double trialNormSq = normSquared(trialResidual);

            if (trialNormSq < currentNormSq) {
                x = trialX;
                residual = trialResidual;
                currentNormSq = trialNormSq;
                lambda = std::max(lambda / 10.0, 1e-12);
                accepted = true;
            } else {
                lambda *= 10.0;
            }
        }
        if (!accepted) break; // stuck: neither converged nor able to improve further
    }

    applyVector(sketch, vars, x);
    result.finalResidualNorm = std::sqrt(currentNormSq);
    result.iterations = iter;
    if (result.finalResidualNorm < tolerance) result.converged = true;
    return result;
}

DofReport analyzeDof(const Sketch& sketch) {
    DofReport report;
    const VariableMap vars(sketch);
    report.totalDof = vars.size();
    report.constraintEquations = totalEquationCount(sketch);
    report.remainingDof = std::max(0, report.totalDof - report.constraintEquations);
    report.likelyOverConstrained = report.constraintEquations > report.totalDof;
    return report;
}

} // namespace lcad
