#include "core/geometry/BoundaryTrace.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

namespace {

constexpr double kEps = 1e-6;

double normalizeAngle(double a) {
    while (a < 0.0) a += 2.0 * M_PI;
    while (a >= 2.0 * M_PI) a -= 2.0 * M_PI;
    return a;
}

// A planar arrangement built from the raw segments, split at every mutual
// crossing so faces can be traced vertex-by-vertex.
struct Arrangement {
    std::vector<Point2D> nodes;
    std::vector<std::vector<int>> adjacency; // node -> neighbor node indices (parallel edges allowed)

    int findOrAddNode(const Point2D& p) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].distanceTo(p) < kEps) return static_cast<int>(i);
        }
        nodes.push_back(p);
        adjacency.emplace_back();
        return static_cast<int>(nodes.size() - 1);
    }

    void addEdge(int a, int b) {
        if (a == b) return;
        adjacency[a].push_back(b);
        adjacency[b].push_back(a);
    }
};

// Proper interior crossing of [a1,a2] and [b1,b2]; false for parallel/collinear
// or touches at/near an endpoint (those already share a node once merged).
bool segmentCrossing(const Point2D& a1, const Point2D& a2, const Point2D& b1, const Point2D& b2, double& ta,
                     double& tb) {
    const Point2D r = a2 - a1;
    const Point2D s = b2 - b1;
    const double denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-12) return false;
    const Point2D diff = b1 - a1;
    ta = (diff.x * s.y - diff.y * s.x) / denom;
    tb = (diff.x * r.y - diff.y * r.x) / denom;
    const double margin = 1e-6;
    return ta > margin && ta < 1.0 - margin && tb > margin && tb < 1.0 - margin;
}

Arrangement buildArrangement(const std::vector<std::pair<Point2D, Point2D>>& segments) {
    Arrangement arr;
    const std::size_t n = segments.size();
    // Split parameters (plus the two endpoints) collected per segment.
    std::vector<std::vector<double>> splits(n, std::vector<double>{0.0, 1.0});

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            double ta = 0.0, tb = 0.0;
            if (segmentCrossing(segments[i].first, segments[i].second, segments[j].first, segments[j].second, ta,
                                tb)) {
                splits[i].push_back(ta);
                splits[j].push_back(tb);
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        std::sort(splits[i].begin(), splits[i].end());
        splits[i].erase(std::unique(splits[i].begin(), splits[i].end(),
                                    [](double a, double b) { return std::abs(a - b) < 1e-9; }),
                        splits[i].end());
        const Point2D& a = segments[i].first;
        const Point2D& b = segments[i].second;
        int prevNode = arr.findOrAddNode(a + (b - a) * splits[i].front());
        for (std::size_t k = 1; k < splits[i].size(); ++k) {
            const int node = arr.findOrAddNode(a + (b - a) * splits[i][k]);
            arr.addEdge(prevNode, node);
            prevNode = node;
        }
    }
    return arr;
}

// Traces the face reached by walking from startU into startV, always taking
// the next edge in consistent angular order at each vertex (never doubling
// back along the edge just arrived on). Returns the loop without a duplicated
// closing vertex, or nullopt if it runs off an open (dangling) end.
std::optional<std::vector<Point2D>> traceFace(const Arrangement& arr, int startU, int startV) {
    std::vector<Point2D> loop{arr.nodes[startU]};
    int prev = startU;
    int cur = startV;
    const int maxSteps = static_cast<int>(arr.nodes.size()) * 4 + 32;

    for (int step = 0; step < maxSteps; ++step) {
        if (cur == startU) return loop;
        loop.push_back(arr.nodes[cur]);

        const double incoming = std::atan2(arr.nodes[cur].y - arr.nodes[prev].y, arr.nodes[cur].x - arr.nodes[prev].x);
        const double reverseAngle = normalizeAngle(incoming + M_PI);

        int best = -1;
        double bestDelta = std::numeric_limits<double>::max();
        for (int w : arr.adjacency[cur]) {
            if (w == prev) continue; // never immediately backtrack
            const double outAngle = std::atan2(arr.nodes[w].y - arr.nodes[cur].y, arr.nodes[w].x - arr.nodes[cur].x);
            // Smallest clockwise turn from the reverse of the incoming edge:
            // the rule that keeps the trace on the *smallest* adjacent face
            // rather than wrapping around the outside of a junction.
            const double delta = normalizeAngle(reverseAngle - outAngle);
            if (delta < bestDelta) {
                bestDelta = delta;
                best = w;
            }
        }
        if (best < 0) return std::nullopt; // dangling end: no enclosed face here
        prev = cur;
        cur = best;
    }
    return std::nullopt; // didn't close in a sane number of steps
}

bool pointInPolygon(const std::vector<Point2D>& poly, const Point2D& pt) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Point2D& a = poly[i];
        const Point2D& b = poly[j];
        if ((a.y > pt.y) != (b.y > pt.y)) {
            const double x = a.x + (pt.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x > pt.x) inside = !inside;
        }
    }
    return inside;
}

double polygonArea(const std::vector<Point2D>& poly) {
    double area = 0.0;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) area += poly[j].x * poly[i].y - poly[i].x * poly[j].y;
    return std::abs(area) * 0.5;
}

} // namespace

std::optional<std::vector<Point2D>> traceBoundary(const std::vector<std::pair<Point2D, Point2D>>& segments,
                                                   const Point2D& pickPoint) {
    if (segments.empty()) return std::nullopt;
    const Arrangement arr = buildArrangement(segments);

    // Nearest edge crossing a rightward ray from pickPoint: guaranteed to lie
    // on the boundary of whatever bounded face encloses the point, if any.
    int bestA = -1, bestB = -1;
    double bestX = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < arr.nodes.size(); ++i) {
        for (int j : arr.adjacency[i]) {
            if (j <= static_cast<int>(i)) continue; // each undirected edge once
            const Point2D& a = arr.nodes[i];
            const Point2D& b = arr.nodes[static_cast<std::size_t>(j)];
            if ((a.y > pickPoint.y) == (b.y > pickPoint.y)) continue;
            const double x = a.x + (pickPoint.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x > pickPoint.x && x < bestX) {
                bestX = x;
                bestA = static_cast<int>(i);
                bestB = j;
            }
        }
    }
    if (bestA < 0) return std::nullopt;

    for (const auto& [u, v] : {std::pair{bestA, bestB}, std::pair{bestB, bestA}}) {
        if (auto loop = traceFace(arr, u, v)) {
            if (loop->size() >= 3 && polygonArea(*loop) > kEps && pointInPolygon(*loop, pickPoint)) return loop;
        }
    }
    return std::nullopt;
}

} // namespace lcad
