#include "core/io/KiCadMod.h"

#include "core/document/Block.h"
#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Region.h"
#include "core/geometry/Text.h"
#include "core/io/SExpr.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>

namespace lcad {

namespace {

constexpr double kPi = 3.14159265358979323846;

LayerId ensureLayer(Document& doc, const std::string& name) {
    for (const Layer& l : doc.layers()) {
        if (l.name == name) return l.id;
    }
    return doc.addLayer(name, Color{200, 200, 200});
}

std::string layerNameOf(const Document& doc, LayerId id) {
    const Layer* l = doc.findLayer(id);
    return l ? l->name : "F.Fab";
}

std::string uniqueBlockName(const Document& doc, const std::string& base) {
    if (!doc.findBlock(base)) return base;
    for (int i = 2;; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!doc.findBlock(candidate)) return candidate;
    }
}

SExpr atExpr(Point2D p, std::optional<double> rotDeg = std::nullopt) {
    std::vector<SExpr> rest{SExpr::num(p.x), SExpr::num(p.y)};
    if (rotDeg) rest.push_back(SExpr::num(*rotDeg));
    return SExpr::list("at", std::move(rest));
}

Point2D readXY(const SExpr& taggedPoint) { return Point2D(taggedPoint.numberAt(0), taggedPoint.numberAt(1)); }

std::string padShapeToken(PadShape shape) {
    switch (shape) {
        case PadShape::Round: return "circle";
        case PadShape::Rect: return "rect";
        case PadShape::Oval: return "oval";
        case PadShape::RoundRect: return "roundrect";
        case PadShape::Trapezoid: return "trapezoid";
        case PadShape::Custom: return "custom";
    }
    return "circle";
}

PadShape padShapeFromToken(const std::string& tok) {
    if (tok == "roundrect") return PadShape::RoundRect;
    if (tok == "trapezoid") return PadShape::Trapezoid;
    if (tok == "custom") return PadShape::Custom;
    if (tok == "rect") return PadShape::Rect;
    if (tok == "oval") return PadShape::Oval;
    return PadShape::Round;
}

SExpr makePadExpr(const Pad& pad) {
    bool thru = pad.drillDiameter > 1e-9;
    std::vector<SExpr> rest;
    rest.push_back(SExpr::str(pad.number));
    rest.push_back(SExpr::sym(thru ? "thru_hole" : "smd"));
    rest.push_back(SExpr::sym(padShapeToken(pad.shape)));
    rest.push_back(atExpr(pad.position));
    rest.push_back(SExpr::list("size", {SExpr::num(pad.width), SExpr::num(pad.height)}));
    if (thru) {
        rest.push_back(SExpr::list("drill", {SExpr::num(pad.drillDiameter)}));
        rest.push_back(SExpr::list("layers", {SExpr::str("*.Cu"), SExpr::str("*.Mask")}));
    } else {
        rest.push_back(SExpr::list("layers", {SExpr::str("F.Cu"), SExpr::str("F.Paste"), SExpr::str("F.Mask")}));
    }
    if (pad.shape == PadShape::RoundRect) {
        rest.push_back(SExpr::list("roundrect_rratio", {SExpr::num(pad.shapeParam)}));
    } else if (pad.shape == PadShape::Trapezoid) {
        rest.push_back(SExpr::list("rect_delta", {SExpr::num(pad.shapeParam), SExpr::num(0.0)}));
    } else if (pad.shape == PadShape::Custom && pad.customOutline.size() >= 3) {
        // Real KiCad custom-pad format: "size" above is the small anchor
        // pad (fixed to a rect anchor here -- a real, disclosed
        // simplification, this codebase doesn't expose a per-pad anchor
        // shape choice), and the actual copper is one or more primitives.
        // This codebase only ever writes a single gr_poly primitive (see
        // Pad::customOutline's own comment on scope).
        std::vector<SExpr> xyItems;
        for (const Point2D& p : pad.customOutline) xyItems.push_back(SExpr::list("xy", {SExpr::num(p.x), SExpr::num(p.y)}));
        SExpr grPoly = SExpr::list("gr_poly", {SExpr::list("pts", std::move(xyItems)), SExpr::list("width", {SExpr::num(0.0)})});
        rest.push_back(SExpr::list("options", {SExpr::list("clearance", {SExpr::sym("outline")}),
                                               SExpr::list("anchor", {SExpr::sym("rect")})}));
        rest.push_back(SExpr::list("primitives", {std::move(grPoly)}));
    }
    return SExpr::list("pad", std::move(rest));
}

std::vector<Point2D> rectOutline(const Point2D& start, const Point2D& end) {
    return {start, Point2D(end.x, start.y), end, Point2D(start.x, end.y)};
}

std::vector<Point2D> circleOutline(const Point2D& center, double radius, int segments = 32) {
    std::vector<Point2D> pts;
    pts.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const double t = 2.0 * kPi * static_cast<double>(i) / segments;
        pts.emplace_back(center.x + radius * std::cos(t), center.y + radius * std::sin(t));
    }
    return pts;
}

// Real union via the same polygon clipper REGION's own boolean ops use --
// picks the largest-area resulting loop if the union produces more than
// one disjoint piece, since Pad::customOutline is a single flat polygon,
// not a multi-loop shape (a real, disclosed scope limit, the same
// "largest wins" convention deriveBoardOutline already uses elsewhere for
// the same single-polygon-representation reason).
//
// A real, disclosed limitation confirmed while testing this: two
// primitives that only TOUCH (share an exact edge or vertex, zero-area
// overlap) rather than genuinely overlap don't merge into one loop --
// regionBoolean's own crossing-point detection needs a proper transversal
// intersection, so an exactly-touching pair comes back as two disjoint
// loops (picked between by the largest-wins rule above, silently
// dropping the other) instead of one connected shape. A custom pad whose
// primitives are drawn with a deliberate small overlap (the common real-
// world way to build one, precisely to avoid relying on an exact shared
// edge) is unaffected.
std::vector<Point2D> unionOutline(const std::vector<Point2D>& a, const std::vector<Point2D>& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const std::vector<RegionLoop> loops = regionBoolean(a, b, RegionBoolOp::Union);
    if (loops.empty()) return a; // degenerate/failed union -- keep what we already had
    return std::max_element(loops.begin(), loops.end(),
                            [](const RegionLoop& x, const RegionLoop& y) {
                                return std::abs(x.signedArea()) < std::abs(y.signedArea());
                            })
        ->vertices;
}

Pad readPad(const SExpr& padExpr) {
    Pad pad;
    pad.number = padExpr.textAt(0);
    const std::string typeTok = padExpr.textAt(1);
    pad.shape = padShapeFromToken(padExpr.textAt(2));
    if (const SExpr* at = padExpr.child("at")) pad.position = readXY(*at);
    if (const SExpr* size = padExpr.child("size")) {
        pad.width = size->numberAt(0, pad.width);
        pad.height = size->numberAt(1, pad.height);
    }
    if (typeTok == "thru_hole" || typeTok == "np_thru_hole") {
        if (const SExpr* drill = padExpr.child("drill")) pad.drillDiameter = drill->numberAt(0, 0.0);
    }
    if (pad.shape == PadShape::RoundRect) {
        if (const SExpr* ratio = padExpr.child("roundrect_rratio")) pad.shapeParam = ratio->numberAt(0, 0.0);
    } else if (pad.shape == PadShape::Trapezoid) {
        if (const SExpr* delta = padExpr.child("rect_delta")) pad.shapeParam = delta->numberAt(0, 0.0);
    } else if (pad.shape == PadShape::Custom) {
        // Real KiCad allows several primitives (gr_poly/gr_rect/gr_circle/
        // gr_line/gr_arc) whose union forms one custom pad's copper --
        // gr_poly/gr_rect/gr_circle are modeled here (the overwhelming
        // majority of real custom pads use one or a mix of these three),
        // each unioned into pad.customOutline in document order via the
        // same real polygon-boolean union REGION's own UNION uses. A
        // gr_line/gr_arc primitive is still skipped -- a real, disclosed
        // remaining gap: a stroked line/arc needs its own width-to-outline
        // conversion this doesn't do.
        if (const SExpr* primitives = padExpr.child("primitives")) {
            for (const SExpr* grPoly : primitives->children("gr_poly")) {
                std::vector<Point2D> poly;
                if (const SExpr* pts = grPoly->child("pts")) {
                    for (const SExpr* xy : pts->children("xy")) poly.emplace_back(xy->numberAt(0), xy->numberAt(1));
                }
                pad.customOutline = unionOutline(pad.customOutline, poly);
            }
            for (const SExpr* grRect : primitives->children("gr_rect")) {
                const SExpr* start = grRect->child("start");
                const SExpr* end = grRect->child("end");
                if (start && end) pad.customOutline = unionOutline(pad.customOutline, rectOutline(readXY(*start), readXY(*end)));
            }
            for (const SExpr* grCircle : primitives->children("gr_circle")) {
                const SExpr* center = grCircle->child("center");
                const SExpr* end = grCircle->child("end");
                if (center && end) {
                    const Point2D c = readXY(*center);
                    pad.customOutline = unionOutline(pad.customOutline, circleOutline(c, c.distanceTo(readXY(*end))));
                }
            }
        }
    }
    return pad;
}

// Real circumcenter-based reconstruction of a 3-point (start/mid/end) arc,
// KiCad's own modern fp_arc/gr_arc representation. Returns nullopt for a
// degenerate (collinear) triple.
struct ArcParams {
    Point2D center;
    double radius;
    double startAngle;
    double endAngle;
};

double normalizeAngle(double a) {
    while (a < 0) a += 2 * kPi;
    while (a >= 2 * kPi) a -= 2 * kPi;
    return a;
}

std::optional<ArcParams> arcFromThreePoints(Point2D start, Point2D mid, Point2D end) {
    double ax = start.x, ay = start.y, bx = mid.x, by = mid.y, cx = end.x, cy = end.y;
    double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < 1e-9) return std::nullopt;
    double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) +
                 (cx * cx + cy * cy) * (ay - by)) /
                d;
    double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) +
                 (cx * cx + cy * cy) * (bx - ax)) /
                d;
    Point2D center(ux, uy);
    double radius = center.distanceTo(start);
    double startAngle = std::atan2(start.y - center.y, start.x - center.x);
    double endAngle = std::atan2(end.y - center.y, end.x - center.x);
    double midAngle = std::atan2(mid.y - center.y, mid.x - center.x);
    double sN = normalizeAngle(startAngle), eN = normalizeAngle(endAngle), mN = normalizeAngle(midAngle);
    double sweepToEnd = eN - sN;
    if (sweepToEnd < 0) sweepToEnd += 2 * kPi;
    double sweepToMid = mN - sN;
    if (sweepToMid < 0) sweepToMid += 2 * kPi;
    if (sweepToMid <= sweepToEnd) return ArcParams{center, radius, startAngle, endAngle};
    return ArcParams{center, radius, endAngle, startAngle};
}

void writeGraphics(const Document& doc, const BlockDefinition& block, std::vector<SExpr>& out) {
    for (const auto& ent : block.entities) {
        const std::string layer = layerNameOf(doc, ent->layer());
        switch (ent->type()) {
            case EntityType::Line: {
                const auto* l = static_cast<const LineEntity*>(ent.get());
                out.push_back(SExpr::list("fp_line", {SExpr::list("start", {SExpr::num(l->start().x), SExpr::num(l->start().y)}),
                                                       SExpr::list("end", {SExpr::num(l->end().x), SExpr::num(l->end().y)}),
                                                       SExpr::list("layer", {SExpr::str(layer)}),
                                                       SExpr::list("width", {SExpr::num(0.12)})}));
                break;
            }
            case EntityType::Circle: {
                const auto* c = static_cast<const CircleEntity*>(ent.get());
                Point2D endPt = c->center() + Point2D(c->radius(), 0.0);
                out.push_back(SExpr::list(
                    "fp_circle", {SExpr::list("center", {SExpr::num(c->center().x), SExpr::num(c->center().y)}),
                                  SExpr::list("end", {SExpr::num(endPt.x), SExpr::num(endPt.y)}),
                                  SExpr::list("layer", {SExpr::str(layer)}), SExpr::list("width", {SExpr::num(0.12)})}));
                break;
            }
            case EntityType::Arc: {
                const auto* a = static_cast<const ArcEntity*>(ent.get());
                double sweep = a->endAngle() - a->startAngle();
                while (sweep < 0) sweep += 2 * kPi;
                double midAngle = a->startAngle() + sweep / 2.0;
                Point2D start = a->startPoint();
                Point2D end = a->endPoint();
                Point2D mid = a->center() + Point2D(a->radius() * std::cos(midAngle), a->radius() * std::sin(midAngle));
                out.push_back(SExpr::list("fp_arc", {SExpr::list("start", {SExpr::num(start.x), SExpr::num(start.y)}),
                                                     SExpr::list("mid", {SExpr::num(mid.x), SExpr::num(mid.y)}),
                                                     SExpr::list("end", {SExpr::num(end.x), SExpr::num(end.y)}),
                                                     SExpr::list("layer", {SExpr::str(layer)}),
                                                     SExpr::list("width", {SExpr::num(0.12)})}));
                break;
            }
            case EntityType::Text: {
                const auto* t = static_cast<const TextEntity*>(ent.get());
                double rotDeg = t->rotation() * 180.0 / kPi;
                out.push_back(SExpr::list(
                    "fp_text",
                    {SExpr::sym("user"), SExpr::str(t->text()), atExpr(t->position(), rotDeg),
                     SExpr::list("layer", {SExpr::str(layer)}),
                     SExpr::list("effects", {SExpr::list("font", {SExpr::list("size", {SExpr::num(t->height()), SExpr::num(t->height())}),
                                                                   SExpr::list("thickness", {SExpr::num(t->height() * 0.15)})})})}));
                break;
            }
            default:
                // Other child entity kinds (Polyline, MText, Hatch, ...) aren't
                // part of a real .kicad_mod's own graphic-item set; skipped, a
                // real disclosed gap rather than a silent miscount.
                break;
        }
    }
}

void readGraphics(Document& doc, const SExpr& footprint, std::vector<std::unique_ptr<Entity>>& body) {
    for (const SExpr& item : footprint.items) {
        if (!item.isList()) continue;
        const std::string tag = item.tag();
        const SExpr* layerExpr = item.child("layer");
        const std::string layer = layerExpr ? layerExpr->textAt(0, "F.SilkS") : "F.SilkS";
        if (tag == "fp_line") {
            const SExpr* s = item.child("start");
            const SExpr* e = item.child("end");
            body.push_back(std::make_unique<LineEntity>(doc.reserveEntityId(), ensureLayer(doc, layer),
                                                        s ? readXY(*s) : Point2D(), e ? readXY(*e) : Point2D()));
        } else if (tag == "fp_circle") {
            const SExpr* c = item.child("center");
            const SExpr* e = item.child("end");
            Point2D center = c ? readXY(*c) : Point2D();
            Point2D end = e ? readXY(*e) : center;
            body.push_back(
                std::make_unique<CircleEntity>(doc.reserveEntityId(), ensureLayer(doc, layer), center, center.distanceTo(end)));
        } else if (tag == "fp_arc") {
            const SExpr* s = item.child("start");
            const SExpr* m = item.child("mid");
            const SExpr* e = item.child("end");
            if (!s || !m || !e) continue;
            auto params = arcFromThreePoints(readXY(*s), readXY(*m), readXY(*e));
            if (!params) continue;
            body.push_back(std::make_unique<ArcEntity>(doc.reserveEntityId(), ensureLayer(doc, layer), params->center,
                                                       params->radius, params->startAngle, params->endAngle));
        } else if (tag == "fp_text") {
            const std::string text = item.textAt(1);
            const SExpr* at = item.child("at");
            Point2D pos = at ? readXY(*at) : Point2D();
            double rotDeg = at ? at->numberAt(2, 0.0) : 0.0;
            double height = 1.0;
            if (const SExpr* effects = item.child("effects")) {
                if (const SExpr* font = effects->child("font")) {
                    if (const SExpr* size = font->child("size")) height = size->numberAt(0, 1.0);
                }
            }
            body.push_back(std::make_unique<TextEntity>(doc.reserveEntityId(), ensureLayer(doc, layer), pos, text, height,
                                                        rotDeg * kPi / 180.0));
        }
    }
}

} // namespace

bool writeKiCadMod(const Document& doc, const BlockDefinition& block, const std::string& path, std::string* errorOut) {
    std::vector<SExpr> items;
    items.push_back(SExpr::str(block.name));
    items.push_back(SExpr::list("version", {SExpr::num(20221018)}));
    items.push_back(SExpr::list("generator", {SExpr::str("kumcad")}));
    items.push_back(SExpr::list("layer", {SExpr::str("F.Cu")}));
    bool anyThru = false;
    for (const Pad& p : block.pads) {
        if (p.drillDiameter > 1e-9) {
            anyThru = true;
            break;
        }
    }
    items.push_back(SExpr::list("attr", {SExpr::sym(anyThru ? "through_hole" : "smd")}));
    writeGraphics(doc, block, items);
    for (const Pad& p : block.pads) items.push_back(makePadExpr(p));

    SExpr root;
    root.kind = SExpr::Kind::List;
    root.items.push_back(SExpr::sym("footprint"));
    for (SExpr& it : items) root.items.push_back(std::move(it));

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (errorOut) *errorOut = "Could not open " + path + " for writing";
        return false;
    }
    out << writeSExpr(root);
    return true;
}

SExpr buildPlacedFootprintExpr(const Document& doc, const BlockDefinition& block, Point2D at, double rotationDeg,
                               bool backSide, const std::vector<int>& padNetNumbers,
                               const std::vector<std::string>& padNetNames) {
    std::vector<SExpr> items;
    items.push_back(SExpr::str(block.name));
    items.push_back(SExpr::list("layer", {SExpr::str(backSide ? "B.Cu" : "F.Cu")}));
    items.push_back(atExpr(at, rotationDeg));
    bool anyThru = false;
    for (const Pad& p : block.pads) {
        if (p.drillDiameter > 1e-9) {
            anyThru = true;
            break;
        }
    }
    items.push_back(SExpr::list("attr", {SExpr::sym(anyThru ? "through_hole" : "smd")}));
    writeGraphics(doc, block, items);
    for (std::size_t i = 0; i < block.pads.size(); ++i) {
        SExpr padExpr = makePadExpr(block.pads[i]);
        const int netNum = i < padNetNumbers.size() ? padNetNumbers[i] : 0;
        if (netNum > 0) {
            const std::string name = i < padNetNames.size() ? padNetNames[i] : std::string();
            padExpr.items.push_back(SExpr::list("net", {SExpr::num(netNum), SExpr::str(name)}));
        }
        items.push_back(std::move(padExpr));
    }

    SExpr root;
    root.kind = SExpr::Kind::List;
    root.items.push_back(SExpr::sym("footprint"));
    for (SExpr& it : items) root.items.push_back(std::move(it));
    return root;
}

std::optional<ParsedPlacedFootprint> readPlacedFootprintExpr(Document& doc, const SExpr& footprintExpr) {
    if (footprintExpr.tag() != "footprint" && footprintExpr.tag() != "module") return std::nullopt;

    const std::string name = uniqueBlockName(doc, footprintExpr.textAt(0, "Footprint"));
    std::vector<std::unique_ptr<Entity>> body;
    readGraphics(doc, footprintExpr, body);
    std::vector<Pad> pads;
    std::vector<int> netNumbers;
    for (const SExpr* padExpr : footprintExpr.children("pad")) {
        pads.push_back(readPad(*padExpr));
        int netNum = 0;
        if (const SExpr* net = padExpr->child("net")) netNum = static_cast<int>(net->numberAt(0, 0.0));
        netNumbers.push_back(netNum);
    }

    doc.addBlock(name, std::move(body));
    BlockDefinition* mutableBlock = doc.findBlock(name);
    if (!mutableBlock) return std::nullopt;
    mutableBlock->pads = std::move(pads);

    ParsedPlacedFootprint result;
    result.block = mutableBlock;
    if (const SExpr* layer = footprintExpr.child("layer")) result.backSide = layer->textAt(0) == "B.Cu";
    if (const SExpr* at = footprintExpr.child("at")) {
        result.position = readXY(*at);
        result.rotationDeg = at->numberAt(2, 0.0);
    }
    result.padNetNumbers = std::move(netNumbers);
    return result;
}

// Kept as its own internal-linkage function so the public readKiCadMod
// below can wrap the whole parse in a try/catch -- untrusted input (a
// hand-edited or foreign-tool .kicad_mod) shouldn't be able to crash the
// whole application over a malformed S-expression.
static const BlockDefinition* readKiCadModImpl(Document& doc, const std::string& path, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) *errorOut = "Could not open " + path + " for reading";
        return nullptr;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::optional<SExpr> root = parseSExpr(ss.str());
    if (!root || (root->tag() != "footprint" && root->tag() != "module")) {
        if (errorOut) *errorOut = "Not a valid .kicad_mod file: " + path;
        return nullptr;
    }

    std::string name = uniqueBlockName(doc, root->textAt(0, "Footprint"));

    std::vector<std::unique_ptr<Entity>> body;
    readGraphics(doc, *root, body);
    std::vector<Pad> pads;
    for (const SExpr* padExpr : root->children("pad")) pads.push_back(readPad(*padExpr));

    doc.addBlock(name, std::move(body));
    BlockDefinition* mutableBlock = doc.findBlock(name);
    if (mutableBlock) mutableBlock->pads = std::move(pads);
    return mutableBlock;
}

const BlockDefinition* readKiCadMod(Document& doc, const std::string& path, std::string* errorOut) {
    try {
        return readKiCadModImpl(doc, path, errorOut);
    } catch (const std::exception& e) {
        if (errorOut) *errorOut = std::string("Unexpected error reading .kicad_mod file: ") + e.what();
        return nullptr;
    } catch (...) {
        if (errorOut) *errorOut = "Unexpected error reading .kicad_mod file";
        return nullptr;
    }
}

} // namespace lcad
