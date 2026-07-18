#pragma once

#include "core/Ids.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// An ordered top-to-bottom list of a board's copper layers (by Document
// Layer id), e.g. Top Copper, In1.Cu, In2.Cu, Bottom Copper for a 4-layer
// board. Doesn't enforce any particular layer count.
//
// computeRatsnest (Ratsnest.h) and runDrc (Drc.h) both take an optional
// CopperStackup. Passed as the default-constructed empty stackup (the
// default for both), every Track/Via is treated as one shared copper
// plane regardless of which named layer it's actually on -- the
// project's original, simpler behavior, preserved exactly for every
// existing caller. Passed a real (non-empty) stackup, connectivity and
// DRC clearance both become layer-aware: two tracks on different
// stackup layers that happen to cross in XY are neither electrically
// joined nor a clearance violation unless a ViaEntity actually spans
// both layers at that position (see ViaEntity's own comment for
// through-hole vs. blind/buried vias). A Track on a layer that isn't
// part of the given stackup at all (e.g. silkscreen accidentally typed
// as EntityType::Track) is treated as isolated -- it only connects to
// itself, never merges with anything else by position.
//
// A footprint Pad has no per-pad layer field of its own; instead its side
// is derived from drillDiameter (see Block.h's Pad struct's own comment),
// matching real KiCad's own SMD-vs-THT distinction: a through-hole pad
// (drillDiameter > 0) touches every stackup layer at its position, same
// as a through-hole via, while a surface-mount pad (drillDiameter == 0)
// exists only on the ONE stackup layer its owning InsertEntity is placed
// on.
struct CopperStackup {
    std::vector<LayerId> layers; // top to bottom; empty = the legacy single-plane behavior
};

// Resolves layer names already present in doc into a CopperStackup, in
// the order given, silently skipping any name that isn't an existing
// Document layer.
CopperStackup buildStackup(const Document& doc, const std::vector<std::string>& orderedLayerNames);

} // namespace lcad
