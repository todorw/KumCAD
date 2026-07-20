#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/pcb/Autorouter.h"
#include "core/pcb/Drc.h"
#include "core/pcb/NetClass.h"
#include "core/pcb/Ratsnest.h"
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
InsertEntity* placeRFp(Document& doc, const std::string& refdes, Point2D at) {
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, at);
    insert->setAttribute("REFDES", refdes);
    InsertEntity* raw = insert.get();
    doc.addEntity(std::move(insert));
    return raw;
}
} // namespace

TEST_CASE("findNetClass resolves a net to its own class, or nullptr if none match", "[pcb][netclass]") {
    std::vector<NetClass> classes = {{.name = "Power", .clearance = 0.3, .trackWidth = 0.5, .netNames = {"VCC", "GND"}},
                                     {.name = "Signal", .clearance = 0.15, .trackWidth = 0.2, .netNames = {"DATA"}}};
    REQUIRE(findNetClass(classes, "VCC") != nullptr);
    REQUIRE(findNetClass(classes, "VCC")->name == "Power");
    REQUIRE(findNetClass(classes, "DATA")->name == "Signal");
    REQUIRE(findNetClass(classes, "UNKNOWN") == nullptr);
    REQUIRE(findNetClass(classes, "") == nullptr);
    REQUIRE(findNetClass({}, "VCC") == nullptr);
}

TEST_CASE("runDrc flags a track under its own net class's trackWidth even when it clears the global minimum",
         "[pcb][drc][netclass]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "R1", Point2D(0, 0));

    // Stops short of R1's own pad 2 (at world (10,0)) so this track's own
    // end doesn't itself trip an unrelated clearance violation against
    // it, which would confound the entityId-only check below.
    auto track = std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                               std::vector<Point2D>{Point2D(0, 0), Point2D(5, 0)}, 0.15);
    const EntityId trackId = track->id();
    doc.addEntity(std::move(track));

    ImportedNet net;
    net.name = "Power";
    net.pins = {{"R1", "1"}};

    // 0.15 clears the default global minTrackWidth (0.15) exactly, so no
    // net-class-unaware call flags it.
    const std::vector<DrcViolation> legacy = runDrc(doc);
    REQUIRE(std::none_of(legacy.begin(), legacy.end(),
                        [&](const DrcViolation& v) { return v.entityId == trackId; }));

    NetClass powerClass;
    powerClass.name = "Power";
    powerClass.trackWidth = 0.3; // stricter than the track's own 0.15
    powerClass.clearance = 0.2;
    powerClass.netNames = {"Power"};

    const std::vector<DrcViolation> withClass = runDrc(doc, {}, {}, {net}, {powerClass});
    const bool flagged = std::any_of(withClass.begin(), withClass.end(), [&](const DrcViolation& v) {
        return v.entityId == trackId && v.message.find("Track width") != std::string::npos;
    });
    REQUIRE(flagged);
}

TEST_CASE("runDrc uses the stricter of two nets' own class clearances between their copper",
         "[pcb][drc][netclass]") {
    Document doc;
    registerBuiltinSymbols(doc);
    // Footprints placed far from where the two tracks actually run
    // parallel, and far from each other, so neither pad confounds the
    // clearance check below -- only each track's OWN net connects it to
    // its own footprint (one shared endpoint), an L-shaped path carrying
    // it away before the two tracks' far segments run close together.
    placeRFp(doc, "R1", Point2D(0, 0));
    placeRFp(doc, "R2", Point2D(0, 50));

    auto trackA = std::make_unique<TrackEntity>(
        doc.reserveEntityId(), doc.currentLayer(),
        std::vector<Point2D>{Point2D(0, 0), Point2D(0, 10), Point2D(10, 10)}, 0.1);
    const EntityId trackAId = trackA->id();
    doc.addEntity(std::move(trackA));
    auto trackB = std::make_unique<TrackEntity>(
        doc.reserveEntityId(), doc.currentLayer(),
        std::vector<Point2D>{Point2D(0, 50), Point2D(0, 10.5), Point2D(10, 10.5)}, 0.1);
    doc.addEntity(std::move(trackB));

    ImportedNet netA;
    netA.name = "NetA";
    netA.pins = {{"R1", "1"}};
    ImportedNet netB;
    netB.name = "NetB";
    netB.pins = {{"R2", "1"}};

    // Gap between the tracks' parallel far segments is 0.5 - width(0.1)
    // = 0.4 -- comfortably clear of the default 0.2 minClearance (kept
    // well away from that boundary rather than exactly on it, to avoid
    // a floating-point-rounding false positive/negative right at the
    // threshold).
    DrcRules rules;
    const std::vector<DrcViolation> legacy = runDrc(doc, rules);
    REQUIRE_FALSE(hasClearanceViolationFor(legacy, trackAId));

    NetClass tight;
    tight.name = "Tight";
    tight.clearance = 0.1;
    tight.netNames = {"NetA"};
    NetClass wide;
    wide.name = "Wide";
    wide.clearance = 0.6; // stricter than the actual 0.4 gap -- must win over Tight's own 0.1
    wide.netNames = {"NetB"};

    const std::vector<DrcViolation> withClasses = runDrc(doc, rules, {}, {netA, netB}, {tight, wide});
    REQUIRE(hasClearanceViolationFor(withClasses, trackAId));
}

TEST_CASE("autoroute routes each net at its own net class's track width", "[pcb][autoroute][netclass]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "R1", Point2D(0, 0));
    placeRFp(doc, "R2", Point2D(50, 0));
    placeRFp(doc, "R3", Point2D(0, 20));
    placeRFp(doc, "R4", Point2D(50, 20));

    ImportedNet powerNet;
    powerNet.name = "Power";
    powerNet.pins = {{"R1", "2"}, {"R2", "1"}};
    ImportedNet signalNet;
    signalNet.name = "Signal";
    signalNet.pins = {{"R3", "2"}, {"R4", "1"}};

    NetClass powerClass;
    powerClass.name = "PowerClass";
    powerClass.trackWidth = 0.8;
    powerClass.clearance = 0.3;
    powerClass.netNames = {"Power"};
    NetClass signalClass;
    signalClass.name = "SignalClass";
    signalClass.trackWidth = 0.2;
    signalClass.clearance = 0.2;
    signalClass.netNames = {"Signal"};

    const AutorouteResult result = autoroute(doc, {powerNet, signalNet}, {}, {powerClass, signalClass});
    REQUIRE(result.routedCount == 2);
    REQUIRE(result.failedCount == 0);

    bool foundPowerWidth = false, foundSignalWidth = false;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Track) continue;
        const auto* track = static_cast<const TrackEntity*>(e);
        if (track->width() == 0.8) foundPowerWidth = true;
        if (track->width() == 0.2) foundSignalWidth = true;
    }
    REQUIRE(foundPowerWidth);
    REQUIRE(foundSignalWidth);

    // Cross-checked with the net-class-aware DRC: still perfectly clean.
    const std::vector<DrcViolation> violations = runDrc(doc, {}, {}, {powerNet, signalNet}, {powerClass, signalClass});
    REQUIRE(violations.empty());
}

// Real, disclosed testing gap, same one AutorouterTests.cpp's own
// multilayer section already documents: reliably forcing autoroute()
// itself to place a MID-PATH via (not just recover a connection via
// multi-layer mode in general) proved impractical after several
// constructions -- the cost model never prefers a via over continuing
// on a layer that's still open end-to-end, so there's no test here
// asserting a specific via's own diameter/drillDiameter. The
// per-connection resolution itself (findNetClass, then
// connClass ? connClass->viaDiameter : params.viaDiameter) is the exact
// same pattern connTrackWidth/connClearance already use just above,
// and those ARE verified end-to-end by the track-width test above this
// comment -- reviewed by inspection rather than a flaky forced-via test.
