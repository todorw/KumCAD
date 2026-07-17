#include "core/pcb/Ratsnest.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <tuple>

namespace lcad {

std::vector<ImportedNet> parseNetlist(const std::string& text) {
    std::vector<ImportedNet> nets;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        // Trim leading/trailing whitespace.
        std::size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) continue;
        std::size_t end = line.find_last_not_of(" \t\r\n");
        const std::string trimmed = line.substr(begin, end - begin + 1);

        if (trimmed.empty() || trimmed.front() == '#') continue;
        if (trimmed.rfind("NET ", 0) == 0) {
            const std::size_t firstQuote = trimmed.find('"');
            const std::size_t lastQuote = trimmed.rfind('"');
            ImportedNet net;
            net.name = (firstQuote != std::string::npos && lastQuote > firstQuote)
                          ? trimmed.substr(firstQuote + 1, lastQuote - firstQuote - 1)
                          : trimmed.substr(4);
            nets.push_back(std::move(net));
            continue;
        }
        const std::size_t dot = trimmed.find('.');
        if (dot == std::string::npos || nets.empty()) continue;
        nets.back().pins.push_back({trimmed.substr(0, dot), trimmed.substr(dot + 1)});
    }
    return nets;
}

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

} // namespace

std::vector<RatsnestLine> computeRatsnest(const Document& doc, const std::vector<ImportedNet>& nets,
                                          const CopperStackup& stackup) {
    std::vector<const TrackEntity*> tracks;
    std::vector<const ViaEntity*> vias;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Track) tracks.push_back(static_cast<const TrackEntity*>(e));
        else if (e->type() == EntityType::Via) vias.push_back(static_cast<const ViaEntity*>(e));
    }

    // With no stackup, every track's tag is 0 -- one shared copper plane,
    // identical to the original layer-agnostic behavior. With a stackup, a
    // track's tag is its layer's index within it, or -1 (isolated -- never
    // bucket-merges with anything by position) if its layer isn't in the
    // stackup at all.
    const bool stackupActive = !stackup.layers.empty();
    auto layerTagOf = [&](LayerId layer) -> int {
        if (!stackupActive) return 0;
        for (std::size_t i = 0; i < stackup.layers.size(); ++i) {
            if (stackup.layers[i] == layer) return static_cast<int>(i);
        }
        return -1;
    };
    const int lastTag = stackupActive ? static_cast<int>(stackup.layers.size()) - 1 : 0;

    // Copper connectivity graph: one UF slot per track vertex, unioned
    // along each track's own path.
    std::size_t totalVerts = 0;
    for (const auto* t : tracks) totalVerts += t->vertices().size();
    UnionFind uf(totalVerts + vias.size());

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
        // A through-hole via spans the whole stackup (or the single shared
        // plane, without one); a blind/buried via spans only its own
        // resolved [fromLayer,toLayer] range.
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
        // A via at a track's interior vertex is a real T-tap (like a
        // schematic Junction), unlike two tracks merely crossing on screen
        // -- but only for tracks on a layer this via actually spans.
        for (std::size_t ti = 0; ti < tracks.size(); ++ti) {
            if (trackTag[ti] < loTag || trackTag[ti] > hiTag) continue;
            const auto& verts = tracks[ti]->vertices();
            for (std::size_t vi = 1; vi + 1 < verts.size(); ++vi) {
                if (quantize(verts[vi]) == pos) buckets[{pos.first, pos.second, trackTag[ti]}].push_back(trackVertexUf[ti][vi]);
            }
        }
    }
    for (auto& [key, indices] : buckets) {
        (void)key;
        for (std::size_t i = 1; i < indices.size(); ++i) uf.unite(indices[0], indices[i]);
    }

    std::vector<RatsnestLine> lines;

    for (const ImportedNet& net : nets) {
        // Resolve each pin to a placed pad's world position, and the copper
        // graph's root at that position (a fresh singleton root if nothing
        // copper touches it yet). Pads have no per-pad layer of their own
        // yet (see Stackup.h's own disclosed simplification), so a pad
        // touches every stackup layer's bucket at its position, same as a
        // through-hole via.
        std::vector<Point2D> padPositions;
        std::vector<std::size_t> padRoots;
        for (const ImportedNetPin& pin : net.pins) {
            const InsertEntity* found = nullptr;
            for (const Entity* e : doc.entities()) {
                if (e->type() != EntityType::Insert) continue;
                const auto* insert = static_cast<const InsertEntity*>(e);
                if (!insert->block() || !insert->block()->isFootprint()) continue;
                const std::string* refdes = insert->attributeValue("REFDES");
                if (refdes && *refdes == pin.refDes) {
                    found = insert;
                    break;
                }
            }
            if (!found) continue;
            for (const auto& padWorld : found->padWorldPositions()) {
                if (padWorld.pad->number != pin.pinNumber) continue;
                const Pos2D pos = quantize(padWorld.position);
                std::vector<std::size_t> touching;
                for (int tag = 0; tag <= lastTag; ++tag) {
                    const auto it = buckets.find({pos.first, pos.second, tag});
                    if (it != buckets.end() && !it->second.empty()) touching.push_back(it->second.front());
                }
                std::size_t root;
                if (!touching.empty()) {
                    for (std::size_t k = 1; k < touching.size(); ++k) uf.unite(touching[0], touching[k]);
                    root = uf.find(touching[0]);
                } else {
                    // No copper here yet: this pad is its own singleton
                    // cluster for MST purposes.
                    root = std::numeric_limits<std::size_t>::max() - padPositions.size();
                }
                padPositions.push_back(padWorld.position);
                padRoots.push_back(root);
                break;
            }
        }

        // Collapse same-root pads into one cluster representative.
        std::map<std::size_t, Point2D> clusters;
        for (std::size_t i = 0; i < padPositions.size(); ++i) clusters.emplace(padRoots[i], padPositions[i]);
        if (clusters.size() < 2) continue;

        // Prim's MST over cluster representatives (small N, O(N^2) is fine).
        std::vector<Point2D> pts;
        for (auto& [root, pt] : clusters) {
            (void)root;
            pts.push_back(pt);
        }
        std::vector<bool> inTree(pts.size(), false);
        inTree[0] = true;
        for (std::size_t added = 1; added < pts.size(); ++added) {
            double bestDist = std::numeric_limits<double>::infinity();
            std::size_t bestFrom = 0, bestTo = 0;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                if (!inTree[i]) continue;
                for (std::size_t j = 0; j < pts.size(); ++j) {
                    if (inTree[j]) continue;
                    const double d = pts[i].distanceTo(pts[j]);
                    if (d < bestDist) {
                        bestDist = d;
                        bestFrom = i;
                        bestTo = j;
                    }
                }
            }
            lines.push_back({pts[bestFrom], pts[bestTo]});
            inTree[bestTo] = true;
        }
    }

    return lines;
}

} // namespace lcad
