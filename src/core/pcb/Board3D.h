#pragma once

#include "core/pcb/Stackup.h"

#include <TopoDS_Shape.hxx>

#include <utility>
#include <vector>

namespace lcad {

class Document;

// Real KiCad gap: no 3D board view at all -- reuses this codebase's
// existing OCCT-based 3D viewport (the same one core3d's MCAD side
// already uses) rather than a new rendering engine, by turning a 2D PCB
// Document's own entities into real TopoDS_Shape solids.
struct Board3DParams {
    double boardThickness = 1.6;    // standard FR4 thickness, mm
    double copperThickness = 0.035; // 1oz copper, mm
    double componentHeight = 3.0;   // generic placeholder component body height, mm
};

struct Board3DShapes {
    TopoDS_Shape substrate;                // the bare board outline, extruded to boardThickness
    std::vector<TopoDS_Shape> copper;       // one shape per Track segment/Via/Pad
    std::vector<TopoDS_Shape> components; // one placeholder box per placed footprint
};

// Builds real 3D geometry from doc's own Track/Via/footprint-Insert
// entities. boardOutline (a closed polygon in plan) supplies the
// substrate shape, since a PCB Document has no dedicated board-outline
// entity type of its own yet -- pass the same boundary a copper pour or
// milling operation would use; empty/degenerate leaves substrate null.
//
// stackup (empty = single layer) places each copper layer's Z height by
// linearly interpolating from the top layer (at the board's own top
// surface) down to the bottom layer (at its bottom surface) -- a real,
// disclosed simplification, since CopperStackup itself carries no
// per-layer dielectric thickness to place inner layers by by properly.
// A through-hole via spans the full board thickness; a blind/buried one
// spans only its own resolved [fromLayer,toLayer] Z range (see
// ViaEntity's own comment). A footprint pad's own side is derived from
// drillDiameter the same way Stackup.h/Drc.cpp/Ratsnest.cpp do: a
// through-hole pad (drillDiameter > 0) gets real copper on both the top
// and bottom surfaces, a surface-mount pad only on its own footprint's
// placement layer. Each placed footprint also gets one placeholder box
// (componentHeight tall, sized to the footprint's own silkscreen
// bounding box) standing in for its real 3D model -- not the part's
// actual model, since no 3D component-model library exists here.
Board3DShapes buildBoard3D(const Document& doc, const std::vector<std::pair<double, double>>& boardOutline,
                          const CopperStackup& stackup, const Board3DParams& params = {});

} // namespace lcad
