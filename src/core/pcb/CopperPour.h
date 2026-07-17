#pragma once

#include "core/Ids.h"
#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

class Document;

// Thermal relief (KiCad's own default pad-to-pour connection style):
// instead of flooding solid copper right up to an own-net pad, an
// annular gap (radius antipadRadius around the pad) is cut, crossed only
// by spokeCount narrow corridors (width spokeWidth) -- easier to hand-
// solder/rework than a pad drowned in a large copper pour's heat-sinking
// mass. Disabled (enabled=false) keeps this codebase's original
// behavior: own-net pads connect with solid copper, no gap at all.
struct ThermalReliefParams {
    bool enabled = false;
    double antipadRadius = 0.6;
    double spokeWidth = 0.3;
    int spokeCount = 4; // evenly spaced; 4 gives KiCad's classic "+" relief
};

// Builds a copper pour that automatically keeps clearance from other-net
// copper, instead of relying purely on DRC to flag violations after the
// fact (see Drc.h) -- a real auto-clearance cutout, but a disclosed
// approximate one: HatchEntity has no holes concept (see Hatch.h), and
// this codebase has no general 2D polygon-boolean/clipping library, so
// the boundary is rasterized into a grid of gridSize cells, keeping only
// cells whose center lands inside the requested boundary AND at least
// clearance away from every Pad/Track/Via that isn't at one of
// ownNetPositions (the pour's own net's pad locations -- copper actually
// AT one of those points is exempt from clearance against itself; copper
// merely connected further away without a pad exactly there is treated
// as "other net" too, a real, disclosed limitation of not tracing full
// electrical connectivity here the way Drc.cpp/Ratsnest.cpp do). This is
// the same "grid/voxel instead of exact computational geometry" call
// Fem.h's own mesher made, applied to 2D. Adjacent same-row kept cells
// are merged into wider rectangular HatchEntity pieces rather than one
// entity per tiny cell.
//
// Returns the ids of every HatchEntity piece added (on layer).
std::vector<EntityId> buildCopperPourWithClearance(Document& doc, LayerId layer, const std::vector<Point2D>& boundary,
                                                    const std::vector<Point2D>& ownNetPositions, double gridSize = 0.5,
                                                    double clearance = 0.2,
                                                    const ThermalReliefParams& thermalRelief = {});

} // namespace lcad
