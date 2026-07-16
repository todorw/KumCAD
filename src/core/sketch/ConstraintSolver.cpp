#include "core/sketch/ConstraintSolver.h"

#include "core/sketch/LinearSolve.h"

#include <cmath>

namespace lcad {

namespace {

// Maps each free (non-fixed) point to a pair of consecutive variable slots
// (x, y) and each circle to one variable slot (radius); fixed points have
// no slot at all and always read from the sketch's stored value.
class VariableMap {
public:
    explicit VariableMap(const Sketch& sketch) : m_pointVar(sketch.points().size(), -1), m_circleVar(sketch.circles().size(), -1) {
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
        m_size = next;
    }

    int size() const { return m_size; }
    int pointVar(int pointIndex) const { return m_pointVar[static_cast<std::size_t>(pointIndex)]; }
    int circleVar(int circleIndex) const { return m_circleVar[static_cast<std::size_t>(circleIndex)]; }

private:
    std::vector<int> m_pointVar;
    std::vector<int> m_circleVar;
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

// Perpendicular distance from c to the infinite line through p1-p2.
double pointToLineDistance(const Point2D& p1, const Point2D& p2, const Point2D& c) {
    const Point2D dir = p2 - p1;
    const double len = dir.length();
    if (len < 1e-12) return c.distanceTo(p1);
    return std::abs(dir.x * (c.y - p1.y) - dir.y * (c.x - p1.x)) / len;
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
        }
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
    const int m = static_cast<int>(sketch.constraints().size());

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

} // namespace lcad
