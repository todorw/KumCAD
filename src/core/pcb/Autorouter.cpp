#include "core/pcb/Autorouter.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/pcb/Ratsnest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>

namespace lcad {

namespace {

struct GridCell {
    int x = 0, y = 0;
    bool operator==(const GridCell& o) const { return x == o.x && y == o.y; }
};

struct PlacedPad {
    Point2D position;
    double radius;
    std::string netName; // empty if this pad isn't part of any net being routed
};

struct RoutedSegment {
    Point2D a, b;
    std::string netName;
    double trackWidth = 0.25;
    double clearance = 0.2;
};

struct PendingConnection {
    RatsnestLine line;
    std::string netName;
};

double pointToSegmentDistance(const Point2D& p, const Point2D& a, const Point2D& b) {
    const Point2D dir = b - a;
    const double lenSq = dir.dot(dir);
    if (lenSq < 1e-12) return p.distanceTo(a);
    double t = (p - a).dot(dir) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return p.distanceTo(a + dir * t);
}

// Marks every grid cell within radius of center as an obstacle, only
// scanning that cell's own local bounding box (not the whole grid) for
// efficiency.
void markNearPoint(std::vector<bool>& obstacle, int nx, int ny, double minX, double minY, double gridSize,
                   const Point2D& center, double radius) {
    const int cx = static_cast<int>((center.x - minX) / gridSize);
    const int cy = static_cast<int>((center.y - minY) / gridSize);
    const int spread = static_cast<int>(std::ceil(radius / gridSize)) + 1;
    for (int j = std::max(0, cy - spread); j <= std::min(ny - 1, cy + spread); ++j) {
        for (int i = std::max(0, cx - spread); i <= std::min(nx - 1, cx + spread); ++i) {
            const Point2D cellCenter(minX + (i + 0.5) * gridSize, minY + (j + 0.5) * gridSize);
            if (cellCenter.distanceTo(center) <= radius) obstacle[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i)] = true;
        }
    }
}

void markNearSegment(std::vector<bool>& obstacle, int nx, int ny, double minX, double minY, double gridSize,
                     const Point2D& a, const Point2D& b, double radius) {
    const double length = a.distanceTo(b);
    const int samples = std::max(1, static_cast<int>(std::ceil(length / (gridSize * 0.5))));
    for (int s = 0; s <= samples; ++s) {
        const double t = static_cast<double>(s) / samples;
        const Point2D sample = a + (b - a) * t;
        markNearPoint(obstacle, nx, ny, minX, minY, gridSize, sample, radius);
    }
}

GridCell toCell(const Point2D& p, double minX, double minY, double gridSize) {
    return {static_cast<int>((p.x - minX) / gridSize), static_cast<int>((p.y - minY) / gridSize)};
}

Point2D toWorld(const GridCell& c, double minX, double minY, double gridSize) {
    return Point2D(minX + (c.x + 0.5) * gridSize, minY + (c.y + 0.5) * gridSize);
}

// Classic Lee/maze breadth-first search over the grid, 8-connected.
// Returns the cell path from start to goal inclusive, or empty if goal is
// unreachable.
std::vector<GridCell> bfsRoute(const std::vector<bool>& obstacle, int nx, int ny, GridCell start, GridCell goal) {
    auto inBounds = [&](GridCell c) { return c.x >= 0 && c.x < nx && c.y >= 0 && c.y < ny; };
    auto idx = [&](GridCell c) { return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(c.x); };
    if (!inBounds(start) || !inBounds(goal)) return {};

    std::vector<bool> visited(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), false);
    std::vector<int> parent(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), -1);
    std::queue<GridCell> queue;
    queue.push(start);
    visited[idx(start)] = true;

    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    bool found = start == goal;
    while (!queue.empty() && !found) {
        const GridCell current = queue.front();
        queue.pop();
        for (int d = 0; d < 8; ++d) {
            const GridCell next{current.x + kDx[d], current.y + kDy[d]};
            if (!inBounds(next) || visited[idx(next)]) continue;
            if (obstacle[idx(next)] && !(next == goal)) continue; // goal cell is always enterable
            visited[idx(next)] = true;
            parent[idx(next)] = static_cast<int>(idx(current));
            if (next == goal) {
                found = true;
                break;
            }
            queue.push(next);
        }
    }
    if (!found) return {};

    std::vector<GridCell> path;
    int cur = static_cast<int>(idx(goal));
    while (cur != -1) {
        path.push_back({cur % nx, cur / nx});
        cur = parent[static_cast<std::size_t>(cur)];
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// Removes interior points that lie on a straight run, so a routed track
// is a handful of segments, not one point per grid cell.
std::vector<Point2D> simplifyPath(const std::vector<Point2D>& raw) {
    if (raw.size() <= 2) return raw;
    std::vector<Point2D> out = {raw.front()};
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const Point2D d1 = raw[i] - out.back();
        const Point2D d2 = raw[i + 1] - raw[i];
        if (std::abs(d1.x * d2.y - d1.y * d2.x) > 1e-9) out.push_back(raw[i]);
    }
    out.push_back(raw.back());
    return out;
}

} // namespace

AutorouteResult autoroute(Document& doc, const std::vector<ImportedNet>& nets, const AutorouteParams& params,
                          const std::vector<NetClass>& netClasses) {
    AutorouteResult result;
    if (params.gridSize <= 1e-9) return result;

    std::map<std::pair<std::string, std::string>, std::string> pinToNet;
    for (const ImportedNet& net : nets) {
        for (const ImportedNetPin& pin : net.pins) pinToNet[{pin.refDes, pin.pinNumber}] = net.name;
    }

    std::vector<PlacedPad> allPads;
    double minX = std::numeric_limits<double>::max(), minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest(), maxY = std::numeric_limits<double>::lowest();
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (!insert->block() || !insert->block()->isFootprint()) continue;
        const std::string* refdes = insert->attributeValue("REFDES");
        const std::string rd = refdes ? *refdes : "";
        for (const auto& padWorld : insert->padWorldPositions()) {
            std::string netName;
            const auto it = pinToNet.find({rd, padWorld.pad->number});
            if (it != pinToNet.end()) netName = it->second;
            allPads.push_back({padWorld.position, std::max(padWorld.pad->width, padWorld.pad->height) / 2.0, netName});
            minX = std::min(minX, padWorld.position.x);
            minY = std::min(minY, padWorld.position.y);
            maxX = std::max(maxX, padWorld.position.x);
            maxY = std::max(maxY, padWorld.position.y);
        }
    }
    if (allPads.empty()) return result;

    const double margin = params.gridSize * 4.0;
    minX -= margin;
    minY -= margin;
    maxX += margin;
    maxY += margin;
    const int nx = std::max(1, static_cast<int>(std::ceil((maxX - minX) / params.gridSize)));
    const int ny = std::max(1, static_cast<int>(std::ceil((maxY - minY) / params.gridSize)));

    std::vector<PendingConnection> pending;
    for (const ImportedNet& net : nets) {
        for (const RatsnestLine& line : computeRatsnest(doc, {net})) pending.push_back({line, net.name});
    }
    std::sort(pending.begin(), pending.end(), [](const PendingConnection& a, const PendingConnection& b) {
        return a.line.a.distanceTo(a.line.b) < b.line.a.distanceTo(b.line.b);
    });

    std::vector<RoutedSegment> routedSegments;

    for (const PendingConnection& conn : pending) {
        // netClasses (see NetClass.h) lets different nets route with
        // their own track width/clearance -- a net with no matching
        // class still falls back to params' own single global values,
        // exactly the pre-existing behavior when netClasses is empty.
        const NetClass* connClass = findNetClass(netClasses, conn.netName);
        const double connTrackWidth = connClass ? connClass->trackWidth : params.trackWidth;
        const double connClearance = connClass ? connClass->clearance : params.clearance;

        std::vector<bool> obstacle(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), false);
        for (const PlacedPad& pad : allPads) {
            if (!pad.netName.empty() && pad.netName == conn.netName) continue;
            // Clearance to another net's copper uses the LARGER of the
            // two nets' own required clearances (the same convention
            // Drc.h's own per-net-class check uses), not just this
            // connection's own.
            const NetClass* padClass = findNetClass(netClasses, pad.netName);
            const double padClearance = std::max(connClearance, padClass ? padClass->clearance : params.clearance);
            // pad.radius is already the pad's own full extent (not a
            // half-width needing a second half added on top of it) --
            // only this connection's own track half-width needs adding.
            markNearPoint(obstacle, nx, ny, minX, minY, params.gridSize, pad.position,
                         pad.radius + connTrackWidth / 2.0 + padClearance);
        }
        for (const RoutedSegment& seg : routedSegments) {
            if (seg.netName == conn.netName) continue;
            const double segClearance = std::max(connClearance, seg.clearance);
            markNearSegment(obstacle, nx, ny, minX, minY, params.gridSize, seg.a, seg.b,
                           connTrackWidth / 2.0 + segClearance + seg.trackWidth / 2.0);
        }

        const GridCell start = toCell(conn.line.a, minX, minY, params.gridSize);
        const GridCell goal = toCell(conn.line.b, minX, minY, params.gridSize);
        auto clearCell = [&](GridCell c) {
            if (c.x >= 0 && c.x < nx && c.y >= 0 && c.y < ny) {
                obstacle[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(c.x)] = false;
            }
        };
        clearCell(start);
        clearCell(goal);

        const std::vector<GridCell> cellPath = bfsRoute(obstacle, nx, ny, start, goal);
        if (cellPath.empty()) {
            ++result.failedCount;
            result.failedNetNames.push_back(conn.netName);
            continue;
        }

        std::vector<Point2D> worldPath;
        worldPath.reserve(cellPath.size());
        for (const GridCell& c : cellPath) worldPath.push_back(toWorld(c, minX, minY, params.gridSize));
        worldPath.front() = conn.line.a; // snap the endpoints back to the exact pad positions
        worldPath.back() = conn.line.b;
        const std::vector<Point2D> simplified = simplifyPath(worldPath);

        doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), params.layer, simplified, connTrackWidth));
        for (std::size_t i = 0; i + 1 < simplified.size(); ++i) {
            routedSegments.push_back({simplified[i], simplified[i + 1], conn.netName, connTrackWidth, connClearance});
        }
        ++result.routedCount;
    }

    return result;
}

} // namespace lcad
