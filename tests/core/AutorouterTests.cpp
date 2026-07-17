#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/pcb/Autorouter.h"
#include "core/pcb/Drc.h"
#include "core/pcb/Ratsnest.h"
#include "core/schematic/SymbolLibrary.h"

#include <catch2/catch_test_macros.hpp>

using namespace lcad;

namespace {
InsertEntity* placeRFp(Document& doc, const std::string& refdes, Point2D at) {
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, at);
    insert->setAttribute("REFDES", refdes);
    InsertEntity* raw = insert.get();
    doc.addEntity(std::move(insert));
    return raw;
}

int trackCount(const Document& doc) {
    int n = 0;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Track) ++n;
    }
    return n;
}
} // namespace

TEST_CASE("autoroute connects two unobstructed pads with a single track", "[pcb][autoroute]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "R1", Point2D(0, 0));
    placeRFp(doc, "R2", Point2D(50, 0));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "2"}, {"R2", "1"}};

    const AutorouteResult result = autoroute(doc, {net});
    REQUIRE(result.routedCount == 1);
    REQUIRE(result.failedCount == 0);
    REQUIRE(trackCount(doc) == 1);

    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Track) continue;
        const auto* track = static_cast<const TrackEntity*>(e);
        REQUIRE(track->vertices().size() >= 2);
        // R1 pad 2 world (10,0), R2 pad 1 world (50,0) -- endpoints are
        // snapped back exactly to the pad positions, not left on-grid.
        const bool matches =
            (track->vertices().front().distanceTo(Point2D(10, 0)) < 1e-6 &&
             track->vertices().back().distanceTo(Point2D(50, 0)) < 1e-6) ||
            (track->vertices().back().distanceTo(Point2D(10, 0)) < 1e-6 &&
             track->vertices().front().distanceTo(Point2D(50, 0)) < 1e-6);
        REQUIRE(matches);
    }

    // No unrelated copper anywhere else, so DRC should be entirely clean.
    REQUIRE(runDrc(doc).empty());
}

TEST_CASE("autoroute detours a later net around an earlier net's already-placed track, staying DRC-clean",
         "[pcb][autoroute]") {
    Document doc;
    registerBuiltinSymbols(doc);
    // Net A: a long vertical run from (0,-5) to (0,15).
    placeRFp(doc, "R1", Point2D(0, -5));
    placeRFp(doc, "R2", Point2D(0, 15));
    // Net B: a shorter horizontal run from (-5,1) to (5,1) that directly
    // crosses Net A's straight-line path at (0,1). Net B is shorter, so it
    // routes first (shortest-connections-first); Net A must then detour
    // around Net B's already-placed track rather than crossing through it.
    placeRFp(doc, "R3", Point2D(-15, 1));
    placeRFp(doc, "R4", Point2D(5, 1));

    ImportedNet netA;
    netA.name = "NetA";
    netA.pins = {{"R1", "1"}, {"R2", "1"}};
    ImportedNet netB;
    netB.name = "NetB";
    netB.pins = {{"R3", "2"}, {"R4", "1"}};

    const AutorouteResult result = autoroute(doc, {netA, netB});
    REQUIRE(result.routedCount == 2);
    REQUIRE(result.failedCount == 0);
    REQUIRE(trackCount(doc) == 2);

    // The real assertion: reuse the already-trusted DRC clearance checker
    // (see Drc.h) to confirm the detour actually happened -- if Net A had
    // instead been routed straight through Net B's track, this would fail
    // with a clearance (in fact overlap) violation between the two nets'
    // copper.
    const std::vector<DrcViolation> violations = runDrc(doc);
    REQUIRE(violations.empty());
}

TEST_CASE("autoroute fails and reports the net when a pin is completely walled in by other-net copper",
         "[pcb][autoroute]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "TARGET", Point2D(0, 0));
    placeRFp(doc, "PARTNER", Point2D(50, 50));
    // Four unrelated (netless) pads placed 1.0 away from the target pin in
    // each cardinal direction. Each has an obstacle radius of
    // padRadius(0.75) + trackWidth(0.25) + clearance(0.2) = 1.2, which is
    // more than the 1.0 spacing between them (and more than sqrt(2) apart
    // diagonally), so together they seal off every neighbor of the target
    // pin's own grid cell with no gap for the search to escape through.
    placeRFp(doc, "B1", Point2D(1, 0));
    placeRFp(doc, "B2", Point2D(-1, 0));
    placeRFp(doc, "B3", Point2D(0, 1));
    placeRFp(doc, "B4", Point2D(0, -1));

    ImportedNet net;
    net.name = "Trapped";
    net.pins = {{"TARGET", "1"}, {"PARTNER", "1"}};

    const AutorouteResult result = autoroute(doc, {net});
    REQUIRE(result.routedCount == 0);
    REQUIRE(result.failedCount == 1);
    REQUIRE(result.failedNetNames == std::vector<std::string>{"Trapped"});
    REQUIRE(trackCount(doc) == 0);
}

TEST_CASE("autoroute on an empty document or with a non-positive grid size is a safe no-op", "[pcb][autoroute]") {
    Document doc;
    registerBuiltinSymbols(doc);

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "1"}, {"R2", "1"}};

    // No footprints placed at all.
    AutorouteResult result = autoroute(doc, {net});
    REQUIRE(result.routedCount == 0);
    REQUIRE(result.failedCount == 0);

    placeRFp(doc, "R1", Point2D(0, 0));
    placeRFp(doc, "R2", Point2D(50, 0));

    AutorouteParams params;
    params.gridSize = 0.0;
    result = autoroute(doc, {net}, params);
    REQUIRE(result.routedCount == 0);
    REQUIRE(result.failedCount == 0);
    REQUIRE(trackCount(doc) == 0);
}
