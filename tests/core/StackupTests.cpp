#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/pcb/Drc.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/Stackup.h"
#include "core/schematic/SymbolLibrary.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;

namespace {
bool hasClearanceViolationFor(const std::vector<DrcViolation>& violations, EntityId id) {
    return std::any_of(violations.begin(), violations.end(), [&](const DrcViolation& v) {
        return v.entityId == id && v.message.find("Clearance") != std::string::npos;
    });
}
} // namespace

TEST_CASE("buildStackup resolves known layer names in order and skips unknown ones", "[pcb][stackup]") {
    Document doc;
    const LayerId top = doc.addLayer("Top Copper", Color{});
    const LayerId bottom = doc.addLayer("Bottom Copper", Color{});

    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Nonexistent", "Bottom Copper"});
    REQUIRE(stackup.layers.size() == 2);
    REQUIRE(stackup.layers[0] == top);
    REQUIRE(stackup.layers[1] == bottom);
}

TEST_CASE("runDrc is layer-agnostic without a stackup but layer-aware with one", "[pcb][drc][stackup]") {
    Document doc;
    const LayerId top = doc.addLayer("Top Copper", Color{});
    const LayerId bottom = doc.addLayer("Bottom Copper", Color{});

    // Track1 (top) and Track2 (bottom) run parallel with a 0.05 gap --
    // well under the 0.2 default clearance, but on different copper
    // layers, so they can never actually short.
    auto track1 = std::make_unique<TrackEntity>(doc.reserveEntityId(), top,
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0)}, 0.25);
    const EntityId track1Id = track1->id();
    doc.addEntity(std::move(track1));
    auto track2 = std::make_unique<TrackEntity>(doc.reserveEntityId(), bottom,
                                                std::vector<Point2D>{Point2D(0, 0.05), Point2D(10, 0.05)}, 0.25);
    doc.addEntity(std::move(track2));

    // Track3 and Track4 are both on top, just as close together -- a real
    // same-layer clearance problem that a stackup must NOT wave through.
    auto track3 = std::make_unique<TrackEntity>(doc.reserveEntityId(), top,
                                                std::vector<Point2D>{Point2D(20, 0), Point2D(30, 0)}, 0.25);
    const EntityId track3Id = track3->id();
    doc.addEntity(std::move(track3));
    auto track4 = std::make_unique<TrackEntity>(doc.reserveEntityId(), top,
                                                std::vector<Point2D>{Point2D(20, 0.05), Point2D(30, 0.05)}, 0.25);
    doc.addEntity(std::move(track4));

    const std::vector<DrcViolation> legacy = runDrc(doc);
    REQUIRE(hasClearanceViolationFor(legacy, track1Id));
    REQUIRE(hasClearanceViolationFor(legacy, track3Id));

    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Bottom Copper"});
    const std::vector<DrcViolation> layered = runDrc(doc, {}, stackup);
    REQUIRE_FALSE(hasClearanceViolationFor(layered, track1Id));
    REQUIRE(hasClearanceViolationFor(layered, track3Id));
}

TEST_CASE("computeRatsnest joins tracks on different layers through a through-hole via, but not a blind via "
         "that doesn't span both",
         "[pcb][ratsnest][stackup]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const LayerId top = doc.addLayer("Top Copper", Color{});
    const LayerId bottom = doc.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Bottom Copper"});

    const BlockDefinition* rfp = doc.findBlock("R_FP");
    auto r1 = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    r1->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(r1));
    auto r2 = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 20));
    r2->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(r2));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "1"}, {"R2", "1"}}; // R1 pad1 world (0,0), R2 pad1 world (0,20)

    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), top,
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(0, 10)}, 0.25));
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), bottom,
                                                std::vector<Point2D>{Point2D(0, 10), Point2D(0, 20)}, 0.25));

    auto throughVia = std::make_unique<ViaEntity>(doc.reserveEntityId(), top, Point2D(0, 10), 0.6, 0.3);
    doc.addEntity(std::move(throughVia));

    // A through-hole via at the junction bridges top and bottom -- the net
    // is already fully routed, so no airwire remains.
    const std::vector<RatsnestLine> throughLines = computeRatsnest(doc, {net}, stackup);
    REQUIRE(throughLines.empty());

    // Replace it with a degenerate "blind" via that only spans Top Copper
    // to Top Copper -- it never actually reaches the bottom track, so the
    // net is still unrouted.
    Document doc2;
    registerBuiltinSymbols(doc2);
    const LayerId top2 = doc2.addLayer("Top Copper", Color{});
    const LayerId bottom2 = doc2.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup2 = buildStackup(doc2, {"Top Copper", "Bottom Copper"});

    auto r1b = std::make_unique<InsertEntity>(doc2.reserveEntityId(), doc2.currentLayer(), doc2.findBlock("R_FP"), Point2D(0, 0));
    r1b->setAttribute("REFDES", "R1");
    doc2.addEntity(std::move(r1b));
    auto r2b = std::make_unique<InsertEntity>(doc2.reserveEntityId(), doc2.currentLayer(), doc2.findBlock("R_FP"), Point2D(0, 20));
    r2b->setAttribute("REFDES", "R2");
    doc2.addEntity(std::move(r2b));

    doc2.addEntity(std::make_unique<TrackEntity>(doc2.reserveEntityId(), top2,
                                                 std::vector<Point2D>{Point2D(0, 0), Point2D(0, 10)}, 0.25));
    doc2.addEntity(std::make_unique<TrackEntity>(doc2.reserveEntityId(), bottom2,
                                                 std::vector<Point2D>{Point2D(0, 10), Point2D(0, 20)}, 0.25));

    auto blindVia = std::make_unique<ViaEntity>(doc2.reserveEntityId(), top2, Point2D(0, 10), 0.6, 0.3);
    blindVia->throughHole = false;
    blindVia->fromLayer = top2;
    blindVia->toLayer = top2;
    doc2.addEntity(std::move(blindVia));

    const std::vector<RatsnestLine> blindLines = computeRatsnest(doc2, {net}, stackup2);
    REQUIRE(blindLines.size() == 1);
}

TEST_CASE("runDrc doesn't flag two SMD pads stacked on opposite copper layers, but still flags two on "
         "the same layer",
         "[pcb][drc][stackup]") {
    Document doc;
    registerBuiltinSymbols(doc); // R_FP's own pads are SMD (drillDiameter 0)
    const LayerId top = doc.addLayer("Top Copper", Color{});
    const LayerId bottom = doc.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Bottom Copper"});
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    // Two R_FP's placed at the SAME XY, one on Top, one on Bottom -- their
    // SMD pads coincide in plan but sit on opposite real copper layers, so
    // this is not a real short.
    auto rTop = std::make_unique<InsertEntity>(doc.reserveEntityId(), top, rfp, Point2D(0, 0));
    rTop->setAttribute("REFDES", "R1");
    const EntityId rTopId = rTop->id();
    doc.addEntity(std::move(rTop));
    auto rBottom = std::make_unique<InsertEntity>(doc.reserveEntityId(), bottom, rfp, Point2D(0, 0));
    rBottom->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(rBottom));

    const std::vector<DrcViolation> stacked = runDrc(doc, {}, stackup);
    REQUIRE_FALSE(hasClearanceViolationFor(stacked, rTopId));

    // Same scenario, but both on Top: now it's a real same-layer clash.
    Document doc2;
    registerBuiltinSymbols(doc2);
    const LayerId top2 = doc2.addLayer("Top Copper", Color{});
    doc2.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup2 = buildStackup(doc2, {"Top Copper", "Bottom Copper"});
    const BlockDefinition* rfp2 = doc2.findBlock("R_FP");

    // Offset slightly (not exactly coincident -- two pads at the EXACT
    // same point would union into one connectivity root and read as
    // "already the same net" rather than a clearance violation).
    auto r1 = std::make_unique<InsertEntity>(doc2.reserveEntityId(), top2, rfp2, Point2D(0, 0));
    r1->setAttribute("REFDES", "R1");
    const EntityId r1Id = r1->id();
    doc2.addEntity(std::move(r1));
    auto r2 = std::make_unique<InsertEntity>(doc2.reserveEntityId(), top2, rfp2, Point2D(0.1, 0));
    r2->setAttribute("REFDES", "R2");
    doc2.addEntity(std::move(r2));

    const std::vector<DrcViolation> sameLayer = runDrc(doc2, {}, stackup2);
    REQUIRE(hasClearanceViolationFor(sameLayer, r1Id));
}

TEST_CASE("computeRatsnest doesn't join an SMD pad to a track on the opposite copper layer just because "
         "they coincide in plan (no via)",
         "[pcb][ratsnest][stackup]") {
    Document doc;
    registerBuiltinSymbols(doc); // R_FP's own pads are SMD (drillDiameter 0)
    const LayerId top = doc.addLayer("Top Copper", Color{});
    const LayerId bottom = doc.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Bottom Copper"});
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    // R1 placed on Top; its pad 1 sits at world (0,0). A track on the
    // BOTTOM layer happens to pass through (0,0) too, but with no via
    // there, an SMD pad on Top must never electrically join it.
    auto r1 = std::make_unique<InsertEntity>(doc.reserveEntityId(), top, rfp, Point2D(0, 0));
    r1->setAttribute("REFDES", "R1");
    doc.addEntity(std::move(r1));
    auto r2 = std::make_unique<InsertEntity>(doc.reserveEntityId(), top, rfp, Point2D(0, 20));
    r2->setAttribute("REFDES", "R2");
    doc.addEntity(std::move(r2));

    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), bottom,
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(0, 20)}, 0.25));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "1"}, {"R2", "1"}};

    const std::vector<RatsnestLine> lines = computeRatsnest(doc, {net}, stackup);
    // Still unrouted: the bottom track never actually reaches either SMD pad.
    REQUIRE(lines.size() == 1);
}
