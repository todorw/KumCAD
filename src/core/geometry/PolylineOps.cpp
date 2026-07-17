#include "core/geometry/PolylineOps.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Line.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace lcad {

namespace {

Point2D normalized(const Point2D& v, const Point2D& fallback = Point2D(1, 0)) {
    const double len = v.length();
    return len > 1e-12 ? v * (1.0 / len) : fallback;
}

Point2D leftNormal(const Point2D& dir) {
    return Point2D(-dir.y, dir.x);
}

// Tangent directions of the segment a->b (with bulge) at its start and end.
struct SegmentTangents {
    Point2D atStart;
    Point2D atEnd;
};

SegmentTangents segmentTangents(const Point2D& a, const Point2D& b, double bulge) {
    const Point2D chord = normalized(b - a);
    if (const auto arc = bulgeToArc(a, b, bulge)) {
        const double half = arc->sweep / 2.0;
        return {rotateAround(chord, Point2D(), -half), rotateAround(chord, Point2D(), half)};
    }
    return {chord, chord};
}

double distanceToSegment(const Point2D& pt, const Point2D& a, const Point2D& b) {
    const Point2D seg = b - a;
    const double lenSq = seg.dot(seg);
    if (lenSq < 1e-12) return pt.distanceTo(a);
    double t = (pt - a).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return pt.distanceTo(a + seg * t);
}

// One joinable piece: an ordered vertex run with per-vertex bulges (the last
// bulge is unused/0 for open runs, mirroring PolylineEntity's convention).
struct ChainPiece {
    std::vector<Point2D> pts;
    std::vector<double> bulges;

    void reverse() {
        std::reverse(pts.begin(), pts.end());
        // Bulge i described the segment leaving old vertex i; after reversal
        // the segment leaving new vertex i is the old segment i' = n-2-i,
        // traversed backward (sign flip).
        std::vector<double> flipped(bulges.size(), 0.0);
        for (std::size_t i = 0; i + 1 < bulges.size(); ++i) {
            flipped[i] = -bulges[bulges.size() - 2 - i];
        }
        bulges = std::move(flipped);
    }
};

std::optional<ChainPiece> pieceOf(const Entity& e) {
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        if (line.start().distanceTo(line.end()) < 1e-12) return std::nullopt;
        return ChainPiece{{line.start(), line.end()}, {0.0, 0.0}};
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        double sweep = arc.endAngle() - arc.startAngle();
        sweep = std::fmod(sweep, 2 * M_PI);
        if (sweep <= 0) sweep += 2 * M_PI;
        return ChainPiece{{arc.startPoint(), arc.endPoint()}, {std::tan(sweep / 4.0), 0.0}};
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        if (pl.closed() || pl.vertices().size() < 2) return std::nullopt; // closed plines can't be joined
        return ChainPiece{pl.vertices(), pl.bulges()};
    }
    default:
        return std::nullopt;
    }
}

} // namespace

std::unique_ptr<PolylineEntity> offsetPolyline(const PolylineEntity& source, EntityId newId, double distance,
                                               const Point2D& sidePoint) {
    const auto& verts = source.vertices();
    const auto& bulges = source.bulges();
    const std::size_t n = verts.size();
    if (n < 2 || distance < 1e-12) return nullptr;
    const bool closed = source.closed();

    // Which side: sign of the side point against the closest segment's left
    // normal. For arc segments the side is judged against the curve (inside
    // vs outside the circle), since the pick may sit between chord and arc.
    double bestDist = std::numeric_limits<double>::max();
    double side = 1.0;
    source.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
        if (const auto arc = bulgeToArc(a, b, bulge)) {
            const double toCenter = sidePoint.distanceTo(arc->center);
            const double d = std::abs(toCenter - arc->radius);
            if (d < bestDist) {
                bestDist = d;
                // A CCW arc's left-of-travel normal points at its center.
                const bool inside = toCenter < arc->radius;
                side = (arc->sweep > 0) == inside ? 1.0 : -1.0;
            }
        } else {
            const double d = distanceToSegment(sidePoint, a, b);
            if (d < bestDist) {
                bestDist = d;
                side = (sidePoint - a).dot(leftNormal(normalized(b - a))) >= 0 ? 1.0 : -1.0;
            }
        }
    });
    const double offset = distance * side;

    // An inward offset can't exceed a bulged segment's radius.
    bool degenerate = false;
    source.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
        if (const auto arc = bulgeToArc(a, b, bulge)) {
            // The arc curves toward its center; offsetting toward the center
            // (left normal of travel points there for CCW arcs) shrinks it.
            const double towardCenter = arc->sweep > 0 ? offset : -offset;
            if (arc->radius - towardCenter < 1e-9) degenerate = true;
        }
    });
    if (degenerate) return nullptr;

    // Tangents entering and leaving each vertex.
    std::vector<Point2D> inTangent(n), outTangent(n);
    std::vector<bool> hasIn(n, false), hasOut(n, false);
    for (std::size_t i = 0; i + 1 < n || (closed && i < n); ++i) {
        const std::size_t j = (i + 1) % n;
        if (!closed && i + 1 >= n) break;
        const auto tangents = segmentTangents(verts[i], verts[j], bulges[i]);
        outTangent[i] = tangents.atStart;
        hasOut[i] = true;
        inTangent[j] = tangents.atEnd;
        hasIn[j] = true;
    }

    std::vector<Point2D> newVerts(n);
    for (std::size_t i = 0; i < n; ++i) {
        Point2D normal;
        if (hasIn[i] && hasOut[i]) {
            const Point2D nIn = leftNormal(inTangent[i]);
            const Point2D nOut = leftNormal(outTangent[i]);
            const Point2D miter = normalized(nIn + nOut, nOut);
            const double cosHalf = miter.dot(nOut);
            normal = miter * (1.0 / std::max(cosHalf, 0.1)); // cap miter spikes at sharp corners
        } else if (hasOut[i]) {
            normal = leftNormal(outTangent[i]);
        } else if (hasIn[i]) {
            normal = leftNormal(inTangent[i]);
        } else {
            return nullptr;
        }
        newVerts[i] = verts[i] + normal * offset;
    }

    auto result = std::make_unique<PolylineEntity>(newId, source.layer(), std::move(newVerts), bulges, closed);
    result->setLinetypeOverride(source.linetypeOverride());
    result->setColorOverride(source.colorOverride());
    return result;
}

std::unique_ptr<PolylineEntity> joinToPolyline(EntityId newId, LayerId layer,
                                               const std::vector<const Entity*>& parts, double tol) {
    std::vector<ChainPiece> pieces;
    for (const Entity* e : parts) {
        if (!e) continue;
        if (auto piece = pieceOf(*e)) pieces.push_back(std::move(*piece));
    }
    if (pieces.size() < 2) return nullptr;

    ChainPiece chain = std::move(pieces.front());
    pieces.erase(pieces.begin());

    // Greedily attach whichever remaining piece touches the chain's ends.
    while (!pieces.empty()) {
        bool attached = false;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            ChainPiece piece = pieces[i];
            for (int flip = 0; flip < 2 && !attached; ++flip) {
                if (flip) piece.reverse();
                if (chain.pts.back().distanceTo(piece.pts.front()) <= tol) {
                    // Append: the shared vertex keeps the piece's outgoing bulge.
                    chain.bulges.back() = piece.bulges.front();
                    for (std::size_t k = 1; k < piece.pts.size(); ++k) {
                        chain.pts.push_back(piece.pts[k]);
                        chain.bulges.push_back(piece.bulges[k]);
                    }
                    attached = true;
                } else if (chain.pts.front().distanceTo(piece.pts.back()) <= tol) {
                    // Prepend.
                    piece.bulges.back() = chain.bulges.front();
                    for (std::size_t k = 1; k < chain.pts.size(); ++k) {
                        piece.pts.push_back(chain.pts[k]);
                        piece.bulges.push_back(chain.bulges[k]);
                    }
                    chain = std::move(piece);
                    attached = true;
                }
            }
            if (attached) {
                pieces.erase(pieces.begin() + static_cast<long>(i));
                break;
            }
        }
        if (!attached) return nullptr; // disjoint leftovers: not a single chain
    }

    bool closed = false;
    if (chain.pts.size() > 2 && chain.pts.front().distanceTo(chain.pts.back()) <= tol) {
        // The closing segment's bulge is the one leaving the (dropped) last
        // vertex's predecessor -- already in place; drop the duplicate vertex.
        chain.pts.pop_back();
        chain.bulges.pop_back();
        closed = true;
    }
    if (chain.pts.size() < 2) return nullptr;
    return std::make_unique<PolylineEntity>(newId, layer, std::move(chain.pts), std::move(chain.bulges), closed);
}

std::unique_ptr<PolylineEntity> revisionCloud(EntityId newId, LayerId layer, const std::vector<Point2D>& boundary,
                                              bool closed, double archLength, double bulgeMagnitude) {
    if (boundary.size() < 3 || archLength <= 1e-9) return nullptr;

    const std::size_t segCount = closed ? boundary.size() : boundary.size() - 1;
    double perimeter = 0.0;
    for (std::size_t i = 0; i < segCount; ++i) {
        perimeter += boundary[i].distanceTo(boundary[(i + 1) % boundary.size()]);
    }
    if (perimeter < 1e-9) return nullptr;

    const int teeth = std::max(3, static_cast<int>(std::lround(perimeter / archLength)));
    const double stepLen = perimeter / teeth;
    // How many resampled points to end up with: one per tooth for a closed
    // boundary (the wrap-around segment closes the loop); one extra to
    // keep the original end point exactly for an open one.
    const std::size_t pointCount = static_cast<std::size_t>(teeth) + (closed ? 0 : 1);

    // Overall winding direction (shoelace sign over the ORIGINAL boundary,
    // stable regardless of resampling density) picks a single bulge sign
    // applied to every resampled segment, so every tooth bulges outward.
    double signedArea = 0.0;
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const Point2D& a = boundary[i];
        const Point2D& b = boundary[(i + 1) % boundary.size()];
        signedArea += a.x * b.y - b.x * a.y;
    }
    const double bulge = signedArea >= 0.0 ? bulgeMagnitude : -bulgeMagnitude;

    // Walk the original boundary's cumulative arc length, emitting a
    // resampled point every stepLen -- a standard even-arc-length resample.
    std::vector<Point2D> resampled;
    resampled.reserve(pointCount);
    resampled.push_back(boundary[0]);

    double travelled = 0.0;
    double nextMark = stepLen;
    Point2D cur = boundary[0];
    for (std::size_t i = 0; i < segCount && resampled.size() < pointCount; ++i) {
        const Point2D& next = boundary[(i + 1) % boundary.size()];
        const double segLen = cur.distanceTo(next);
        if (segLen < 1e-12) {
            cur = next;
            continue;
        }
        // A segment may need to emit more than one resampled point if
        // stepLen is small relative to it.
        while (travelled + segLen >= nextMark - 1e-9 && resampled.size() < pointCount) {
            const double t = std::clamp((nextMark - travelled) / segLen, 0.0, 1.0);
            resampled.emplace_back(cur.x + (next.x - cur.x) * t, cur.y + (next.y - cur.y) * t);
            nextMark += stepLen;
        }
        travelled += segLen;
        cur = next;
    }
    if (!closed && resampled.size() < pointCount) resampled.push_back(boundary.back());

    if (!closed && resampled.size() < 2) return nullptr;
    if (closed && resampled.size() < 3) return nullptr;

    std::vector<double> bulges(resampled.size(), bulge);
    if (!closed) bulges.back() = 0.0; // no segment leaves the final open vertex

    return std::make_unique<PolylineEntity>(newId, layer, std::move(resampled), std::move(bulges), closed);
}

} // namespace lcad
