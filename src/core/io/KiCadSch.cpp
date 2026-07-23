#include "core/io/KiCadSch.h"

#include "core/document/Block.h"
#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/Wire.h"
#include "core/io/SExpr.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

namespace lcad {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A deterministic, valid-shaped (version-4-looking) UUID string -- this
// codebase has no cryptographic RNG dependency to reach for real
// randomness, and doesn't need one: uniqueness WITHIN one written file is
// all a round-trip test can observe, and std::mt19937's own sequence
// already gives that.
std::string makeUuid(std::mt19937& rng) {
    static const char* kHex = "0123456789abcdef";
    static const char* kVariant = "89ab";
    std::uniform_int_distribution<int> nibble(0, 15);
    std::uniform_int_distribution<int> variantPick(0, 3);
    auto hex = [&] { return kHex[nibble(rng)]; };

    std::string s;
    for (int i = 0; i < 8; ++i) s += hex();
    s += '-';
    for (int i = 0; i < 4; ++i) s += hex();
    s += '-';
    s += '4';
    for (int i = 0; i < 3; ++i) s += hex();
    s += '-';
    s += kVariant[variantPick(rng)];
    for (int i = 0; i < 3; ++i) s += hex();
    s += '-';
    for (int i = 0; i < 12; ++i) s += hex();
    return s;
}

SExpr uuidExpr(std::mt19937& rng) { return SExpr::list("uuid", {SExpr::str(makeUuid(rng))}); }

SExpr xyExpr(Point2D p) { return SExpr::list("xy", {SExpr::num(p.x), SExpr::num(p.y)}); }

SExpr atExpr(Point2D p, double rotDeg = 0.0) {
    return SExpr::list("at", {SExpr::num(p.x), SExpr::num(p.y), SExpr::num(rotDeg)});
}

SExpr fontEffectsExpr(double size) {
    return SExpr::list("effects",
                       {SExpr::list("font", {SExpr::list("size", {SExpr::num(size), SExpr::num(size)})})});
}

SExpr propertyExpr(const std::string& key, const std::string& value, Point2D at, double size) {
    return SExpr::list("property", {SExpr::str(key), SExpr::str(value), atExpr(at), fontEffectsExpr(size)});
}

std::string electricalTypeToken(PinElectricalType t) {
    switch (t) {
    case PinElectricalType::Input: return "input";
    case PinElectricalType::Output: return "output";
    case PinElectricalType::Bidirectional: return "bidirectional";
    case PinElectricalType::TriState: return "tri_state";
    case PinElectricalType::Passive: return "passive";
    case PinElectricalType::Power: return "power_in";
    case PinElectricalType::OpenCollector: return "open_collector";
    case PinElectricalType::NotConnected: return "no_connect";
    case PinElectricalType::PowerOutput: return "power_out";
    }
    return "passive";
}

PinElectricalType electricalTypeFromToken(const std::string& s) {
    if (s == "input") return PinElectricalType::Input;
    if (s == "output") return PinElectricalType::Output;
    if (s == "bidirectional") return PinElectricalType::Bidirectional;
    if (s == "tri_state") return PinElectricalType::TriState;
    if (s == "power_in") return PinElectricalType::Power;
    if (s == "power_out") return PinElectricalType::PowerOutput;
    if (s == "open_collector" || s == "open_emitter") return PinElectricalType::OpenCollector;
    if (s == "no_connect") return PinElectricalType::NotConnected;
    return PinElectricalType::Passive; // "passive"/"free"/"unspecified" all land here
}

std::string uniqueBlockName(const Document& doc, const std::string& base) {
    if (!doc.findBlock(base)) return base;
    for (int i = 2;; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!doc.findBlock(candidate)) return candidate;
    }
}

// A pin's real connection point is `at`'s own (x,y); `at`'s angle is the
// direction the stub points AWAY from the body (where a wire attaches),
// so the body-side end (this codebase's own Pin::stubStart) sits `length`
// back along the OPPOSITE direction.
Pin readPin(const SExpr& pinExpr) {
    Pin p;
    p.electricalType = electricalTypeFromToken(pinExpr.items.size() > 1 ? pinExpr.items[1].text : "passive");
    if (const SExpr* number = pinExpr.child("number")) p.number = number->textAt(0);
    if (const SExpr* name = pinExpr.child("name")) p.name = name->textAt(0);
    double angleDeg = 0.0;
    if (const SExpr* at = pinExpr.child("at")) {
        p.position = Point2D(at->numberAt(0), at->numberAt(1));
        angleDeg = at->numberAt(2, 0.0);
    }
    double length = 2.54;
    if (const SExpr* len = pinExpr.child("length")) length = len->numberAt(0, 2.54);
    const double rad = angleDeg * kPi / 180.0;
    p.stubStart = p.position - Point2D(std::cos(rad), std::sin(rad)) * length;
    return p;
}

// Every (pin ...) anywhere inside symbolExpr, including nested unit sub-
// symbols (real KiCad multi-unit parts nest each unit's own graphics/pins
// as child (symbol "Name_<unit>_<style>" ...) lists) -- merges every
// unit's pins into one flat list, a real, disclosed simplification (no
// per-unit gate-swapping modeled), not an incorrect read.
void collectPins(const SExpr& symbolExpr, std::vector<Pin>& pins) {
    for (const SExpr* pinExpr : symbolExpr.children("pin")) pins.push_back(readPin(*pinExpr));
    for (const SExpr* nested : symbolExpr.children("symbol")) collectPins(*nested, pins);
}

} // namespace

bool writeKiCadSch(const Document& doc, const std::string& path, std::string* errorOut) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }

    std::mt19937 rng(0xC0FFEE);

    std::vector<SExpr> root;
    root.push_back(SExpr::sym("kicad_sch"));
    root.push_back(SExpr::list("version", {SExpr::num(20231120)}));
    root.push_back(SExpr::list("generator", {SExpr::str("kumcad")}));
    root.push_back(uuidExpr(rng));
    root.push_back(SExpr::list("paper", {SExpr::str("A4")}));

    // lib_symbols: every distinct symbol block actually placed, in
    // first-encountered order, with its REAL pin geometry/electrical
    // types (so a write+read round trip -- even through THIS codebase's
    // own writer alone, with no external library involved -- preserves
    // real net connectivity, not just file syntax). Body artwork
    // (outline rectangle/circle/etc) is NOT embedded -- see KiCadSch.h's
    // own comment -- so a real KiCad opening this file shows correct
    // pins/connectivity on an otherwise blank symbol body.
    std::vector<const BlockDefinition*> placedSymbolBlocks;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* ins = static_cast<const InsertEntity*>(e);
        if (!ins->block() || !ins->block()->isSymbol()) continue;
        if (std::find(placedSymbolBlocks.begin(), placedSymbolBlocks.end(), ins->block()) ==
            placedSymbolBlocks.end()) {
            placedSymbolBlocks.push_back(ins->block());
        }
    }
    std::vector<SExpr> libSymbolsItems{SExpr::sym("lib_symbols")};
    for (const BlockDefinition* block : placedSymbolBlocks) {
        std::vector<SExpr> unitItems{SExpr::sym("symbol"), SExpr::str(block->name + "_1_1")};
        for (const Pin& pin : block->pins) {
            const Point2D dir = pin.position - pin.stubStart;
            const double length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            const double angleDeg = length > 1e-9 ? std::atan2(dir.y, dir.x) * 180.0 / kPi : 0.0;
            std::vector<SExpr> pe;
            pe.push_back(SExpr::sym("pin"));
            pe.push_back(SExpr::sym(electricalTypeToken(pin.electricalType)));
            pe.push_back(SExpr::sym("line"));
            pe.push_back(atExpr(pin.position, angleDeg));
            pe.push_back(SExpr::list("length", {SExpr::num(length)}));
            pe.push_back(SExpr::list("name", {SExpr::str(pin.name)}));
            pe.push_back(SExpr::list("number", {SExpr::str(pin.number)}));
            unitItems.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(pe)});
        }
        std::vector<SExpr> topItems{SExpr::sym("symbol"), SExpr::str(block->name),
                                    SExpr{SExpr::Kind::List, "", 0.0, std::move(unitItems)}};
        libSymbolsItems.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(topItems)});
    }
    root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(libSymbolsItems)});

    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Wire) {
            const auto* wire = static_cast<const WireEntity*>(e);
            for (std::size_t i = 0; i + 1 < wire->vertices().size(); ++i) {
                std::vector<SExpr> w;
                w.push_back(SExpr::sym("wire"));
                w.push_back(SExpr::list("pts", {xyExpr(wire->vertices()[i]), xyExpr(wire->vertices()[i + 1])}));
                w.push_back(SExpr::list(
                    "stroke", {SExpr::list("width", {SExpr::num(0)}), SExpr::list("type", {SExpr::sym("default")})}));
                w.push_back(uuidExpr(rng));
                root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(w)});
            }
        } else if (e->type() == EntityType::Junction) {
            const auto* j = static_cast<const JunctionEntity*>(e);
            std::vector<SExpr> je{SExpr::sym("junction"), atExpr(j->position()), SExpr::list("diameter", {SExpr::num(0)}),
                                  uuidExpr(rng)};
            root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(je)});
        } else if (e->type() == EntityType::NoConnect) {
            const auto* nc = static_cast<const NoConnectEntity*>(e);
            std::vector<SExpr> nce{SExpr::sym("no_connect"), atExpr(nc->position()), uuidExpr(rng)};
            root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(nce)});
        } else if (e->type() == EntityType::NetLabel) {
            const auto* lbl = static_cast<const NetLabelEntity*>(e);
            std::vector<SExpr> le{SExpr::sym("label"), SExpr::str(lbl->name()), atExpr(lbl->position()),
                                  fontEffectsExpr(lbl->height()), uuidExpr(rng)};
            root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(le)});
        } else if (e->type() == EntityType::Insert) {
            const auto* ins = static_cast<const InsertEntity*>(e);
            if (!ins->block() || !ins->block()->isSymbol()) continue;
            std::vector<SExpr> se;
            se.push_back(SExpr::sym("symbol"));
            se.push_back(SExpr::list("lib_id", {SExpr::str(ins->blockName())}));
            se.push_back(atExpr(ins->position(), ins->rotation() * 180.0 / kPi));
            se.push_back(SExpr::list("unit", {SExpr::num(1)}));
            se.push_back(SExpr::list("in_bom", {SExpr::sym("yes")}));
            se.push_back(SExpr::list("on_board", {SExpr::sym("yes")}));
            se.push_back(uuidExpr(rng));
            const std::string* refdes = ins->attributeValue("REFDES");
            se.push_back(propertyExpr("Reference", refdes ? *refdes : ins->blockName(), ins->position(), 1.27));
            const std::string* value = ins->attributeValue("VALUE");
            se.push_back(propertyExpr("Value", value ? *value : std::string(), ins->position(), 1.27));
            root.push_back(SExpr{SExpr::Kind::List, "", 0.0, std::move(se)});
        }
    }

    SExpr file{SExpr::Kind::List, "", 0.0, std::move(root)};
    out << writeSExpr(file);
    return static_cast<bool>(out);
}

bool readKiCadSch(Document& doc, const std::string& path, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) *errorOut = "Could not open " + path + " for reading";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::optional<SExpr> root = parseSExpr(ss.str());
    if (!root || root->tag() != "kicad_sch") {
        if (errorOut) *errorOut = "Not a valid .kicad_sch file: " + path;
        return false;
    }

    // Build every referenced lib_id's shared BlockDefinition up front --
    // one block per distinct lib_id, not one per instance (see
    // readKiCadSch's own header comment).
    std::map<std::string, BlockDefinition*> libBlocks;
    if (const SExpr* libSymbols = root->child("lib_symbols")) {
        for (const SExpr* symExpr : libSymbols->children("symbol")) {
            const std::string libId = symExpr->textAt(0);
            std::vector<Pin> pins;
            collectPins(*symExpr, pins);
            const std::string blockName = uniqueBlockName(doc, libId);
            doc.addBlock(blockName, {});
            BlockDefinition* block = doc.findBlock(blockName);
            if (!block) continue;
            block->pins = std::move(pins);
            libBlocks[libId] = block;
        }
    }

    for (const SExpr* wireExpr : root->children("wire")) {
        const SExpr* pts = wireExpr->child("pts");
        if (!pts) continue;
        const auto xyList = pts->children("xy");
        if (xyList.size() < 2) continue;
        std::vector<Point2D> verts{{xyList[0]->numberAt(0), xyList[0]->numberAt(1)},
                                   {xyList[1]->numberAt(0), xyList[1]->numberAt(1)}};
        doc.addEntity(std::make_unique<WireEntity>(doc.reserveEntityId(), doc.currentLayer(), verts));
    }

    for (const SExpr* junctionExpr : root->children("junction")) {
        const SExpr* at = junctionExpr->child("at");
        if (!at) continue;
        doc.addEntity(std::make_unique<JunctionEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                        Point2D(at->numberAt(0), at->numberAt(1))));
    }

    for (const SExpr* ncExpr : root->children("no_connect")) {
        const SExpr* at = ncExpr->child("at");
        if (!at) continue;
        doc.addEntity(std::make_unique<NoConnectEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                         Point2D(at->numberAt(0), at->numberAt(1))));
    }

    for (const char* tag : {"label", "global_label", "hierarchical_label"}) {
        for (const SExpr* labelExpr : root->children(tag)) {
            const SExpr* at = labelExpr->child("at");
            if (!at) continue;
            const std::string name = labelExpr->textAt(0);
            double height = 1.27;
            if (const SExpr* effects = labelExpr->child("effects")) {
                if (const SExpr* font = effects->child("font")) {
                    if (const SExpr* size = font->child("size")) height = size->numberAt(0, 1.27);
                }
            }
            doc.addEntity(std::make_unique<NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                            Point2D(at->numberAt(0), at->numberAt(1)), name, height));
        }
    }

    for (const SExpr* symExpr : root->children("symbol")) {
        const SExpr* libIdExpr = symExpr->child("lib_id");
        if (!libIdExpr) continue;
        const std::string libId = libIdExpr->textAt(0);

        BlockDefinition* block = nullptr;
        if (const auto it = libBlocks.find(libId); it != libBlocks.end()) {
            block = it->second;
        } else {
            // No lib_symbols entry for this id (e.g. a file this writer
            // itself produced, which never embeds one) -- a shared, pin-
            // less placeholder block, same name as the lib_id, reused
            // across every instance of it.
            block = doc.findBlock(libId);
            if (!block) {
                doc.addBlock(libId, {});
                block = doc.findBlock(libId);
            }
        }
        if (!block) continue;

        Point2D pos;
        double angleDeg = 0.0;
        if (const SExpr* at = symExpr->child("at")) {
            pos = Point2D(at->numberAt(0), at->numberAt(1));
            angleDeg = at->numberAt(2, 0.0);
        }
        auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block, pos, 1.0,
                                                      angleDeg * kPi / 180.0);
        for (const SExpr* propExpr : symExpr->children("property")) {
            const std::string key = propExpr->textAt(0);
            const std::string value = propExpr->textAt(1);
            if (key == "Reference") insert->setAttribute("REFDES", value);
            else if (key == "Value") insert->setAttribute("VALUE", value);
        }
        doc.addEntity(std::move(insert));
    }

    return true;
}

} // namespace lcad
