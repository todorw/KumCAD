#include "core/pcb/Autorouter.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/pcb/Ratsnest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>

namespace lcad {

namespace {

// layerIdx indexes the `layers` vector runRoutingPass is given (NOT a
// Document LayerId directly) -- 0 for the whole grid in legacy
// single-layer mode, where no via-switch move is ever generated since
// there's no other layer index to switch to.
struct GridCell {
    int x = 0, y = 0, layerIdx = 0;
    bool operator==(const GridCell& o) const { return x == o.x && y == o.y && layerIdx == o.layerIdx; }
};

struct PlacedPad {
    Point2D position;
    double radius;
    std::string netName; // empty if this pad isn't part of any net being routed
};

struct RoutedSegment {
    Point2D a, b;
    int layerIdx = 0;
    std::string netName;
    double trackWidth = 0.25;
    double clearance = 0.2;
};

// A layer-transition via this pass itself inserted -- through-hole, so it
// blocks every layer's grid for other nets the same way a pad does (see
// runMultiLayerObstacles' own comment).
struct RoutedVia {
    Point2D position;
    std::string netName;
    double diameter = 0.6;
    double clearance = 0.2;
};

struct PendingConnection {
    RatsnestLine line;
    std::string netName;
};

struct RoutingPass {
    AutorouteResult result;
    std::vector<EntityId> addedEntityIds; // tracks AND vias this pass added, for ripping up a losing attempt
    // Indices into the orderedPending list this pass was given, for
    // whichever connections failed -- used to rebuild the next rip-up
    // attempt's ordering without matching on netName strings (which
    // aren't unique: a multi-pin net's own MST can produce more than one
    // PendingConnection sharing the same net name).
    std::vector<int> failedIndices;
};

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

GridCell toCell(const Point2D& p, double minX, double minY, double gridSize, int layerIdx) {
    return {static_cast<int>((p.x - minX) / gridSize), static_cast<int>((p.y - minY) / gridSize), layerIdx};
}

Point2D toWorld(const GridCell& c, double minX, double minY, double gridSize) {
    return Point2D(minX + (c.x + 0.5) * gridSize, minY + (c.y + 0.5) * gridSize);
}

// Classic Lee/maze breadth-first search, 8-connected within a layer, PLUS
// a via-switch move (cost 1, same as any grid step) to every OTHER
// layer at the same (x,y) -- a real through-hole via, reachable from/to
// any layer, matching ViaEntity's own default throughHole=true
// semantics; this router never creates a blind/buried via. Multi-source/
// multi-target: a footprint pad is reachable from (and a valid
// destination on) every layer, matching Stackup.h's own disclosed "pads
// have no per-pad layer yet" simplification, so BFS starts from
// start's own (x,y) on EVERY layer simultaneously and accepts reaching
// goal's (x,y) on ANY layer. With exactly one layer (legacy single-layer
// mode), this degenerates to exactly the original 2D single-source
// search -- no via move is ever generated since there's no other layer
// index to switch to.
//
// Real, disclosed simplification: a via-switch move is only checked
// against the DESTINATION layer's own obstacle grid at (x,y), not every
// layer the via barrel would physically pass through -- correct for a
// 2-layer stack (the common case), an approximation for 3+ layers. Once
// a connection's own via is placed, it's marked as an obstacle on EVERY
// layer for later connections (see runMultiLayerObstacles), so this
// simplification only affects whether THIS SAME connection's own path
// can freely pass through a layer it doesn't yet know is about to become
// crowded by its own via -- a self-consistency edge case, not a
// clearance violation risk to other nets.
std::vector<GridCell> bfsRoute(const std::vector<std::vector<bool>>& obstacle, int nx, int ny, int numLayers,
                               GridCell start, GridCell goal) {
    auto inBounds = [&](GridCell c) { return c.x >= 0 && c.x < nx && c.y >= 0 && c.y < ny; };
    auto idx2d = [&](GridCell c) { return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(c.x); };
    auto idx3d = [&](GridCell c) {
        return static_cast<std::size_t>(c.layerIdx) * static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) + idx2d(c);
    };
    if (!inBounds(start) || !inBounds(goal)) return {};

    const std::size_t total = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * static_cast<std::size_t>(numLayers);
    std::vector<bool> visited(total, false);
    std::vector<int> parent(total, -1);
    std::queue<GridCell> queue;

    for (int l = 0; l < numLayers; ++l) {
        const GridCell src{start.x, start.y, l};
        if (obstacle[static_cast<std::size_t>(l)][idx2d(src)] && !(src.x == goal.x && src.y == goal.y)) continue;
        if (!visited[idx3d(src)]) {
            visited[idx3d(src)] = true;
            queue.push(src);
        }
    }

    static const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    bool found = start.x == goal.x && start.y == goal.y;
    GridCell reachedGoal{goal.x, goal.y, found ? start.layerIdx : 0};
    while (!queue.empty() && !found) {
        const GridCell current = queue.front();
        queue.pop();

        // 8-connected in-layer moves.
        for (int d = 0; d < 8 && !found; ++d) {
            const GridCell next{current.x + kDx[d], current.y + kDy[d], current.layerIdx};
            if (!inBounds(next) || visited[idx3d(next)]) continue;
            const bool isGoal = next.x == goal.x && next.y == goal.y;
            if (obstacle[static_cast<std::size_t>(next.layerIdx)][idx2d(next)] && !isGoal) continue;
            visited[idx3d(next)] = true;
            parent[idx3d(next)] = static_cast<int>(idx3d(current));
            if (isGoal) {
                found = true;
                reachedGoal = next;
                break;
            }
            queue.push(next);
        }
        if (found) break;

        // Via-switch moves to every other layer at the same (x,y).
        for (int l = 0; l < numLayers && !found; ++l) {
            if (l == current.layerIdx) continue;
            const GridCell next{current.x, current.y, l};
            if (visited[idx3d(next)]) continue;
            const bool isGoal = next.x == goal.x && next.y == goal.y;
            if (obstacle[static_cast<std::size_t>(l)][idx2d(next)] && !isGoal) continue;
            visited[idx3d(next)] = true;
            parent[idx3d(next)] = static_cast<int>(idx3d(current));
            if (isGoal) {
                found = true;
                reachedGoal = next;
                break;
            }
            queue.push(next);
        }
    }
    if (!found) return {};

    std::vector<GridCell> path;
    int cur = static_cast<int>(idx3d(reachedGoal));
    const std::size_t plane = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    while (cur != -1) {
        const std::size_t u = static_cast<std::size_t>(cur);
        path.push_back({static_cast<int>(u % plane) % nx, static_cast<int>(u % plane) / nx, static_cast<int>(u / plane)});
        cur = parent[u];
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

// One shortest-first Lee/maze pass over orderedPending, in the order
// given (the caller controls ordering -- the initial pass sorts
// shortest-first; a rip-up retry instead puts the previous attempt's
// failures first). Adds real TrackEntity/ViaEntity geometry to doc per
// success -- one TrackEntity per layer-contiguous run of the found path,
// one ViaEntity at each layer transition between runs.
RoutingPass runRoutingPass(Document& doc, const std::vector<PendingConnection>& orderedPending,
                           const std::vector<PlacedPad>& allPads, int nx, int ny, double minX, double minY,
                           const std::vector<LayerId>& layers, const AutorouteParams& params,
                           const std::vector<NetClass>& netClasses) {
    RoutingPass pass;
    std::vector<RoutedSegment> routedSegments;
    std::vector<RoutedVia> routedVias;
    const int numLayers = static_cast<int>(layers.size());
    const std::size_t planeSize = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);

    for (std::size_t idx = 0; idx < orderedPending.size(); ++idx) {
        const PendingConnection& conn = orderedPending[idx];
        // netClasses (see NetClass.h) lets different nets route with
        // their own track width/clearance -- a net with no matching
        // class still falls back to params' own single global values,
        // exactly the pre-existing behavior when netClasses is empty.
        const NetClass* connClass = findNetClass(netClasses, conn.netName);
        const double connTrackWidth = connClass ? connClass->trackWidth : params.trackWidth;
        const double connClearance = connClass ? connClass->clearance : params.clearance;

        std::vector<std::vector<bool>> obstacle(static_cast<std::size_t>(numLayers), std::vector<bool>(planeSize, false));
        for (const PlacedPad& pad : allPads) {
            if (!pad.netName.empty() && pad.netName == conn.netName) continue;
            // Clearance to another net's copper uses the LARGER of the
            // two nets' own required clearances (the same convention
            // Drc.h's own per-net-class check uses), not just this
            // connection's own. A pad blocks every layer's grid (see
            // Stackup.h's own disclosed "pads have no per-pad layer yet"
            // simplification).
            const NetClass* padClass = findNetClass(netClasses, pad.netName);
            const double padClearance = std::max(connClearance, padClass ? padClass->clearance : params.clearance);
            const double radius = pad.radius + connTrackWidth / 2.0 + padClearance;
            for (auto& layerGrid : obstacle) markNearPoint(layerGrid, nx, ny, minX, minY, params.gridSize, pad.position, radius);
        }
        for (const RoutedSegment& seg : routedSegments) {
            if (seg.netName == conn.netName) continue;
            const double segClearance = std::max(connClearance, seg.clearance);
            markNearSegment(obstacle[static_cast<std::size_t>(seg.layerIdx)], nx, ny, minX, minY, params.gridSize, seg.a,
                           seg.b, connTrackWidth / 2.0 + segClearance + seg.trackWidth / 2.0);
        }
        for (const RoutedVia& via : routedVias) {
            if (via.netName == conn.netName) continue;
            // A through-hole via blocks every layer, same as a pad.
            const double viaClearance = std::max(connClearance, via.clearance);
            const double radius = via.diameter / 2.0 + connTrackWidth / 2.0 + viaClearance;
            for (auto& layerGrid : obstacle) markNearPoint(layerGrid, nx, ny, minX, minY, params.gridSize, via.position, radius);
        }

        // Start/goal are reachable on any layer (a pad touches every
        // layer) -- layerIdx 0 here is just a placeholder slot bfsRoute's
        // own multi-source/multi-target search ignores in favor of
        // trying every layer itself.
        const GridCell start = toCell(conn.line.a, minX, minY, params.gridSize, 0);
        const GridCell goal = toCell(conn.line.b, minX, minY, params.gridSize, 0);
        auto clearCellAllLayers = [&](GridCell c) {
            if (c.x < 0 || c.x >= nx || c.y < 0 || c.y >= ny) return;
            const std::size_t flat = static_cast<std::size_t>(c.y) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(c.x);
            for (auto& layerGrid : obstacle) layerGrid[flat] = false;
        };
        clearCellAllLayers(start);
        clearCellAllLayers(goal);

        const std::vector<GridCell> cellPath = bfsRoute(obstacle, nx, ny, numLayers, start, goal);
        if (cellPath.empty()) {
            ++pass.result.failedCount;
            pass.result.failedNetNames.push_back(conn.netName);
            pass.failedIndices.push_back(static_cast<int>(idx));
            continue;
        }

        // Split into layer-contiguous runs; a run becomes one
        // TrackEntity, and each transition between runs becomes one
        // through-hole ViaEntity at that (x,y).
        std::size_t runStart = 0;
        for (std::size_t i = 1; i <= cellPath.size(); ++i) {
            const bool endOfRun = i == cellPath.size() || cellPath[i].layerIdx != cellPath[runStart].layerIdx;
            if (!endOfRun) continue;

            std::vector<Point2D> worldPath;
            worldPath.reserve(i - runStart);
            for (std::size_t k = runStart; k < i; ++k) worldPath.push_back(toWorld(cellPath[k], minX, minY, params.gridSize));
            if (runStart == 0) worldPath.front() = conn.line.a; // snap the very first point to the exact pad position
            if (i == cellPath.size()) worldPath.back() = conn.line.b; // ...and the very last

            if (worldPath.size() >= 2) {
                const std::vector<Point2D> simplified = simplifyPath(worldPath);
                const LayerId trackLayer = layers[static_cast<std::size_t>(cellPath[runStart].layerIdx)];
                auto track = std::make_unique<TrackEntity>(doc.reserveEntityId(), trackLayer, simplified, connTrackWidth);
                const EntityId trackId = track->id();
                doc.addEntity(std::move(track));
                pass.addedEntityIds.push_back(trackId);
                for (std::size_t k = 0; k + 1 < simplified.size(); ++k) {
                    routedSegments.push_back(
                        {simplified[k], simplified[k + 1], cellPath[runStart].layerIdx, conn.netName, connTrackWidth, connClearance});
                }
            }

            if (i < cellPath.size()) {
                // A layer transition happens here: cellPath[i-1] and
                // cellPath[i] share the same (x,y) (a via-switch move) --
                // place the via at that shared position.
                const Point2D viaPos = toWorld(cellPath[i - 1], minX, minY, params.gridSize);
                auto via = std::make_unique<ViaEntity>(doc.reserveEntityId(), layers.front(), viaPos, params.viaDiameter,
                                                       params.viaDrillDiameter);
                via->throughHole = true;
                const EntityId viaId = via->id();
                doc.addEntity(std::move(via));
                pass.addedEntityIds.push_back(viaId);
                routedVias.push_back({viaPos, conn.netName, params.viaDiameter, connClearance});
            }

            runStart = i;
        }
        ++pass.result.routedCount;
    }

    return pass;
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

    // Empty stackup = legacy single-layer mode on params.layer, exactly
    // the original behavior (and, since there's then only one layer
    // index to route on, bfsRoute never generates a via-switch move --
    // no vias are ever inserted in this mode).
    const std::vector<LayerId> layers = params.stackup.layers.empty() ? std::vector<LayerId>{params.layer} : params.stackup.layers;

    std::vector<PendingConnection> pending;
    for (const ImportedNet& net : nets) {
        for (const RatsnestLine& line : computeRatsnest(doc, {net})) pending.push_back({line, net.name});
    }
    std::sort(pending.begin(), pending.end(), [](const PendingConnection& a, const PendingConnection& b) {
        return a.line.a.distanceTo(a.line.b) < b.line.a.distanceTo(b.line.b);
    });

    RoutingPass best = runRoutingPass(doc, pending, allPads, nx, ny, minX, minY, layers, params, netClasses);

    // Rip-up-and-reroute: a real, if simple/global (not localized "rip up
    // exactly the one blocking trace" the way a true push-and-shove
    // router would), technique -- sometimes a connection only failed
    // because an earlier, successfully-routed one happened to claim the
    // only viable corridor first under pure shortest-first ordering.
    // Retrying with the previously-failed connections given first pick
    // this time can recover some of those routes, at the cost of
    // possibly displacing ones that succeeded before; each attempt's
    // tracks/vias are ripped up (removed from doc) before the next
    // attempt, and only the attempt with the fewest failures is kept.
    // Not KiCad's own interactive push-and-shove router -- that needs
    // live mouse-drag physics during routing this batch/typed-command
    // architecture has no interactive viewport plumbing for; this is the
    // bounded alternative that's actually buildable here.
    for (int attempt = 0; attempt < params.ripUpPasses && best.result.failedCount > 0; ++attempt) {
        std::vector<PendingConnection> reordered;
        reordered.reserve(pending.size());
        std::vector<bool> wasFailedIndex(pending.size(), false);
        for (int idx : best.failedIndices) wasFailedIndex[static_cast<std::size_t>(idx)] = true;
        for (int idx : best.failedIndices) reordered.push_back(pending[static_cast<std::size_t>(idx)]);
        for (std::size_t i = 0; i < pending.size(); ++i) {
            if (!wasFailedIndex[i]) reordered.push_back(pending[i]);
        }

        RoutingPass retry = runRoutingPass(doc, reordered, allPads, nx, ny, minX, minY, layers, params, netClasses);
        if (retry.result.failedCount < best.result.failedCount) {
            for (EntityId id : best.addedEntityIds) doc.removeEntity(id);
            best = std::move(retry);
        } else {
            for (EntityId id : retry.addedEntityIds) doc.removeEntity(id);
        }
    }

    return best.result;
}

} // namespace lcad
