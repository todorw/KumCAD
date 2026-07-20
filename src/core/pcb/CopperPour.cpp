#include "core/pcb/CopperPour.h"

#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>

namespace lcad {

namespace {

// Same union-find-by-coincident-endpoint connectivity tracer Drc.cpp and
// Ratsnest.cpp each already build their own copy of, applied here so
// exemption from the pour's own clearance check follows real electrical
// connectivity (a track or via reaching an own-net pad/point through a
// chain of other tracks/vias) instead of only exact coincidence with one
// of ownNetPositions -- closing the limitation this file's own header
// comment used to disclose.
class UnionFind {
public:
    explicit UnionFind(std::size_t n) : m_parent(n) { std::iota(m_parent.begin(), m_parent.end(), 0); }
    std::size_t find(std::size_t a) {
        while (m_parent[a] != a) {
            m_parent[a] = m_parent[m_parent[a]];
            a = m_parent[a];
        }
        return a;
    }
    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a != b) m_parent[a] = b;
    }

private:
    std::vector<std::size_t> m_parent;
};

using Pos2D = std::pair<std::int64_t, std::int64_t>;
Pos2D quantizeCoincidence(const Point2D& p) {
    // Matches this file's own long-standing 1e-3 coincidence tolerance.
    constexpr double kEpsilon = 1e-3;
    return {static_cast<std::int64_t>(std::llround(p.x / kEpsilon)), static_cast<std::int64_t>(std::llround(p.y / kEpsilon))};
}

// True for every via/track/pad reachable, through a chain of touching
// copper, from one of ownNetPositions -- not just the ones exactly AT an
// own-net position.
struct Connectivity {
    std::vector<bool> viaExempt;
    std::vector<bool> trackExempt;
    // Parallel to the (via, track, pad) iteration order buildCopperPourWithClearance
    // itself walks doc.entities() in -- padExempt is looked up by pad world
    // position instead, since pads don't get their own std::vector here.
    std::map<Pos2D, bool> padExemptByPosition;
};

Connectivity traceConnectivity(const Document& doc, const std::vector<Point2D>& ownNetPositions) {
    Connectivity result;

    std::vector<const TrackEntity*> tracks;
    std::vector<const ViaEntity*> vias;
    std::vector<const InsertEntity*> footprints;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Track) tracks.push_back(static_cast<const TrackEntity*>(e));
        else if (e->type() == EntityType::Via) vias.push_back(static_cast<const ViaEntity*>(e));
        else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (insert->block() && insert->block()->isFootprint()) footprints.push_back(insert);
        }
    }

    std::size_t totalVerts = 0;
    for (const auto* t : tracks) totalVerts += t->vertices().size();
    std::size_t padCount = 0;
    for (const auto* fp : footprints) padCount += fp->block()->pads.size();
    UnionFind uf(totalVerts + vias.size() + padCount + ownNetPositions.size());

    std::vector<std::vector<std::size_t>> trackVertexUf(tracks.size());
    std::size_t next = 0;
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        const auto& verts = tracks[ti]->vertices();
        trackVertexUf[ti].resize(verts.size());
        for (std::size_t vi = 0; vi < verts.size(); ++vi) {
            trackVertexUf[ti][vi] = next++;
            if (vi > 0) uf.unite(trackVertexUf[ti][vi], trackVertexUf[ti][vi - 1]);
        }
    }
    std::vector<std::size_t> viaUf(vias.size());
    for (std::size_t i = 0; i < vias.size(); ++i) viaUf[i] = next++;

    std::map<Pos2D, std::vector<std::size_t>> buckets;
    auto addToBucket = [&](const Point2D& p, std::size_t ufIndex) { buckets[quantizeCoincidence(p)].push_back(ufIndex); };
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        const auto& verts = tracks[ti]->vertices();
        if (verts.empty()) continue;
        addToBucket(verts.front(), trackVertexUf[ti].front());
        if (verts.size() > 1) addToBucket(verts.back(), trackVertexUf[ti].back());
    }
    for (std::size_t i = 0; i < vias.size(); ++i) addToBucket(vias[i]->position(), viaUf[i]);

    std::vector<std::size_t> padUf;
    std::vector<Point2D> padPositions;
    for (const auto* fp : footprints) {
        for (const auto& padWorld : fp->padWorldPositions()) {
            const std::size_t ufIndex = next++;
            addToBucket(padWorld.position, ufIndex);
            padUf.push_back(ufIndex);
            padPositions.push_back(padWorld.position);
        }
    }

    std::vector<std::size_t> anchorUf(ownNetPositions.size());
    for (std::size_t i = 0; i < ownNetPositions.size(); ++i) {
        anchorUf[i] = next++;
        addToBucket(ownNetPositions[i], anchorUf[i]);
    }

    for (auto& [key, indices] : buckets) {
        (void)key;
        for (std::size_t i = 1; i < indices.size(); ++i) uf.unite(indices[0], indices[i]);
    }

    std::vector<std::size_t> ownRoots;
    for (std::size_t a : anchorUf) ownRoots.push_back(uf.find(a));
    auto isOwnRoot = [&](std::size_t root) {
        return std::find(ownRoots.begin(), ownRoots.end(), root) != ownRoots.end();
    };

    result.trackExempt.resize(tracks.size(), false);
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        if (!trackVertexUf[ti].empty()) result.trackExempt[ti] = isOwnRoot(uf.find(trackVertexUf[ti].front()));
    }
    result.viaExempt.resize(vias.size(), false);
    for (std::size_t i = 0; i < vias.size(); ++i) result.viaExempt[i] = isOwnRoot(uf.find(viaUf[i]));
    for (std::size_t i = 0; i < padUf.size(); ++i) {
        result.padExemptByPosition[quantizeCoincidence(padPositions[i])] = isOwnRoot(uf.find(padUf[i]));
    }

    return result;
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

// True if cellCenter falls inside the thermal-relief annular keepout of
// some own-net pad (within antipadRadius) but NOT inside one of that
// pad's spoke corridors -- i.e. this cell should be excluded from the
// pour even though it's on the pour's own net.
bool inThermalKeepout(const Point2D& cellCenter, const std::vector<Point2D>& ownNetPositions,
                      const ThermalReliefParams& relief) {
    if (!relief.enabled || relief.spokeCount <= 0) return false;
    constexpr double kPi = 3.14159265358979323846;
    for (const Point2D& pad : ownNetPositions) {
        const Point2D offset = cellCenter - pad;
        const double dist = offset.length();
        if (dist >= relief.antipadRadius) continue; // outside this pad's ring: not this pad's concern

        bool inSpoke = false;
        for (int s = 0; s < relief.spokeCount; ++s) {
            const double angle = s * 2.0 * kPi / relief.spokeCount;
            const double dx = std::cos(angle), dy = std::sin(angle);
            const double perp = std::abs(offset.x * dy - offset.y * dx); // distance to the spoke's centerline
            if (perp <= relief.spokeWidth / 2.0) {
                inSpoke = true;
                break;
            }
        }
        if (!inSpoke) return true; // inside the ring, no spoke here: keepout
    }
    return false;
}

} // namespace

std::vector<EntityId> buildCopperPourWithClearance(Document& doc, LayerId layer, const std::vector<Point2D>& boundary,
                                                    const std::vector<Point2D>& ownNetPositions, double gridSize,
                                                    double clearance, const ThermalReliefParams& thermalRelief,
                                                    const std::vector<KeepoutZone>& keepouts) {
    std::vector<EntityId> created;
    if (boundary.size() < 3 || gridSize <= 1e-9) return created;

    const Connectivity connectivity = traceConnectivity(doc, ownNetPositions);

    std::vector<Obstacle> pointObstacles;
    std::vector<const TrackEntity*> tracks;
    std::size_t viaIndex = 0, trackIndex = 0;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Via) {
            const auto* via = static_cast<const ViaEntity*>(e);
            const bool exempt = viaIndex < connectivity.viaExempt.size() && connectivity.viaExempt[viaIndex];
            ++viaIndex;
            if (!exempt) pointObstacles.push_back({via->position(), via->diameter() / 2.0});
        } else if (e->type() == EntityType::Track) {
            const auto* track = static_cast<const TrackEntity*>(e);
            const bool exempt = trackIndex < connectivity.trackExempt.size() && connectivity.trackExempt[trackIndex];
            ++trackIndex;
            if (!exempt) tracks.push_back(track);
        } else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (!insert->block() || !insert->block()->isFootprint()) continue;
            for (const auto& padWorld : insert->padWorldPositions()) {
                const auto it = connectivity.padExemptByPosition.find(quantizeCoincidence(padWorld.position));
                const bool exempt = it != connectivity.padExemptByPosition.end() && it->second;
                if (exempt) continue;
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
            const bool keep = pointInPolygon(center, boundary) &&
                              clearanceOk(center, clearance, pointObstacles, tracks) &&
                              !inThermalKeepout(center, ownNetPositions, thermalRelief) &&
                              !pointInKeepout(center, layer, keepouts, /*forPour=*/true);
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
