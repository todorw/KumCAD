#pragma once

#include "core/Ids.h"
#include "core/pcb/NetClass.h"

#include <string>
#include <vector>

namespace lcad {

class Document;
struct ImportedNet;

struct AutorouteParams {
    double gridSize = 0.5; // routing grid pitch
    double trackWidth = 0.25;
    double clearance = 0.2;
    LayerId layer = 0; // which layer newly-routed tracks land on
};

struct AutorouteResult {
    int routedCount = 0;
    int failedCount = 0;
    std::vector<std::string> failedNetNames; // one entry per failed ratsnest connection, not de-duplicated
};

// A real in-house autorouter (previously deferred entirely in favor of
// Specctra DSN export for external tools -- this adds actual in-house
// routing on top of that, not instead of it). Discretizes the board into
// a grid (cell = gridSize) and runs a classic Lee/maze breadth-first
// search per still-needed ratsnest connection (see Ratsnest.h), shortest
// connections first (a real, if simple, heuristic real routers use too --
// short connections claim direct paths before long ones have to route
// around them). A cell is an obstacle for a given connection if it's
// within trackWidth/2 + clearance + (the other feature's own half-width)
// of an already-routed track (half-width == trackWidth/2, since every
// track in one autoroute() call shares params.trackWidth) or a pad
// (half-width == pad.radius, its own already-full extent) that ISN'T on
// that connection's own net; a connection's own start/end cells are
// always force-cleared so a pin's own location never blocks its own
// route regardless of nearby other-net pads.
//
// Real, disclosed simplifications: single layer only (no via insertion or
// layer-change routing -- see the plan's own "multi-layer copper
// stackup" as a separate, not-yet-done item), no push/shove of existing
// traces, no length matching or differential pairs, and grid-based paths
// (not the smooth 45-degree-preferring paths a real interactive router
// produces) -- a real, useful "does it connect without shorting"
// autorouter, not KiCad's own interactive push-and-shove router.
//
// Adds one TrackEntity per successfully routed connection directly to
// doc, on params.layer.
//
// netClasses (default empty) overrides params.trackWidth/clearance per
// connection when that connection's own net resolves to a class (see
// core/pcb/NetClass.h) -- a "Power" class can route wider than
// "Default", for instance. A net with no matching class still uses
// params' own single global values, exactly the pre-existing behavior
// when netClasses is left empty altogether.
AutorouteResult autoroute(Document& doc, const std::vector<ImportedNet>& nets, const AutorouteParams& params = {},
                         const std::vector<NetClass>& netClasses = {});

} // namespace lcad
