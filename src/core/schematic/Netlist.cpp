#include "core/schematic/Netlist.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/Wire.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <numeric>
#include <utility>

namespace lcad {

namespace {

constexpr double kEpsilon = 1e-6;

using Key = std::pair<std::int64_t, std::int64_t>;

Key quantize(const Point2D& p) {
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

struct PinSlot {
    EntityId insertId;
    const Pin* pin;
    Point2D world;
    std::size_t ufIndex = 0;
};

} // namespace

std::vector<Net> computeNets(const Document& doc) {
    std::vector<const WireEntity*> wires;
    std::vector<const JunctionEntity*> junctions;
    std::vector<const NetLabelEntity*> labels;
    std::vector<const InsertEntity*> symbolInserts;

    for (const Entity* e : doc.entities()) {
        switch (e->type()) {
        case EntityType::Wire:
            wires.push_back(static_cast<const WireEntity*>(e));
            break;
        case EntityType::Junction:
            junctions.push_back(static_cast<const JunctionEntity*>(e));
            break;
        case EntityType::NetLabel:
            labels.push_back(static_cast<const NetLabelEntity*>(e));
            break;
        case EntityType::Insert: {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (insert->block() && insert->block()->isSymbol()) symbolInserts.push_back(insert);
            break;
        }
        default:
            break;
        }
    }

    std::vector<PinSlot> pinSlots;
    for (const auto* insert : symbolInserts) {
        for (const auto& pinWorld : insert->pinWorldPositions()) {
            pinSlots.push_back({insert->id(), pinWorld.pin, pinWorld.attach, 0});
        }
    }

    std::size_t totalWireVertices = 0;
    for (const auto* w : wires) totalWireVertices += w->vertices().size();

    UnionFind uf(totalWireVertices + pinSlots.size());

    // Slot each wire's vertices and union them along the wire's own path --
    // a single physical wire is always internally connected end to end.
    std::vector<std::vector<std::size_t>> wireVertexUf(wires.size());
    std::size_t next = 0;
    for (std::size_t wi = 0; wi < wires.size(); ++wi) {
        const auto& verts = wires[wi]->vertices();
        wireVertexUf[wi].resize(verts.size());
        for (std::size_t vi = 0; vi < verts.size(); ++vi) {
            wireVertexUf[wi][vi] = next++;
            if (vi > 0) uf.unite(wireVertexUf[wi][vi], wireVertexUf[wi][vi - 1]);
        }
    }
    for (auto& slot : pinSlots) slot.ufIndex = next++;

    // Cross-entity connection pool: wire endpoints and pin positions always
    // qualify; a wire's interior vertex only qualifies where a Junction sits
    // exactly on it (a real T/cross tap), not merely because two wires
    // happen to cross on screen.
    std::map<Key, std::vector<std::size_t>> buckets;
    auto addToBucket = [&](const Point2D& p, std::size_t ufIndex) { buckets[quantize(p)].push_back(ufIndex); };

    for (std::size_t wi = 0; wi < wires.size(); ++wi) {
        const auto& verts = wires[wi]->vertices();
        if (verts.empty()) continue;
        addToBucket(verts.front(), wireVertexUf[wi].front());
        if (verts.size() > 1) addToBucket(verts.back(), wireVertexUf[wi].back());
    }
    for (const auto* j : junctions) {
        const Key key = quantize(j->position());
        for (std::size_t wi = 0; wi < wires.size(); ++wi) {
            const auto& verts = wires[wi]->vertices();
            for (std::size_t vi = 1; vi + 1 < verts.size(); ++vi) {
                if (quantize(verts[vi]) == key) buckets[key].push_back(wireVertexUf[wi][vi]);
            }
        }
    }
    for (const auto& slot : pinSlots) addToBucket(slot.world, slot.ufIndex);

    for (auto& [key, indices] : buckets) {
        (void)key;
        for (std::size_t i = 1; i < indices.size(); ++i) uf.unite(indices[0], indices[i]);
    }

    // Hierarchical/global label connectivity: two labels with the same
    // name union their nets together, regardless of physical distance --
    // this is what actually lets a design be split across sheets (see
    // Sheets.h) while staying one electrical netlist. A label that isn't
    // touching anything has no bucket entry and is silently skipped, same
    // as the existing rootNames lookup below.
    std::map<std::string, std::vector<std::size_t>> labelIndicesByName;
    for (const auto* label : labels) {
        const auto it = buckets.find(quantize(label->position()));
        if (it == buckets.end() || it->second.empty()) continue;
        labelIndicesByName[label->name()].push_back(it->second.front());
    }
    for (auto& [name, indices] : labelIndicesByName) {
        (void)name;
        for (std::size_t i = 1; i < indices.size(); ++i) uf.unite(indices[0], indices[i]);
    }

    // Group pins by their connected-component root, preserving first-
    // appearance order for stable auto-naming.
    std::map<std::size_t, std::vector<NetPin>> groups;
    std::vector<std::size_t> order;
    for (const auto& slot : pinSlots) {
        const std::size_t root = uf.find(slot.ufIndex);
        if (groups.find(root) == groups.end()) order.push_back(root);
        groups[root].push_back({slot.insertId, slot.pin->number});
    }

    // A NetLabel names whichever group shares its bucket; first match (in
    // document order) wins if more than one label lands on the same net.
    std::map<Key, std::size_t> keyToRoot;
    for (auto& [key, indices] : buckets) {
        if (!indices.empty()) keyToRoot[key] = uf.find(indices.front());
    }
    std::map<std::size_t, std::string> rootNames;
    for (const auto* label : labels) {
        const auto it = keyToRoot.find(quantize(label->position()));
        if (it != keyToRoot.end()) rootNames.try_emplace(it->second, label->name());
    }

    std::vector<Net> nets;
    int autoIndex = 1;
    for (std::size_t root : order) {
        Net net;
        const auto nameIt = rootNames.find(root);
        net.name = nameIt != rootNames.end() ? nameIt->second : ("Net" + std::to_string(autoIndex++));
        net.pins = groups[root];
        nets.push_back(std::move(net));
    }
    return nets;
}

namespace {

std::string refDesOf(const Document& doc, EntityId insertId) {
    const Entity* e = doc.findEntity(insertId);
    if (e && e->type() == EntityType::Insert) {
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (const std::string* refdes = insert->attributeValue("REFDES"); refdes && !refdes->empty()) return *refdes;
    }
    return "U" + std::to_string(insertId);
}

} // namespace

std::string formatNetlist(const Document& doc, const std::vector<Net>& nets) {
    std::string out = "# KumCAD netlist\n";
    for (const Net& net : nets) {
        out += "NET \"" + net.name + "\"\n";
        for (const NetPin& pin : net.pins) {
            out += "  " + refDesOf(doc, pin.insertId) + "." + pin.pinNumber + "\n";
        }
    }
    return out;
}

bool writeNetlist(const Document& doc, const std::vector<Net>& nets, const std::string& path,
                  std::string* errorOut) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }
    out << formatNetlist(doc, nets);
    return true;
}

} // namespace lcad
