#include "core/io/KiCadPcb.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/io/KiCadMod.h"
#include "core/io/SExpr.h"

#include <fstream>
#include <map>
#include <sstream>

namespace lcad {

namespace {

constexpr double kEps = 1e-4;

struct UnionFind {
    std::vector<int> parent;
    explicit UnionFind(int n) : parent(static_cast<std::size_t>(n)) {
        for (int i = 0; i < n; ++i) parent[static_cast<std::size_t>(i)] = i;
    }
    int find(int x) {
        while (parent[static_cast<std::size_t>(x)] != x) {
            parent[static_cast<std::size_t>(x)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
            x = parent[static_cast<std::size_t>(x)];
        }
        return x;
    }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[static_cast<std::size_t>(a)] = b;
    }
};

std::vector<const InsertEntity*> placedFootprints(const Document& doc) {
    std::vector<const InsertEntity*> out;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (insert->block() && insert->block()->isFootprint()) out.push_back(insert);
    }
    return out;
}

// Real touch-connectivity net assignment: a union-find over each net's
// resolved pad positions, every track's own vertices (unioned along each
// track's own length, since a trace connects its own endpoints
// regardless of distance), and every via's position -- see KiCadPcb.h's
// own comment for the algorithm and its disclosed limitation.
struct NetAssignment {
    std::vector<int> trackNet; // parallel to tracks, -1 = unresolved
    std::vector<int> viaNet;   // parallel to vias, -1 = unresolved
};

NetAssignment resolveNets(const Document& doc, const std::vector<ImportedNet>& nets,
                          const std::vector<const TrackEntity*>& tracks, const std::vector<const ViaEntity*>& vias) {
    struct Node {
        Point2D pt;
        int netIndex = -1;
    };
    std::vector<Node> nodes;

    for (std::size_t ni = 0; ni < nets.size(); ++ni) {
        for (const ImportedNetPin& pin : nets[ni].pins) {
            for (const Entity* e : doc.entities()) {
                if (e->type() != EntityType::Insert) continue;
                const auto* insert = static_cast<const InsertEntity*>(e);
                if (!insert->block()) continue;
                const std::string* refdes = insert->attributeValue("REFDES");
                if (!refdes || *refdes != pin.refDes) continue;
                for (const auto& padWorld : insert->padWorldPositions()) {
                    if (padWorld.pad->number == pin.pinNumber) nodes.push_back({padWorld.position, static_cast<int>(ni)});
                }
            }
        }
    }

    std::vector<std::pair<int, int>> trackRange(tracks.size()); // [firstNode, count)
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        trackRange[i] = {static_cast<int>(nodes.size()), static_cast<int>(tracks[i]->vertices().size())};
        for (const Point2D& v : tracks[i]->vertices()) nodes.push_back({v, -1});
    }
    std::vector<int> viaIndex(vias.size());
    for (std::size_t i = 0; i < vias.size(); ++i) {
        viaIndex[i] = static_cast<int>(nodes.size());
        nodes.push_back({vias[i]->position(), -1});
    }

    UnionFind uf(static_cast<int>(nodes.size()));
    for (const auto& [first, count] : trackRange) {
        for (int k = 1; k < count; ++k) uf.unite(first + k - 1, first + k);
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (std::size_t j = i + 1; j < nodes.size(); ++j) {
            if (nodes[i].pt.distanceTo(nodes[j].pt) < kEps) uf.unite(static_cast<int>(i), static_cast<int>(j));
        }
    }

    std::map<int, int> rootToNet;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].netIndex < 0) continue;
        const int root = uf.find(static_cast<int>(i));
        rootToNet.emplace(root, nodes[i].netIndex);
    }

    NetAssignment result;
    result.trackNet.resize(tracks.size(), -1);
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const auto it = rootToNet.find(uf.find(trackRange[i].first));
        if (it != rootToNet.end()) result.trackNet[i] = it->second;
    }
    result.viaNet.resize(vias.size(), -1);
    for (std::size_t i = 0; i < vias.size(); ++i) {
        const auto it = rootToNet.find(uf.find(viaIndex[i]));
        if (it != rootToNet.end()) result.viaNet[i] = it->second;
    }
    return result;
}

} // namespace

bool writeKiCadPcb(const Document& doc, const std::vector<ImportedNet>& nets, const std::string& path,
                   std::string* errorOut) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }

    const std::vector<const InsertEntity*> footprints = placedFootprints(doc);
    std::vector<const TrackEntity*> tracks;
    std::vector<const ViaEntity*> vias;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Track) tracks.push_back(static_cast<const TrackEntity*>(e));
        else if (e->type() == EntityType::Via) vias.push_back(static_cast<const ViaEntity*>(e));
    }

    // Layer set: every distinct LayerId actually referenced by a track,
    // a blind/buried via's span, or a footprint's own side -- numbered
    // sequentially in first-encountered order, named with whatever the
    // Document itself already calls that layer (this codebase's own PCB
    // convention already names them F.Cu/B.Cu, matching SpecctraWriter.cpp's
    // own back-side detection).
    std::vector<LayerId> orderedLayers;
    std::map<LayerId, int> layerOrdinal;
    auto registerLayer = [&](LayerId lid) {
        if (layerOrdinal.count(lid)) return;
        layerOrdinal[lid] = static_cast<int>(orderedLayers.size());
        orderedLayers.push_back(lid);
    };
    for (const auto* t : tracks) registerLayer(t->layer());
    for (const auto* v : vias) {
        if (!v->throughHole) {
            registerLayer(v->fromLayer);
            registerLayer(v->toLayer);
        }
    }
    for (const auto* fp : footprints) registerLayer(fp->layer());
    if (orderedLayers.empty()) {
        // No copper entities at all -- still write a minimal, real 2-layer
        // stackup so the file is a valid, openable (if empty) board.
        orderedLayers = {0, 0}; // placeholder ids, names below don't depend on doc.findLayer()
    }

    auto layerName = [&](LayerId lid) -> std::string {
        if (const Layer* l = doc.findLayer(lid)) return l->name;
        return "F.Cu";
    };

    const NetAssignment assignment = resolveNets(doc, nets, tracks, vias);

    std::vector<SExpr> root;
    root.push_back(SExpr::sym("kicad_pcb"));
    root.push_back(SExpr::list("version", {SExpr::num(20221018)}));
    root.push_back(SExpr::list("generator", {SExpr::str("kumcad")}));
    root.push_back(SExpr::list("general", {SExpr::list("thickness", {SExpr::num(1.6)})}));

    std::vector<SExpr> layersList;
    layersList.push_back(SExpr::sym("layers"));
    for (std::size_t i = 0; i < orderedLayers.size(); ++i) {
        const std::string name = i < orderedLayers.size() && orderedLayers[i] != 0 ? layerName(orderedLayers[i])
                                                                                    : (i == 0 ? "F.Cu" : "B.Cu");
        layersList.push_back(
            SExpr::list(std::to_string(i), {SExpr::sym(name), SExpr::sym("signal")}));
    }
    root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(layersList)});

    root.push_back(SExpr::list("setup", {SExpr::list("pad_to_mask_clearance", {SExpr::num(0)})}));

    root.push_back(SExpr::list("net", {SExpr::num(0), SExpr::str("")}));
    for (std::size_t i = 0; i < nets.size(); ++i) {
        root.push_back(SExpr::list("net", {SExpr::num(static_cast<double>(i + 1)), SExpr::str(nets[i].name)}));
    }

    for (const auto* fp : footprints) {
        std::vector<int> padNetNumbers(fp->block()->pads.size(), 0);
        std::vector<std::string> padNetNames(fp->block()->pads.size());
        const std::string* refdes = fp->attributeValue("REFDES");
        if (refdes) {
            for (std::size_t pi = 0; pi < fp->block()->pads.size(); ++pi) {
                for (std::size_t ni = 0; ni < nets.size(); ++ni) {
                    bool matched = false;
                    for (const ImportedNetPin& pin : nets[ni].pins) {
                        if (pin.refDes == *refdes && pin.pinNumber == fp->block()->pads[pi].number) {
                            matched = true;
                            break;
                        }
                    }
                    if (matched) {
                        padNetNumbers[pi] = static_cast<int>(ni) + 1;
                        padNetNames[pi] = nets[ni].name;
                        break;
                    }
                }
            }
        }
        const bool backSide = layerName(fp->layer()) == "B.Cu";
        root.push_back(buildPlacedFootprintExpr(doc, *fp->block(), fp->position(), fp->rotation() * 180.0 / M_PI,
                                                backSide, padNetNumbers, padNetNames));
    }

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const TrackEntity& t = *tracks[i];
        for (std::size_t v = 0; v + 1 < t.vertices().size(); ++v) {
            std::vector<SExpr> seg;
            seg.push_back(SExpr::sym("segment"));
            seg.push_back(SExpr::list("start", {SExpr::num(t.vertices()[v].x), SExpr::num(t.vertices()[v].y)}));
            seg.push_back(SExpr::list("end", {SExpr::num(t.vertices()[v + 1].x), SExpr::num(t.vertices()[v + 1].y)}));
            seg.push_back(SExpr::list("width", {SExpr::num(t.width())}));
            seg.push_back(SExpr::list("layer", {SExpr::str(layerName(t.layer()))}));
            seg.push_back(SExpr::list("net", {SExpr::num(assignment.trackNet[i] >= 0 ? assignment.trackNet[i] + 1 : 0)}));
            root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(seg)});
        }
    }

    for (std::size_t i = 0; i < vias.size(); ++i) {
        const ViaEntity& v = *vias[i];
        std::vector<SExpr> viaExpr;
        viaExpr.push_back(SExpr::sym("via"));
        viaExpr.push_back(SExpr::list("at", {SExpr::num(v.position().x), SExpr::num(v.position().y)}));
        viaExpr.push_back(SExpr::list("size", {SExpr::num(v.diameter())}));
        viaExpr.push_back(SExpr::list("drill", {SExpr::num(v.drillDiameter())}));
        const std::string fromName = v.throughHole ? std::string("F.Cu") : layerName(v.fromLayer);
        const std::string toName = v.throughHole ? std::string("B.Cu") : layerName(v.toLayer);
        viaExpr.push_back(SExpr::list("layers", {SExpr::str(fromName), SExpr::str(toName)}));
        viaExpr.push_back(SExpr::list("net", {SExpr::num(assignment.viaNet[i] >= 0 ? assignment.viaNet[i] + 1 : 0)}));
        root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(viaExpr)});
    }

    SExpr file{SExpr::Kind::List, "", 0.0, std::move(root)};
    out << writeSExpr(file);
    return static_cast<bool>(out);
}

bool readKiCadPcb(Document& doc, const std::string& path, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) *errorOut = "Could not open " + path + " for reading";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::optional<SExpr> root = parseSExpr(ss.str());
    if (!root || root->tag() != "kicad_pcb") {
        if (errorOut) *errorOut = "Not a valid .kicad_pcb file: " + path;
        return false;
    }

    // Net table: index -> name, index 0 reserved for "no net".
    std::map<int, std::string> netNames;
    for (const SExpr* netExpr : root->children("net")) {
        netNames[static_cast<int>(netExpr->numberAt(0, 0.0))] = netExpr->textAt(1);
    }

    // Layers: name -> a real Document layer (created if new).
    std::map<std::string, LayerId> layerIds;
    auto ensureLayer = [&](const std::string& name) -> LayerId {
        auto it = layerIds.find(name);
        if (it != layerIds.end()) return it->second;
        for (const Layer& l : doc.layers()) {
            if (l.name == name) {
                layerIds[name] = l.id;
                return l.id;
            }
        }
        const LayerId id = doc.addLayer(name, Color{200, 200, 200});
        layerIds[name] = id;
        return id;
    };
    if (const SExpr* layersExpr = root->child("layers")) {
        for (const SExpr& item : layersExpr->items) {
            if (!item.isList() || item.items.size() < 2) continue;
            ensureLayer(item.items[1].isSymbol() || item.items[1].isString() ? item.items[1].text : "F.Cu");
        }
    }

    for (const SExpr* fpExpr : root->children("footprint")) {
        auto parsed = readPlacedFootprintExpr(doc, *fpExpr);
        if (!parsed) continue;
        const LayerId layer = ensureLayer(parsed->backSide ? "B.Cu" : "F.Cu");
        auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), layer, parsed->block, parsed->position,
                                                     1.0, parsed->rotationDeg * M_PI / 180.0);
        doc.addEntity(std::move(insert));
    }

    for (const SExpr* segExpr : root->children("segment")) {
        const SExpr* start = segExpr->child("start");
        const SExpr* end = segExpr->child("end");
        if (!start || !end) continue;
        const SExpr* layerExpr = segExpr->child("layer");
        const LayerId layer = ensureLayer(layerExpr ? layerExpr->textAt(0, "F.Cu") : "F.Cu");
        const double width = segExpr->child("width") ? segExpr->child("width")->numberAt(0, 0.25) : 0.25;
        std::vector<Point2D> verts{{start->numberAt(0), start->numberAt(1)}, {end->numberAt(0), end->numberAt(1)}};
        doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), layer, verts, width));
    }

    for (const SExpr* viaExpr : root->children("via")) {
        const SExpr* at = viaExpr->child("at");
        if (!at) continue;
        const double size = viaExpr->child("size") ? viaExpr->child("size")->numberAt(0, 0.6) : 0.6;
        const double drill = viaExpr->child("drill") ? viaExpr->child("drill")->numberAt(0, 0.3) : 0.3;
        const LayerId layer = ensureLayer("F.Cu");
        auto via = std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(at->numberAt(0), at->numberAt(1)),
                                               size, drill);
        if (const SExpr* layersExpr = viaExpr->child("layers")) {
            const std::string from = layersExpr->textAt(0, "F.Cu");
            const std::string to = layersExpr->textAt(1, "B.Cu");
            via->throughHole = (from == "F.Cu" && to == "B.Cu");
            if (!via->throughHole) {
                via->fromLayer = ensureLayer(from);
                via->toLayer = ensureLayer(to);
            }
        }
        doc.addEntity(std::move(via));
    }

    return true;
}

} // namespace lcad
