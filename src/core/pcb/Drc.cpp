#include "core/pcb/Drc.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <tuple>

namespace lcad {

namespace {

constexpr double kEpsilon = 1e-6;
using Pos2D = std::pair<std::int64_t, std::int64_t>;
using Key = std::tuple<std::int64_t, std::int64_t, int>; // quantized x, y, and stackup layer index

Pos2D quantize(const Point2D& p) {
    return {static_cast<std::int64_t>(std::llround(p.x / kEpsilon)), static_cast<std::int64_t>(std::llround(p.y / kEpsilon))};
}

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

// A capsule from a to b (a == b for a circular pad/via) with radius,
// belonging to entity ownerId and connectivity-graph root ufRoot.
// layerTag is a resolved stackup layer index for a Track capsule when a
// stackup is active, or -1 for everything else (Via/Pad capsules, or any
// capsule when no stackup was given) -- -1 means "interacts with every
// layer," preserving the original layer-agnostic clearance behavior.
struct Capsule {
    EntityId ownerId;
    Point2D a, b;
    double radius;
    std::size_t ufRoot;
    int layerTag = -1;
    std::string netName; // resolved net class lookup key -- see runDrc's own comment
};

// The larger of the two sides' own net-class clearance, or rules'
// single global one if either side has no resolved net/matching class
// (nets/netClasses not supplied, or a net with no class of its own) --
// the standard KiCad "clearance between two nets is the stricter of
// their two classes" convention.
double effectiveClearance(const DrcRules& rules, const std::vector<NetClass>& netClasses, const std::string& netA,
                          const std::string& netB) {
    const NetClass* classA = findNetClass(netClasses, netA);
    const NetClass* classB = findNetClass(netClasses, netB);
    double clearance = rules.minClearance;
    if (classA) clearance = std::max(clearance, classA->clearance);
    if (classB) clearance = std::max(clearance, classB->clearance);
    return clearance;
}

double pointSegmentDistance(const Point2D& p, const Point2D& a, const Point2D& b) {
    const Point2D seg = b - a;
    const double lenSq = seg.dot(seg);
    double t = lenSq < 1e-12 ? 0.0 : (p - a).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return p.distanceTo(a + seg * t);
}

// Closest approach between segments a1-b1 and a2-b2 (exact only when they
// don't cross; if they do, this returns 0, which is correct for our
// purposes -- a crossing means zero clearance).
double segmentSegmentDistance(const Point2D& a1, const Point2D& b1, const Point2D& a2, const Point2D& b2) {
    return std::min({pointSegmentDistance(a1, a2, b2), pointSegmentDistance(b1, a2, b2),
                     pointSegmentDistance(a2, a1, b1), pointSegmentDistance(b2, a1, b1)});
}

} // namespace

std::vector<DrcViolation> runDrc(const Document& doc, const DrcRules& rules, const CopperStackup& stackup,
                                 const std::vector<ImportedNet>& nets, const std::vector<NetClass>& netClasses) {
    std::vector<DrcViolation> violations;

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

    std::map<std::pair<std::string, std::string>, std::string> pinToNet;
    for (const ImportedNet& net : nets) {
        for (const ImportedNetPin& pin : net.pins) pinToNet[{pin.refDes, pin.pinNumber}] = net.name;
    }

    for (const auto* via : vias) {
        if (via->diameter() < rules.minViaDiameter) {
            violations.push_back({"Via diameter " + std::to_string(via->diameter()) + " is under the minimum " +
                                      std::to_string(rules.minViaDiameter),
                                  via->id()});
        }
        if (via->drillDiameter() < rules.minViaDrillDiameter) {
            violations.push_back({"Via drill " + std::to_string(via->drillDiameter()) + " is under the minimum " +
                                      std::to_string(rules.minViaDrillDiameter),
                                  via->id()});
        }
        if (via->drillDiameter() >= via->diameter()) {
            violations.push_back({"Via drill is not smaller than its own pad diameter", via->id()});
        }
    }

    // With no stackup, every track's tag is 0 -- one shared copper plane,
    // identical to the original layer-agnostic behavior (see Stackup.h).
    const bool stackupActive = !stackup.layers.empty();
    auto layerTagOf = [&](LayerId layer) -> int {
        if (!stackupActive) return 0;
        for (std::size_t i = 0; i < stackup.layers.size(); ++i) {
            if (stackup.layers[i] == layer) return static_cast<int>(i);
        }
        return -1;
    };
    const int lastTag = stackupActive ? static_cast<int>(stackup.layers.size()) - 1 : 0;

    // Copper connectivity graph, same shape as computeRatsnest's.
    std::size_t totalVerts = 0;
    for (const auto* t : tracks) totalVerts += t->vertices().size();
    std::size_t padCount = 0;
    for (const auto* fp : footprints) padCount += fp->block()->pads.size();
    UnionFind uf(totalVerts + vias.size() + padCount);

    std::vector<std::vector<std::size_t>> trackVertexUf(tracks.size());
    std::vector<int> trackTag(tracks.size());
    std::size_t next = 0;
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        trackTag[ti] = layerTagOf(tracks[ti]->layer());
        const auto& verts = tracks[ti]->vertices();
        trackVertexUf[ti].resize(verts.size());
        for (std::size_t vi = 0; vi < verts.size(); ++vi) {
            trackVertexUf[ti][vi] = next++;
            if (vi > 0) uf.unite(trackVertexUf[ti][vi], trackVertexUf[ti][vi - 1]);
        }
    }
    std::vector<std::size_t> viaUf(vias.size());
    for (std::size_t i = 0; i < vias.size(); ++i) viaUf[i] = next++;

    std::map<Key, std::vector<std::size_t>> buckets;
    auto addToBucket = [&](const Point2D& p, int tag, std::size_t ufIndex) {
        if (tag < 0) return;
        const Pos2D pos = quantize(p);
        buckets[{pos.first, pos.second, tag}].push_back(ufIndex);
    };
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        const auto& verts = tracks[ti]->vertices();
        if (verts.empty()) continue;
        addToBucket(verts.front(), trackTag[ti], trackVertexUf[ti].front());
        if (verts.size() > 1) addToBucket(verts.back(), trackTag[ti], trackVertexUf[ti].back());
    }
    for (std::size_t i = 0; i < vias.size(); ++i) {
        int loTag = 0, hiTag = lastTag;
        if (stackupActive && !vias[i]->throughHole) {
            const int a = layerTagOf(vias[i]->fromLayer);
            const int b = layerTagOf(vias[i]->toLayer);
            if (a >= 0 && b >= 0) {
                loTag = std::min(a, b);
                hiTag = std::max(a, b);
            }
        }
        const Pos2D pos = quantize(vias[i]->position());
        for (int tag = loTag; tag <= hiTag; ++tag) buckets[{pos.first, pos.second, tag}].push_back(viaUf[i]);
    }

    // Build capsules, assigning pads their own fresh UF slots and unioning
    // them into the pool by coincidence with a track endpoint/via. A
    // through-hole pad (drillDiameter > 0) touches every stackup layer at
    // its position, same as a via; a surface-mount pad exists only on the
    // ONE layer its own footprint is placed on (see Stackup.h's own
    // comment) -- bucketed/tagged accordingly so clearance and
    // connectivity both respect it instead of treating every pad as
    // spanning the whole stackup.
    std::vector<Capsule> capsules;
    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        const auto& verts = tracks[ti]->vertices();
        for (std::size_t vi = 0; vi + 1 < verts.size(); ++vi) {
            capsules.push_back({tracks[ti]->id(), verts[vi], verts[vi + 1], tracks[ti]->width() / 2.0,
                                trackVertexUf[ti][vi], trackTag[ti], {}});
        }
    }
    for (std::size_t i = 0; i < vias.size(); ++i) {
        capsules.push_back({vias[i]->id(), vias[i]->position(), vias[i]->position(), vias[i]->diameter() / 2.0,
                            viaUf[i], -1, {}});
    }
    for (const auto* fp : footprints) {
        const std::string* refDes = fp->attributeValue("REFDES");
        const int fpTag = layerTagOf(fp->layer());
        for (const auto& padWorld : fp->padWorldPositions()) {
            const std::size_t ufIndex = next++;
            const bool throughHole = padWorld.pad->drillDiameter > 1e-9;
            int padCapsuleTag = -1; // -1 = spans every layer (through-hole, no stackup, or unresolved placement)
            if (throughHole || !stackupActive || fpTag < 0) {
                for (int tag = 0; tag <= lastTag; ++tag) addToBucket(padWorld.position, tag, ufIndex);
            } else {
                addToBucket(padWorld.position, fpTag, ufIndex);
                padCapsuleTag = fpTag;
            }
            std::string netName;
            if (refDes) {
                const auto it = pinToNet.find({*refDes, padWorld.pad->number});
                if (it != pinToNet.end()) netName = it->second;
            }
            capsules.push_back(
                {fp->id(), padWorld.position, padWorld.position, std::max(padWorld.pad->width, padWorld.pad->height) / 2.0,
                 ufIndex, padCapsuleTag, netName});
        }
    }

    for (auto& [key, indices] : buckets) {
        (void)key;
        for (std::size_t i = 1; i < indices.size(); ++i) uf.unite(indices[0], indices[i]);
    }
    for (Capsule& c : capsules) c.ufRoot = uf.find(c.ufRoot);

    // Propagate each pad's resolved net name to every OTHER capsule
    // (tracks, vias) sharing its same connectivity root -- neither
    // stores a net name of its own, but electrically they're on whatever
    // net any pad in their component resolved to.
    std::map<std::size_t, std::string> rootNetName;
    for (const Capsule& c : capsules) {
        if (!c.netName.empty()) rootNetName.emplace(c.ufRoot, c.netName);
    }
    for (Capsule& c : capsules) {
        if (c.netName.empty()) {
            const auto it = rootNetName.find(c.ufRoot);
            if (it != rootNetName.end()) c.netName = it->second;
        }
    }

    for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
        const std::string& netName = rootNetName.count(uf.find(trackVertexUf[ti].front()))
                                       ? rootNetName.at(uf.find(trackVertexUf[ti].front()))
                                       : std::string();
        const NetClass* netClass = findNetClass(netClasses, netName);
        const double minWidth = netClass ? netClass->trackWidth : rules.minTrackWidth;
        if (tracks[ti]->width() < minWidth) {
            violations.push_back({"Track width " + std::to_string(tracks[ti]->width()) + " is under the minimum " +
                                      std::to_string(minWidth) +
                                      (netClass ? " for net class \"" + netClass->name + "\"" : ""),
                                  tracks[ti]->id()});
        }
    }

    for (std::size_t i = 0; i < capsules.size(); ++i) {
        for (std::size_t j = i + 1; j < capsules.size(); ++j) {
            const Capsule& c1 = capsules[i];
            const Capsule& c2 = capsules[j];
            if (c1.ownerId == c2.ownerId || c1.ufRoot == c2.ufRoot) continue;
            if (stackupActive && c1.layerTag >= 0 && c2.layerTag >= 0 && c1.layerTag != c2.layerTag) continue;
            const double gap = segmentSegmentDistance(c1.a, c1.b, c2.a, c2.b) - c1.radius - c2.radius;
            const double clearance = effectiveClearance(rules, netClasses, c1.netName, c2.netName);
            if (gap < clearance) {
                violations.push_back({"Clearance violation: " + std::to_string(gap < 0 ? 0.0 : gap) +
                                          " between unconnected copper (minimum " + std::to_string(clearance) + ")",
                                      c1.ownerId});
            }
        }
    }

    if (rules.checkCourtyards) {
        std::vector<std::pair<EntityId, BoundingBox>> courtyards;
        for (const auto* fp : footprints) {
            BoundingBox box = fp->boundingBox();
            if (!box.isValid()) continue;
            box.min.x -= rules.courtyardMargin;
            box.min.y -= rules.courtyardMargin;
            box.max.x += rules.courtyardMargin;
            box.max.y += rules.courtyardMargin;
            courtyards.push_back({fp->id(), box});
        }
        for (std::size_t i = 0; i < courtyards.size(); ++i) {
            for (std::size_t j = i + 1; j < courtyards.size(); ++j) {
                const BoundingBox& a = courtyards[i].second;
                const BoundingBox& b = courtyards[j].second;
                const bool overlap = a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y;
                if (overlap) violations.push_back({"Courtyard overlap between footprints", courtyards[i].first});
            }
        }
    }

    if (rules.checkSilkscreenOverPad) {
        struct PadInfo {
            Point2D position;
            double radius;
        };
        std::vector<PadInfo> allPadInfos;
        for (const auto* fp : footprints) {
            for (const auto& padWorld : fp->padWorldPositions()) {
                allPadInfos.push_back({padWorld.position, std::max(padWorld.pad->width, padWorld.pad->height) / 2.0});
            }
        }
        for (const auto* fp : footprints) {
            const std::vector<std::unique_ptr<Entity>> bodyEntities = fp->instantiate();
            for (const auto& bodyEntity : bodyEntities) {
                for (const PadInfo& pad : allPadInfos) {
                    const double gap = bodyEntity->distanceTo(pad.position) - pad.radius;
                    if (gap < rules.silkscreenClearance) {
                        violations.push_back({"Silkscreen over pad (gap " + std::to_string(gap) + ")", fp->id()});
                    }
                }
            }
        }
    }

    return violations;
}

} // namespace lcad
