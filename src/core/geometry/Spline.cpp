#include "core/geometry/Spline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

namespace {

// All basis values N_{k-p..k},p(u) for the knot span k containing u -- the
// only nonzero ones -- via the standard triangular scheme (The NURBS Book,
// algorithm A2.2). Returns p+1 values, out[j] = N_{k-p+j},p(u).
std::vector<double> basisFunctions(int span, double u, int degree, const std::vector<double>& knots) {
    std::vector<double> out(degree + 1, 0.0);
    std::vector<double> left(degree + 1, 0.0);
    std::vector<double> right(degree + 1, 0.0);
    out[0] = 1.0;
    for (int j = 1; j <= degree; ++j) {
        left[j] = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double denom = right[r + 1] + left[j - r];
            const double temp = denom > 1e-12 ? out[r] / denom : 0.0;
            out[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        out[j] = saved;
    }
    return out;
}

int findSpan(double u, int degree, const std::vector<double>& knots, int numControl) {
    // Valid spans run from `degree` to numControl-1.
    if (u >= knots[numControl]) return numControl - 1;
    if (u <= knots[degree]) return degree;
    int lo = degree;
    int hi = numControl;
    int mid = (lo + hi) / 2;
    while (u < knots[mid] || u >= knots[mid + 1]) {
        if (u < knots[mid]) hi = mid;
        else lo = mid;
        mid = (lo + hi) / 2;
    }
    return mid;
}

double distanceToSegment(const Point2D& pt, const Point2D& a, const Point2D& b) {
    const Point2D seg = b - a;
    const double lenSq = seg.dot(seg);
    if (lenSq < 1e-12) return pt.distanceTo(a);
    double t = (pt - a).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return pt.distanceTo(a + seg * t);
}

// Solves the dense linear system A x = b for the interpolation control
// points, one Gaussian elimination shared by the x and y right-hand sides.
// A is destroyed. Returns false on a (numerically) singular matrix.
bool solve(std::vector<std::vector<double>>& A, std::vector<Point2D>& b) {
    const int n = static_cast<int>(A.size());
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(A[row][col]) > std::abs(A[pivot][col])) pivot = row;
        }
        if (std::abs(A[pivot][col]) < 1e-12) return false;
        std::swap(A[col], A[pivot]);
        std::swap(b[col], b[pivot]);
        for (int row = col + 1; row < n; ++row) {
            const double factor = A[row][col] / A[col][col];
            if (factor == 0.0) continue;
            for (int k = col; k < n; ++k) A[row][k] -= factor * A[col][k];
            b[row] = b[row] - b[col] * factor;
        }
    }
    for (int col = n - 1; col >= 0; --col) {
        Point2D sum = b[col];
        for (int k = col + 1; k < n; ++k) sum = sum - b[k] * A[col][k];
        b[col] = sum * (1.0 / A[col][col]);
    }
    return true;
}

} // namespace

SplineEntity::SplineEntity(EntityId id, LayerId layer, int degree, std::vector<Point2D> controlPoints,
                           std::vector<double> knots, std::vector<Point2D> fitPoints)
    : Entity(id, layer), m_degree(std::max(1, degree)), m_controlPoints(std::move(controlPoints)),
      m_knots(std::move(knots)), m_fitPoints(std::move(fitPoints)) {}

std::unique_ptr<SplineEntity> SplineEntity::fromFitPoints(EntityId id, LayerId layer, std::vector<Point2D> fitPoints) {
    const int n = static_cast<int>(fitPoints.size());
    if (n < 2) return nullptr;
    const int degree = std::min(3, n - 1);

    // Chord-length parameterization, normalized to [0, 1].
    std::vector<double> params(n, 0.0);
    double total = 0.0;
    for (int i = 1; i < n; ++i) {
        total += fitPoints[i].distanceTo(fitPoints[i - 1]);
        params[i] = total;
    }
    if (total < 1e-12) return nullptr; // all points coincident
    for (double& t : params) t /= total;

    // Clamped knot vector by knot averaging (The NURBS Book, eq. 9.8).
    std::vector<double> knots(n + degree + 1, 0.0);
    for (int i = 0; i <= degree; ++i) knots[knots.size() - 1 - i] = 1.0;
    for (int j = 1; j <= n - 1 - degree; ++j) {
        double sum = 0.0;
        for (int i = j; i <= j + degree - 1; ++i) sum += params[i];
        knots[j + degree] = sum / degree;
    }

    // Interpolation system: N_j(params[i]) * P_j = Q_i.
    std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        const int span = findSpan(params[i], degree, knots, n);
        const auto basis = basisFunctions(span, params[i], degree, knots);
        for (int j = 0; j <= degree; ++j) A[i][span - degree + j] = basis[j];
    }
    std::vector<Point2D> control = fitPoints;
    if (!solve(A, control)) return nullptr;

    return std::make_unique<SplineEntity>(id, layer, degree, std::move(control), std::move(knots),
                                          std::move(fitPoints));
}

Point2D SplineEntity::evaluate(double u) const {
    const int n = static_cast<int>(m_controlPoints.size());
    if (n == 0) return Point2D();
    if (n <= m_degree || m_knots.size() != static_cast<std::size_t>(n + m_degree + 1)) {
        return m_controlPoints.front(); // malformed; don't crash on bad files
    }
    u = std::clamp(u, m_knots[m_degree], m_knots[n]);
    const int span = findSpan(u, m_degree, m_knots, n);
    const auto basis = basisFunctions(span, u, m_degree, m_knots);
    Point2D result;
    for (int j = 0; j <= m_degree; ++j) {
        result = result + m_controlPoints[span - m_degree + j] * basis[j];
    }
    return result;
}

std::vector<Point2D> SplineEntity::sample(int count) const {
    std::vector<Point2D> pts;
    const int n = static_cast<int>(m_controlPoints.size());
    if (n == 0) return pts;
    if (n <= m_degree || m_knots.size() != static_cast<std::size_t>(n + m_degree + 1)) {
        return m_controlPoints; // fall back to the control polygon
    }
    count = std::max(count, 2);
    const double lo = m_knots[m_degree];
    const double hi = m_knots[n];
    pts.reserve(count);
    for (int i = 0; i < count; ++i) {
        pts.push_back(evaluate(lo + (hi - lo) * i / (count - 1)));
    }
    return pts;
}

BoundingBox SplineEntity::boundingBox() const {
    BoundingBox box;
    for (const auto& p : sample()) box.expand(p);
    return box;
}

double SplineEntity::distanceTo(const Point2D& pt) const {
    const auto pts = sample(128);
    if (pts.size() == 1) return pt.distanceTo(pts.front());
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        best = std::min(best, distanceToSegment(pt, pts[i], pts[i + 1]));
    }
    return best;
}

void SplineEntity::translate(const Point2D& delta) {
    for (auto& p : m_controlPoints) p = p + delta;
    for (auto& p : m_fitPoints) p = p + delta;
}

void SplineEntity::rotate(const Point2D& center, double angleRadians) {
    for (auto& p : m_controlPoints) p = rotateAround(p, center, angleRadians);
    for (auto& p : m_fitPoints) p = rotateAround(p, center, angleRadians);
}

void SplineEntity::scale(const Point2D& center, double factor) {
    for (auto& p : m_controlPoints) p = scaleAround(p, center, factor);
    for (auto& p : m_fitPoints) p = scaleAround(p, center, factor);
}

void SplineEntity::mirror(const Point2D& a, const Point2D& b) {
    for (auto& p : m_controlPoints) p = mirrorAcross(p, a, b);
    for (auto& p : m_fitPoints) p = mirrorAcross(p, a, b);
}

std::vector<Point2D> SplineEntity::gripPoints() const {
    return m_fitPoints.empty() ? m_controlPoints : m_fitPoints;
}

void SplineEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (m_fitPoints.empty()) {
        if (index < m_controlPoints.size()) m_controlPoints[index] = newPos;
        return;
    }
    if (index >= m_fitPoints.size()) return;
    m_fitPoints[index] = newPos;
    refitFromFitPoints();
}

void SplineEntity::refitFromFitPoints() {
    if (auto refit = fromFitPoints(id(), layer(), m_fitPoints)) {
        m_degree = refit->m_degree;
        m_controlPoints = std::move(refit->m_controlPoints);
        m_knots = std::move(refit->m_knots);
    }
}

std::vector<SnapPoint> SplineEntity::snapCandidates() const {
    std::vector<SnapPoint> result;
    const auto& anchors = m_fitPoints.empty() ? m_controlPoints : m_fitPoints;
    if (anchors.empty()) return result;
    result.push_back({anchors.front(), SnapKind::Endpoint});
    if (anchors.size() > 1) result.push_back({anchors.back(), SnapKind::Endpoint});
    return result;
}

std::unique_ptr<Entity> SplineEntity::clone() const {
    return std::make_unique<SplineEntity>(*this);
}

} // namespace lcad
