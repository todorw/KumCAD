#include "core/schematic/SymbolLibrary.h"

#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <cmath>

namespace lcad {

namespace {

void addLine(Document& doc, std::vector<std::unique_ptr<Entity>>& body, Point2D a, Point2D b) {
    body.push_back(std::make_unique<LineEntity>(doc.reserveEntityId(), 0, a, b));
}

void addIfMissing(Document& doc, const std::string& name, std::vector<std::unique_ptr<Entity>> body,
                  std::vector<Pin> pins) {
    if (doc.findBlock(name)) return;
    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pins = std::move(pins);
}

void addFootprintIfMissing(Document& doc, const std::string& name, std::vector<std::unique_ptr<Entity>> body,
                           std::vector<Pad> pads) {
    if (doc.findBlock(name)) return;
    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
}

// A rectangular silkscreen outline body shared by several of the simpler
// footprints below.
std::vector<std::unique_ptr<Entity>> rectOutline(Document& doc, double x0, double y0, double x1, double y1) {
    std::vector<std::unique_ptr<Entity>> body;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0),
        std::vector<Point2D>{Point2D(x0, y0), Point2D(x1, y0), Point2D(x1, y1), Point2D(x0, y1)}, true));
    return body;
}

} // namespace

void registerBuiltinSymbols(Document& doc) {
    // Resistor: IEEE-315-flavored zigzag body between x=5 and x=15; pins are
    // the outer tips at x=0/x=20, stubs drawn from the body out to them.
    {
        std::vector<std::unique_ptr<Entity>> body;
        const std::vector<Point2D> zig = {Point2D(5, 0),   Point2D(6.5, 0), Point2D(7.5, 3),  Point2D(9.5, -3),
                                          Point2D(11.5, 3), Point2D(13.5, -3), Point2D(14.5, 0), Point2D(15, 0)};
        for (std::size_t i = 0; i + 1 < zig.size(); ++i) addLine(doc, body, zig[i], zig[i + 1]);
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(5, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(15, 0)},
        };
        addIfMissing(doc, "R", std::move(body), std::move(pins));
    }

    // Capacitor: two parallel plates at x=9/x=11; the pin stubs themselves
    // are the leads, so no separate lead-line entities are needed.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(9, -5), Point2D(9, 5));
        addLine(doc, body, Point2D(11, -5), Point2D(11, 5));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(9, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(11, 0)},
        };
        addIfMissing(doc, "C", std::move(body), std::move(pins));
    }

    // Diode: anode-side triangle (base at x=8) pointing into a cathode bar
    // at x=14.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(8, -4), Point2D(8, 4));
        addLine(doc, body, Point2D(8, -4), Point2D(14, 0));
        addLine(doc, body, Point2D(8, 4), Point2D(14, 0));
        addLine(doc, body, Point2D(14, -4), Point2D(14, 4));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(8, 0)},   // anode
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(14, 0)}, // cathode
        };
        addIfMissing(doc, "D", std::move(body), std::move(pins));
    }

    // Generic 2-pin connector: a small body box with both pins on its left
    // edge, like a 2-way header.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(5, -6), Point2D(15, -6), Point2D(15, 6), Point2D(5, 6)}, true));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, -3), Point2D(5, -3)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(0, 3), Point2D(5, 3)},
        };
        addIfMissing(doc, "CONN2", std::move(body), std::move(pins));
    }

    // Generic 4-pin IC: a DIP-style rectangle body with two pins per side.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(0, -10), Point2D(20, -10), Point2D(20, 10), Point2D(0, 10)}, true));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Input, Point2D(-5, 7), Point2D(0, 7)},
            Pin{"2", "2", PinElectricalType::Power, Point2D(-5, -7), Point2D(0, -7)},
            Pin{"3", "3", PinElectricalType::Output, Point2D(25, -7), Point2D(20, -7)},
            Pin{"4", "4", PinElectricalType::Power, Point2D(25, 7), Point2D(20, 7)},
        };
        addIfMissing(doc, "IC", std::move(body), std::move(pins));
    }

    // LED: same anode-triangle/cathode-bar shape as the diode, plus two
    // small arrows radiating up-right to show emitted light.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(8, -4), Point2D(8, 4));
        addLine(doc, body, Point2D(8, -4), Point2D(14, 0));
        addLine(doc, body, Point2D(8, 4), Point2D(14, 0));
        addLine(doc, body, Point2D(14, -4), Point2D(14, 4));
        addLine(doc, body, Point2D(11, 6), Point2D(13, 9));
        addLine(doc, body, Point2D(12.3, 7.2), Point2D(13, 9));
        addLine(doc, body, Point2D(11.7, 8.7), Point2D(13, 9));
        addLine(doc, body, Point2D(14, 6), Point2D(16, 9));
        addLine(doc, body, Point2D(15.3, 7.2), Point2D(16, 9));
        addLine(doc, body, Point2D(14.7, 8.7), Point2D(16, 9));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(8, 0)},   // anode
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(14, 0)}, // cathode
        };
        addIfMissing(doc, "LED", std::move(body), std::move(pins));
    }

    // NPN transistor: a vertical base bar with collector/emitter leads
    // diagonally off it; the emitter lead's arrowhead points away from the
    // base (conducts base-to-emitter, NPN convention).
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(8, -6), Point2D(8, 6));
        addLine(doc, body, Point2D(8, 3), Point2D(16, 8));
        addLine(doc, body, Point2D(8, -3), Point2D(16, -8));
        addLine(doc, body, Point2D(12, -5), Point2D(14.5, -6.5));
        addLine(doc, body, Point2D(12, -5), Point2D(13, -3.2));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(8, 0)},    // base
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 12), Point2D(16, 8)}, // collector
            Pin{"3", "3", PinElectricalType::Passive, Point2D(20, -12), Point2D(16, -8)}, // emitter
        };
        addIfMissing(doc, "Q_NPN", std::move(body), std::move(pins));
    }

    // PNP transistor: identical lead geometry, but the emitter arrowhead
    // points toward the base instead of away from it.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(8, -6), Point2D(8, 6));
        addLine(doc, body, Point2D(8, 3), Point2D(16, 8));
        addLine(doc, body, Point2D(8, -3), Point2D(16, -8));
        addLine(doc, body, Point2D(14, -7.5), Point2D(11.5, -6));
        addLine(doc, body, Point2D(14, -7.5), Point2D(13, -9.3));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(8, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 12), Point2D(16, 8)},
            Pin{"3", "3", PinElectricalType::Passive, Point2D(20, -12), Point2D(16, -8)},
        };
        addIfMissing(doc, "Q_PNP", std::move(body), std::move(pins));
    }

    // Inductor: four upper-semicircle coil bumps between x=6 and x=14.
    {
        std::vector<std::unique_ptr<Entity>> body;
        for (double cx : {7.0, 9.0, 11.0, 13.0}) {
            body.push_back(std::make_unique<ArcEntity>(doc.reserveEntityId(), LayerId(0), Point2D(cx, 0), 1.0, 0.0, M_PI));
        }
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(6, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(14, 0)},
        };
        addIfMissing(doc, "L", std::move(body), std::move(pins));
    }

    // SPST switch: two terminal dots joined by a diagonal open-contact arm.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<CircleEntity>(doc.reserveEntityId(), LayerId(0), Point2D(6, 0), 0.6));
        body.push_back(std::make_unique<CircleEntity>(doc.reserveEntityId(), LayerId(0), Point2D(14, 0), 0.6));
        addLine(doc, body, Point2D(6, 0), Point2D(13, 4));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(6, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(14, 0)},
        };
        addIfMissing(doc, "SW", std::move(body), std::move(pins));
    }

    // 3- and 4-way header connectors, same box-with-left-edge-pins shape
    // as CONN2 above, just taller with more evenly-spaced pins.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(5, -9), Point2D(15, -9), Point2D(15, 9), Point2D(5, 9)}, true));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, -6), Point2D(5, -6)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(0, 0), Point2D(5, 0)},
            Pin{"3", "3", PinElectricalType::Passive, Point2D(0, 6), Point2D(5, 6)},
        };
        addIfMissing(doc, "CONN3", std::move(body), std::move(pins));
    }
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(5, -12), Point2D(15, -12), Point2D(15, 12), Point2D(5, 12)}, true));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, -9), Point2D(5, -9)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(0, -3), Point2D(5, -3)},
            Pin{"3", "3", PinElectricalType::Passive, Point2D(0, 3), Point2D(5, 3)},
            Pin{"4", "4", PinElectricalType::Passive, Point2D(0, 9), Point2D(5, 9)},
        };
        addIfMissing(doc, "CONN4", std::move(body), std::move(pins));
    }

    // DC voltage source: a "long plate / short plate" battery glyph.
    // Simulation-only (see core/schematic/Spice.h) -- deliberately no
    // matching "_FP" footprint, unlike every part above: a source
    // represents an external supply or test stimulus, not something that
    // gets placed and soldered onto a real board.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(9, -5), Point2D(9, 5));   // + plate (tall)
        addLine(doc, body, Point2D(11, -3), Point2D(11, 3)); // - plate (short)
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::PowerOutput, Point2D(0, 0), Point2D(9, 0)},  // +
            Pin{"2", "2", PinElectricalType::PowerOutput, Point2D(20, 0), Point2D(11, 0)}, // -
        };
        addIfMissing(doc, "V", std::move(body), std::move(pins));
    }

    // DC current source: a circle with an arrow through it pointing from
    // pin 1 to pin 2 (conventional current direction). Simulation-only,
    // same reasoning as V above.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<CircleEntity>(doc.reserveEntityId(), LayerId(0), Point2D(10, 0), 6.0));
        addLine(doc, body, Point2D(6, 0), Point2D(14, 0));
        addLine(doc, body, Point2D(14, 0), Point2D(11, 2));
        addLine(doc, body, Point2D(14, 0), Point2D(11, -2));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::PowerOutput, Point2D(0, 0), Point2D(4, 0)},
            Pin{"2", "2", PinElectricalType::PowerOutput, Point2D(20, 0), Point2D(16, 0)},
        };
        addIfMissing(doc, "I", std::move(body), std::move(pins));
    }

    // Crystal/resonator: a rectangular can body (two vertical side bars,
    // real IEEE-315 crystal glyph) between the two leads.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(8, -5), Point2D(12, -5), Point2D(12, 5), Point2D(8, 5)}, true));
        addLine(doc, body, Point2D(6, -6), Point2D(6, 6)); // left plate
        addLine(doc, body, Point2D(14, -6), Point2D(14, 6)); // right plate
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(6, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(14, 0)},
        };
        addIfMissing(doc, "XTAL", std::move(body), std::move(pins));
    }

    // Fuse: a rectangle body with a straight element line through it (the
    // standard "rectangle" fuse glyph, as opposed to IEC's sine-wave
    // variant).
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(7, -3), Point2D(13, -3), Point2D(13, 3), Point2D(7, 3)}, true));
        addLine(doc, body, Point2D(7, 0), Point2D(13, 0));
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::Passive, Point2D(0, 0), Point2D(7, 0)},
            Pin{"2", "2", PinElectricalType::Passive, Point2D(20, 0), Point2D(13, 0)},
        };
        addIfMissing(doc, "F", std::move(body), std::move(pins));
    }

    // Battery: a REAL physical part (unlike V's own simulation-only DC
    // source), so -- unlike V -- it gets a matching _FP below. Two
    // long/short plate pairs (the standard "more than one cell" battery
    // glyph, as opposed to V's own single pair).
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(8, -5), Point2D(8, 5));   // + plate (tall)
        addLine(doc, body, Point2D(10, -3), Point2D(10, 3)); // - plate (short)
        addLine(doc, body, Point2D(12, -5), Point2D(12, 5)); // + plate (tall)
        addLine(doc, body, Point2D(14, -3), Point2D(14, 3)); // - plate (short)
        std::vector<Pin> pins = {
            Pin{"1", "1", PinElectricalType::PowerOutput, Point2D(0, 0), Point2D(8, 0)},   // +
            Pin{"2", "2", PinElectricalType::PowerOutput, Point2D(20, 0), Point2D(14, 0)}, // -
        };
        addIfMissing(doc, "BAT", std::move(body), std::move(pins));
    }

    // Op-amp: the standard triangle body (point at x=20), 5 pins -- the
    // two inputs and output real signal flow needs, plus V+/V- power
    // pins ERC's own undriven-power-pin check can verify are actually
    // driven (see Erc.h), matching how a real op-amp symbol is drawn
    // rather than a signal-only 3-pin simplification.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(5, -10), Point2D(5, 10));
        addLine(doc, body, Point2D(5, -10), Point2D(20, 0));
        addLine(doc, body, Point2D(5, 10), Point2D(20, 0));
        addLine(doc, body, Point2D(7, 6), Point2D(11, 6)); // "+" input marker
        addLine(doc, body, Point2D(9, 4), Point2D(9, 8));
        addLine(doc, body, Point2D(7, -6), Point2D(11, -6)); // "-" input marker
        std::vector<Pin> pins = {
            Pin{"IN+", "1", PinElectricalType::Input, Point2D(0, 6), Point2D(5, 6)},
            Pin{"IN-", "2", PinElectricalType::Input, Point2D(0, -6), Point2D(5, -6)},
            Pin{"OUT", "3", PinElectricalType::Output, Point2D(25, 0), Point2D(20, 0)},
            Pin{"V+", "4", PinElectricalType::Power, Point2D(12, 15), Point2D(12, 10)},
            Pin{"V-", "5", PinElectricalType::Power, Point2D(12, -15), Point2D(12, -10)},
        };
        addIfMissing(doc, "OPAMP", std::move(body), std::move(pins));
    }

    // Ground: the standard 3-descending-bar power-flag glyph. A single
    // PowerOutput pin -- like V/I's own terminals -- so ERC's undriven-
    // power-pin check treats every net a GND symbol touches as driven,
    // the real reason a schematic needs GND symbols at all rather than
    // just naming a wire "GND" and hoping ERC notices.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(10, 5), Point2D(10, 0));
        addLine(doc, body, Point2D(6, 0), Point2D(14, 0));
        addLine(doc, body, Point2D(7.5, -2), Point2D(12.5, -2));
        addLine(doc, body, Point2D(9, -4), Point2D(11, -4));
        std::vector<Pin> pins = {
            Pin{"GND", "1", PinElectricalType::PowerOutput, Point2D(10, 5), Point2D(10, 0)},
        };
        addIfMissing(doc, "GND", std::move(body), std::move(pins));
    }

    // VCC: the standard upward-arrow power-flag glyph, same PowerOutput-
    // pin reasoning as GND above but for a positive supply rail instead
    // of the 0V reference.
    {
        std::vector<std::unique_ptr<Entity>> body;
        addLine(doc, body, Point2D(10, -5), Point2D(10, 3));
        addLine(doc, body, Point2D(10, 3), Point2D(7, 0));
        addLine(doc, body, Point2D(10, 3), Point2D(13, 0));
        std::vector<Pin> pins = {
            Pin{"VCC", "1", PinElectricalType::PowerOutput, Point2D(10, -5), Point2D(10, -5)},
        };
        addIfMissing(doc, "VCC", std::move(body), std::move(pins));
    }

    // A matching PCB footprint for each schematic symbol above (see
    // BlockDefinition::pads) -- named "<Symbol>_FP" so both live in the
    // same document's block table without colliding.
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(-2, -2), Point2D(12, -2), Point2D(12, 2), Point2D(-2, 2)}, true));
        std::vector<Pad> pads = {
            Pad{"1", PadShape::Rect, Point2D(0, 0), 1.5, 1.5, 0.0},
            Pad{"2", PadShape::Rect, Point2D(10, 0), 1.5, 1.5, 0.0},
        };
        if (!doc.findBlock("R_FP")) {
            doc.addBlock("R_FP", std::move(body));
            if (BlockDefinition* block = doc.findBlock("R_FP")) block->pads = std::move(pads);
        }
    }
    {
        std::vector<std::unique_ptr<Entity>> body;
        body.push_back(std::make_unique<PolylineEntity>(
            doc.reserveEntityId(), LayerId(0),
            std::vector<Point2D>{Point2D(0, -10), Point2D(20, -10), Point2D(20, 10), Point2D(0, 10)}, true));
        std::vector<Pad> pads = {
            Pad{"1", PadShape::Round, Point2D(0, 7), 1.6, 1.6, 0.8},
            Pad{"2", PadShape::Round, Point2D(0, -7), 1.6, 1.6, 0.8},
            Pad{"3", PadShape::Round, Point2D(20, -7), 1.6, 1.6, 0.8},
            Pad{"4", PadShape::Round, Point2D(20, 7), 1.6, 1.6, 0.8},
        };
        if (!doc.findBlock("IC_FP")) {
            doc.addBlock("IC_FP", std::move(body));
            if (BlockDefinition* block = doc.findBlock("IC_FP")) block->pads = std::move(pads);
        }
    }

    // Previously-missing footprints for C, D and CONN2 -- without one of
    // these, a board placing that part had no footprint to place at all.
    addFootprintIfMissing(doc, "C_FP", rectOutline(doc, -1.5, -1.5, 6.5, 1.5),
                          {Pad{"1", PadShape::Rect, Point2D(0, 0), 1.2, 1.2, 0.0},
                           Pad{"2", PadShape::Rect, Point2D(5, 0), 1.2, 1.2, 0.0}});
    // Through-hole axial diode: round leaded pads, unlike the SMD-style
    // rect pads above -- real diode footprints are commonly THT.
    addFootprintIfMissing(doc, "D_FP", rectOutline(doc, -2, -2, 12, 2),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.6, 1.6, 0.8},
                           Pad{"2", PadShape::Round, Point2D(10, 0), 1.6, 1.6, 0.8}});
    addFootprintIfMissing(doc, "CONN2_FP", rectOutline(doc, -1.5, -1.5, 1.5, 4.04),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.7, 1.7, 1.0},
                           Pad{"2", PadShape::Round, Point2D(0, 2.54), 1.7, 1.7, 1.0}});

    // Footprints for the new schematic symbols above.
    addFootprintIfMissing(doc, "LED_FP", rectOutline(doc, -1.5, -1.5, 6.5, 1.5),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.5, 1.5, 0.7},
                           Pad{"2", PadShape::Round, Point2D(5, 0), 1.5, 1.5, 0.7}});
    // TO-92-style 3-pin package, straight row at 0.1in (2.54mm) pitch.
    addFootprintIfMissing(doc, "Q_NPN_FP", rectOutline(doc, -1.5, -1.5, 6.58, 1.5),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.2, 1.2, 0.6},
                           Pad{"2", PadShape::Round, Point2D(2.54, 0), 1.2, 1.2, 0.6},
                           Pad{"3", PadShape::Round, Point2D(5.08, 0), 1.2, 1.2, 0.6}});
    addFootprintIfMissing(doc, "Q_PNP_FP", rectOutline(doc, -1.5, -1.5, 6.58, 1.5),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.2, 1.2, 0.6},
                           Pad{"2", PadShape::Round, Point2D(2.54, 0), 1.2, 1.2, 0.6},
                           Pad{"3", PadShape::Round, Point2D(5.08, 0), 1.2, 1.2, 0.6}});
    addFootprintIfMissing(doc, "L_FP", rectOutline(doc, -2, -2, 9.5, 2),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 2.0, 2.0, 1.0},
                           Pad{"2", PadShape::Round, Point2D(7.5, 0), 2.0, 2.0, 1.0}});
    addFootprintIfMissing(doc, "SW_FP", rectOutline(doc, -1.5, -1.5, 7.5, 1.5),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.8, 1.8, 1.0},
                           Pad{"2", PadShape::Round, Point2D(6, 0), 1.8, 1.8, 1.0}});
    addFootprintIfMissing(doc, "CONN3_FP", rectOutline(doc, -1.5, -1.5, 1.5, 6.58),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.7, 1.7, 1.0},
                           Pad{"2", PadShape::Round, Point2D(0, 2.54), 1.7, 1.7, 1.0},
                           Pad{"3", PadShape::Round, Point2D(0, 5.08), 1.7, 1.7, 1.0}});
    addFootprintIfMissing(doc, "CONN4_FP", rectOutline(doc, -1.5, -1.5, 1.5, 9.12),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.7, 1.7, 1.0},
                           Pad{"2", PadShape::Round, Point2D(0, 2.54), 1.7, 1.7, 1.0},
                           Pad{"3", PadShape::Round, Point2D(0, 5.08), 1.7, 1.7, 1.0},
                           Pad{"4", PadShape::Round, Point2D(0, 7.62), 1.7, 1.7, 1.0}});

    // Two-pin through-hole can, real HC-49-style crystal footprint pitch.
    addFootprintIfMissing(doc, "XTAL_FP", rectOutline(doc, -2, -3, 6.9, 3),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.5, 1.5, 0.8},
                           Pad{"2", PadShape::Round, Point2D(4.88, 0), 1.5, 1.5, 0.8}});
    // Radial (0.2in lead spacing) through-hole fuse footprint.
    addFootprintIfMissing(doc, "F_FP", rectOutline(doc, -2, -2, 7.08, 2),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.4, 1.4, 0.8},
                           Pad{"2", PadShape::Round, Point2D(5.08, 0), 1.4, 1.4, 0.8}});
    // A 2-pin JST-style connector footprint, since a real battery mounts
    // via a cable connector rather than direct through-hole leads.
    addFootprintIfMissing(doc, "BAT_FP", rectOutline(doc, -1.5, -2, 3.5, 2),
                          {Pad{"1", PadShape::Round, Point2D(0, 0), 1.5, 1.5, 0.8},
                           Pad{"2", PadShape::Round, Point2D(2.0, 0), 1.5, 1.5, 0.8}});
    // 8-pin DIP, real generic single-op-amp package pinout order (1-4
    // down the left side, 5-8 back up the right, matching IC_FP's own
    // 4-pin left/right convention just with twice as many pins per side).
    {
        std::vector<std::unique_ptr<Entity>> body = rectOutline(doc, 0, -12, 20, 12);
        std::vector<Pad> pads = {
            Pad{"1", PadShape::Round, Point2D(0, 9), 1.6, 1.6, 0.8},  Pad{"2", PadShape::Round, Point2D(0, 3), 1.6, 1.6, 0.8},
            Pad{"3", PadShape::Round, Point2D(0, -3), 1.6, 1.6, 0.8}, Pad{"4", PadShape::Round, Point2D(0, -9), 1.6, 1.6, 0.8},
            Pad{"5", PadShape::Round, Point2D(20, -9), 1.6, 1.6, 0.8}, Pad{"6", PadShape::Round, Point2D(20, -3), 1.6, 1.6, 0.8},
            Pad{"7", PadShape::Round, Point2D(20, 3), 1.6, 1.6, 0.8}, Pad{"8", PadShape::Round, Point2D(20, 9), 1.6, 1.6, 0.8},
        };
        addFootprintIfMissing(doc, "OPAMP_FP", std::move(body), std::move(pads));
    }
}

} // namespace lcad
