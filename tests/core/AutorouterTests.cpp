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

int viaCount(const Document& doc) {
    int n = 0;
    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Via) ++n;
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

TEST_CASE("autoroute's rip-up-and-reroute recovers a connection a single shortest-first pass leaves unrouted",
         "[pcb][autoroute][ripup]") {
    // CONSTRAINED (TARGET->FAR, length 10) has exactly one way out of
    // TARGET: south/east/west are sealed by wall pads (BS/BE/BW, each
    // 1.2 away -- close enough to obstacle-block those three directions
    // but not the fourth, verified empirically), leaving only north
    // open. FLEXIBLE (R3->R4, length 6) crosses directly over that one
    // exit, horizontally. Under plain shortest-first ordering, FLEXIBLE
    // (shorter) routes first and seals TARGET's only exit, so
    // CONSTRAINED fails -- even though CONSTRAINED WOULD have succeeded
    // if it had gone first (its exit is unobstructed until FLEXIBLE
    // claims it), and FLEXIBLE has plenty of open space to detour around
    // CONSTRAINED's track if forced to go second instead.
    auto build = [](Document& doc) {
        registerBuiltinSymbols(doc);
        placeRFp(doc, "TARGET", Point2D(0, 0));
        placeRFp(doc, "FAR", Point2D(0, 10));
        placeRFp(doc, "BS", Point2D(0, -1.2));
        placeRFp(doc, "BE", Point2D(1.2, 0));
        placeRFp(doc, "BW", Point2D(-1.2, 0));
        placeRFp(doc, "R3", Point2D(-3, 1));
        placeRFp(doc, "R4", Point2D(3, 1));
    };
    ImportedNet constrained;
    constrained.name = "CONSTRAINED";
    constrained.pins = {{"TARGET", "1"}, {"FAR", "1"}};
    ImportedNet flexible;
    flexible.name = "FLEXIBLE";
    flexible.pins = {{"R3", "1"}, {"R4", "1"}};

    Document withoutRipUp;
    build(withoutRipUp);
    AutorouteParams noRetry;
    noRetry.ripUpPasses = 0;
    const AutorouteResult plainResult = autoroute(withoutRipUp, {constrained, flexible}, noRetry);
    REQUIRE(plainResult.routedCount == 1);
    REQUIRE(plainResult.failedCount == 1);
    REQUIRE(plainResult.failedNetNames == std::vector<std::string>{"CONSTRAINED"});

    Document withRipUp;
    build(withRipUp);
    AutorouteParams retry;
    retry.ripUpPasses = 3;
    const AutorouteResult retriedResult = autoroute(withRipUp, {constrained, flexible}, retry);
    REQUIRE(retriedResult.routedCount == 2);
    REQUIRE(retriedResult.failedCount == 0);
    // Ripping up a losing attempt must actually remove its tracks --
    // exactly one TrackEntity per successfully routed net, no leftovers.
    REQUIRE(trackCount(withRipUp) == 2);
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

TEST_CASE("autoroute with an empty stackup never inserts a via, even in a scenario multi-layer mode "
         "would need one for",
         "[pcb][autoroute][multilayer]") {
    // Same sealed-pin construction as the rip-up recovery test: TARGET is
    // walled on 3 sides, FLEXIBLE's track crosses its only exit. Legacy
    // single-layer mode has nowhere to insert a via at all (there's only
    // ever one layer index to route on), so it must fail exactly like the
    // rip-up test's own plain-ordering case, and produce zero vias.
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "TARGET", Point2D(0, 0));
    placeRFp(doc, "FAR", Point2D(0, 10));
    placeRFp(doc, "BS", Point2D(0, -1.2));
    placeRFp(doc, "BE", Point2D(1.2, 0));
    placeRFp(doc, "BW", Point2D(-1.2, 0));
    placeRFp(doc, "R3", Point2D(-3, 1));
    placeRFp(doc, "R4", Point2D(3, 1));

    ImportedNet constrained;
    constrained.name = "CONSTRAINED";
    constrained.pins = {{"TARGET", "1"}, {"FAR", "1"}};
    ImportedNet flexible;
    flexible.name = "FLEXIBLE";
    flexible.pins = {{"R3", "1"}, {"R4", "1"}};

    AutorouteParams params; // stackup left empty: legacy single-layer mode
    params.ripUpPasses = 0;
    const AutorouteResult result = autoroute(doc, {constrained, flexible}, params);
    REQUIRE(result.routedCount == 1);
    REQUIRE(result.failedCount == 1);
    REQUIRE(viaCount(doc) == 0);
}

TEST_CASE("autoroute with a 2-layer stackup recovers a connection legacy single-layer mode leaves "
         "unrouted, landing tracks on the correct stackup layers",
         "[pcb][autoroute][multilayer]") {
    // Identical geometry to the test above: TARGET is walled on 3 sides,
    // FLEXIBLE's track crosses its only exit -- but FLEXIBLE only
    // occupies whichever ONE layer it happens to route on, leaving the
    // other completely free for CONSTRAINED to escape through instead.
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "TARGET", Point2D(0, 0));
    placeRFp(doc, "FAR", Point2D(0, 10));
    placeRFp(doc, "BS", Point2D(0, -1.2));
    placeRFp(doc, "BE", Point2D(1.2, 0));
    placeRFp(doc, "BW", Point2D(-1.2, 0));
    placeRFp(doc, "R3", Point2D(-3, 1));
    placeRFp(doc, "R4", Point2D(3, 1));

    ImportedNet constrained;
    constrained.name = "CONSTRAINED";
    constrained.pins = {{"TARGET", "1"}, {"FAR", "1"}};
    ImportedNet flexible;
    flexible.name = "FLEXIBLE";
    flexible.pins = {{"R3", "1"}, {"R4", "1"}};

    const LayerId topLayer = doc.addLayer("F.Cu", Color{255, 255, 255});
    const LayerId bottomLayer = doc.addLayer("B.Cu", Color{255, 255, 255});

    AutorouteParams params;
    params.ripUpPasses = 0;
    params.stackup.layers = {topLayer, bottomLayer};
    const AutorouteResult result = autoroute(doc, {constrained, flexible}, params);
    REQUIRE(result.routedCount == 2);
    REQUIRE(result.failedCount == 0);

    // Every produced Track must land on one of the stackup's own layers,
    // never some other/default layer id.
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Track) continue;
        const auto* track = static_cast<const TrackEntity*>(e);
        REQUIRE((track->layer() == topLayer || track->layer() == bottomLayer));
    }
}

// Real, disclosed testing gap: no test here forces autoroute() itself to
// place a MID-PATH via (both endpoints reachable, but only via a layer
// switch partway through). This isn't for lack of trying -- every
// construction attempted (short crossing blockers, wall-confined
// corridors, near-conflicting parallel blockers) either let the router
// avoid the obstruction by starting/ending its ENTIRE path on the other,
// still-fully-open layer (needing zero vias) or let it detour a few
// cells in-plane instead (cheaper than a layer switch, which costs the
// same one step as any grid move). That's not a test-construction
// failure so much as a real property of this cost model: since a via
// never costs less than continuing on the current layer, a mid-path via
// is only ever optimal when BOTH layers are independently blocked
// end-to-end at DIFFERENT points, and reliably forcing that with real
// footprint-sized geometry (R_FP's own 1.5x1.5 pads, ~1.075 obstacle
// radius once clearance is added) proved impractical by hand. The
// two tests above cover what's reliably verifiable: multi-layer mode
// recovers a connection legacy mode can't route at all, and every
// produced track lands on a real stackup layer. Via construction itself
// (TrackEntity split into layer-contiguous runs, ViaEntity dropped at
// each transition with throughHole=true) was verified by code review
// instead -- see runRoutingPass's own comment in Autorouter.cpp.

TEST_CASE("autoroute treats a KeepoutZone with blocksAutorouting as a real, impassable obstacle",
         "[pcb][autoroute][keepout]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "R1", Point2D(0, 0));
    placeRFp(doc, "R2", Point2D(50, 0));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "2"}, {"R2", "1"}};

    // Both pads sit at y=0, so the routing grid's own Y extent is just a
    // thin band (params.gridSize*4 margin) around it -- a keepout
    // spanning well past that band vertically, between the two pads in
    // X, walls off every possible route rather than just the direct one.
    KeepoutZone wall;
    wall.polygon = {{25, -10}, {30, -10}, {30, 10}, {25, 10}};

    AutorouteParams params;
    params.keepouts = {wall};

    const AutorouteResult result = autoroute(doc, {net}, params);
    REQUIRE(result.routedCount == 0);
    REQUIRE(result.failedCount == 1);
    REQUIRE(trackCount(doc) == 0);
}

TEST_CASE("autoroute ignores a KeepoutZone with blocksAutorouting disabled", "[pcb][autoroute][keepout]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeRFp(doc, "R1", Point2D(0, 0));
    placeRFp(doc, "R2", Point2D(50, 0));

    ImportedNet net;
    net.name = "Net1";
    net.pins = {{"R1", "2"}, {"R2", "1"}};

    KeepoutZone zone;
    zone.polygon = {{25, -10}, {30, -10}, {30, 10}, {25, 10}};
    zone.blocksAutorouting = false; // e.g. a copper-pour-only keepout

    AutorouteParams params;
    params.keepouts = {zone};

    const AutorouteResult result = autoroute(doc, {net}, params);
    REQUIRE(result.routedCount == 1);
    REQUIRE(result.failedCount == 0);
}
