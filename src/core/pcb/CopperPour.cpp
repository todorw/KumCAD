#include "core/pcb/CopperPour.h"

#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

namespace {

bool pointInPolygon(const Point2D& p, const std::vector<Point2D>& poly) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Point2D& a = poly[i];
        const Point2D& b = poly[j];
        const bool crosses = ((a.y > p.y) != (b.y > p.y));
        if (!crosses) continue;
        const double xCross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (p.x < xCross) inside = !inside;
    }
    return inside;
}

bool nearExempt(const Point2D& p, const std::vector<Point2D>& exemptPoints) {
    constexpr double kEpsilon = 1e-3;
    return std::any_of(exemptPoints.begin(), exemptPoints.end(),
                       [&](const Point2D& e) { return p.distanceTo(e) < kEpsilon; });
}

struct Obstacle {
    // A point obstacle (pad/via): radius around position. A track uses
    // its own real distanceTo() instead (see clearanceOk below), so it
    // isn't represented as an Obstacle here.
    Point2D position;
    double radius;
};

double clearanceOk(const Point2D& center, double clearance, const std::vector<Obstacle>& pointObstacles,
                   const std::vector<const TrackEntity*>& tracks) {
    for (const Obstacle& obstacle : pointObstacles) {
        if (center.distanceTo(obstacle.position) < clearance + obstacle.radius) return false;
    }
    for (const TrackEntity* track : tracks) {
        if (track->distanceTo(center) < clearance + track->width() / 2.0) return false;
    }
    return true;
}

} // namespace

std::vector<EntityId> buildCopperPourWithClearance(Document& doc, LayerId layer, const std::vector<Point2D>& boundary,
                                                    const std::vector<Point2D>& ownNetPositions, double gridSize,
                                                    double clearance) {
    std::vector<EntityId> created;
    if (boundary.size() < 3 || gridSize <= 1e-9) return created;

    std::vector<Obstacle> pointObstacles;
    std::vector<const TrackEntity*> tracks;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Via) {
            const auto* via = static_cast<const ViaEntity*>(e);
            if (!nearExempt(via->position(), ownNetPositions)) {
                pointObstacles.push_back({via->position(), via->diameter() / 2.0});
            }
        } else if (e->type() == EntityType::Track) {
            const auto* track = static_cast<const TrackEntity*>(e);
            const bool exempt = std::any_of(track->vertices().begin(), track->vertices().end(),
                                            [&](const Point2D& v) { return nearExempt(v, ownNetPositions); });
            if (!exempt) tracks.push_back(track);
        } else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (!insert->block() || !insert->block()->isFootprint()) continue;
            for (const auto& padWorld : insert->padWorldPositions()) {
                if (nearExempt(padWorld.position, ownNetPositions)) continue;
                pointObstacles.push_back({padWorld.position, std::max(padWorld.pad->width, padWorld.pad->height) / 2.0});
            }
        }
    }

    double minX = std::numeric_limits<double>::max(), minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest(), maxY = std::numeric_limits<double>::lowest();
    for (const Point2D& p : boundary) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }

    const int nx = std::max(1, static_cast<int>(std::ceil((maxX - minX) / gridSize)));
    const int ny = std::max(1, static_cast<int>(std::ceil((maxY - minY) / gridSize)));

    for (int j = 0; j < ny; ++j) {
        int runStart = -1;
        auto flushRun = [&](int endExclusive) {
            if (runStart < 0) return;
            const double x1 = minX + runStart * gridSize;
            const double x2 = minX + endExclusive * gridSize;
            const double y1 = minY + j * gridSize;
            const double y2 = minY + (j + 1) * gridSize;
            const EntityId id = doc.reserveEntityId();
            doc.addEntity(std::make_unique<HatchEntity>(
                id, layer, std::vector<Point2D>{Point2D(x1, y1), Point2D(x2, y1), Point2D(x2, y2), Point2D(x1, y2)}));
            created.push_back(id);
            runStart = -1;
        };

        for (int i = 0; i < nx; ++i) {
            const Point2D center(minX + (i + 0.5) * gridSize, minY + (j + 0.5) * gridSize);
            const bool keep = pointInPolygon(center, boundary) && clearanceOk(center, clearance, pointObstacles, tracks);
            if (keep) {
                if (runStart < 0) runStart = i;
            } else {
                flushRun(i);
            }
        }
        flushRun(nx);
    }

    return created;
}

} // namespace lcad
