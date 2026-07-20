#pragma once

#include "core/Ids.h"
#include "core/pcb/NetClass.h"
#include "core/pcb/Ratsnest.h"
#include "core/pcb/Stackup.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// Design rule check thresholds, in the document's drawing units (typically
// mm). Defaults are conservative hobbyist-fab numbers, not any particular
// fab house's real capability table.
struct DrcRules {
    double minClearance = 0.2;
    double minTrackWidth = 0.15;
    double minViaDiameter = 0.3;
    double minViaDrillDiameter = 0.2;
    // Copper (diameter/width) left around a drilled hole -- via or
    // through-hole pad, checked the same way for both. A real fab-house
    // rule (too little annular ring risks the hole breaking out of the
    // copper if drilling drifts even slightly), always on like the
    // checks above since it's derived purely from fields every via/pad
    // already has.
    double minAnnularRing = 0.15;
    // Minimum center-to-center distance, minus both drill radii, between
    // any two UNCONNECTED drilled holes (via-via, via-pad, pad-pad) --
    // a real mechanical constraint (drill bits colliding), not an
    // electrical one, so it applies even where copper clearance
    // wouldn't. Skips hole pairs already electrically joined (same
    // connectivity component as the main clearance check), the same
    // real, disclosed simplification: a via deliberately stacked
    // directly in a through-hole pad is intentional design, not a
    // manufacturing defect, and would otherwise always false-positive.
    double minHoleToHoleClearance = 0.25;

    // All three below default OFF: unlike the checks above (always on),
    // they have no natural "already disabled" input the way an empty
    // nets/netClasses list does, so they're opt-in flags instead --
    // turning them on can't silently change behavior for any existing
    // caller that never asked for them.
    bool checkCourtyards = false; // flag any two footprints whose own bounding box, expanded by courtyardMargin, overlaps
    double courtyardMargin = 0.25;
    bool checkSilkscreenOverPad = false; // flag any footprint's own silkscreen graphics coming within silkscreenClearance of ANY pad (its own or another's)
    double silkscreenClearance = 0.0;
    // Flags copper (track/via/pad) that's outside the board outline
    // entirely, or inside it but within boardEdgeClearance of its own
    // edge -- derived from the Edge.Cuts layer via BoardOutline.h's own
    // deriveBoardOutline, same as every other board-boundary-consuming
    // function here. Off by default since not every document/test has
    // Edge.Cuts geometry drawn at all; silently skipped (not an error)
    // when deriveBoardOutline finds nothing even when this IS on, the
    // same "nothing to check against" reasoning courtyard/silkscreen
    // checks don't need since footprints/pads always exist by the time
    // those run.
    bool checkBoardEdgeClearance = false;
    double boardEdgeClearance = 0.3;
};

struct DrcViolation {
    std::string message;
    EntityId entityId = 0;
};

// Checks (all approximate -- see runDrc's own comment for what's simplified):
//  - every Track narrower than rules.minTrackWidth;
//  - every Via smaller than rules.minViaDiameter, or with a drill hole not
//    smaller than its own diameter, or a drill under rules.minViaDrillDiameter;
//  - clearance: any two copper features (Track/Via/footprint Pad) that are
//    NOT already electrically joined by existing copper (same connectivity
//    rule as core/pcb/Ratsnest.h) and whose closest approach is under
//    rules.minClearance. Two simplifications, both disclosed: (a)
//    pads/tracks/vias are approximated as circles/capsules -- a rect pad's
//    clearance uses its larger half-dimension as a radius, not exact
//    polygon-to-polygon distance; (b) without a real (non-empty) stackup
//    argument (see core/pcb/Stackup.h), this check is layer-agnostic -- a
//    real two-layer board could have safely-overlapping top/bottom traces
//    this flags as violations. Passing a real stackup makes the clearance
//    check layer-aware for Track-vs-Track pairs specifically (skipped
//    when both tracks resolve to different stackup layers); Via/Pad
//    clearance stays layer-agnostic even then, since neither carries a
//    per-layer position of its own yet (Pad) or is meaningfully
//    restricted to one layer at all (a via's whole point is to occupy
//    the drill site on every layer it spans).
//
// nets/netClasses (both default empty) add KiCad-style per-net-class
// rules on top of the above: with nets supplied, each pad resolves to
// its own net name (same REFDES+pin matching Ratsnest.h uses) and that
// name propagates through the SAME connectivity graph this function
// already builds -- so every Track/Via belonging to a net's copper
// (even though neither stores a net name of its own) resolves to it
// too. With netClasses also supplied: a Track narrower than its own
// net's class's trackWidth is flagged (falling back to
// rules.minTrackWidth for any net with no matching class); a clearance
// check between two different nets uses the LARGER of their two
// classes' own clearance values (falling back to rules.minClearance
// when either side has no matching class) -- both real KiCad
// conventions. Passing nets without netClasses (or vice versa) has no
// effect; both are needed together for per-class rules to apply.
//
// Two more always-on checks: minimum annular ring on every via AND
// through-hole pad (rules.minAnnularRing); and hole-to-hole clearance
// between every pair of unconnected drilled holes -- vias and
// through-hole pads alike (rules.minHoleToHoleClearance).
//
// Three more checks, opt-in via rules.checkCourtyards/
// checkSilkscreenOverPad/checkBoardEdgeClearance (default off -- see
// DrcRules's own comment on why these can't just default from an empty
// list the way stackup/net classes do): courtyard overlap (no dedicated
// courtyard layer/shape exists in this codebase, so each footprint's own
// INSERT bounding box, expanded by courtyardMargin, stands in for one --
// a real, disclosed approximation) between any two footprints;
// silkscreen-over-pad (a footprint's own body/silkscreen graphics, via
// InsertEntity's instantiate(), coming within silkscreenClearance of ANY
// pad, its own included -- reuses each entity's own distanceTo() against
// every pad approximated as a circle, the same convention the clearance
// check above already uses for pads); and board-edge clearance (any
// copper outside the Edge.Cuts-derived board outline, or too close to
// its own edge -- see DrcRules::checkBoardEdgeClearance's own comment).
std::vector<DrcViolation> runDrc(const Document& doc, const DrcRules& rules = {}, const CopperStackup& stackup = {},
                                 const std::vector<ImportedNet>& nets = {}, const std::vector<NetClass>& netClasses = {});

} // namespace lcad
