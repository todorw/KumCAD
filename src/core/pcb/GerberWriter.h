#pragma once

#include "core/Ids.h"

#include <string>

namespace lcad {

class Document;

// Writes one copper/silkscreen layer's Track/Via/footprint-Pad/copper-pour
// (a solid-fill HatchEntity used as a pour -- see the mega-roadmap memory's
// note that a pour is just a HATCH placed on a copper layer, not a
// dedicated entity type) geometry as a real RS-274X (Gerber X2) file:
// %FS%/%MO% header, real X2 file attributes (%TF.GenerationSoftware%,
// %TF.CreationDate%, %TF.FileFunction% inferred from the layer's own name
// via KiCad-style conventions -- F.Cu/B.Cu/F.SilkS/B.SilkS/F.Mask/B.Mask/
// Edge.Cuts/InN.Cu, falling back to a generic "Other,User" for an
// unrecognized name -- %TF.FilePolarity%, %TF.Part%), one circular
// aperture per distinct diameter used, D01 draws for tracks, D03 flashes
// for pads/vias (each footprint pad's flash wrapped in a real %TO.C%
// component attribute naming its REFDES, cleared with %TD*% once pads are
// done), G36/G37 regions for pours. Assumes the document's units are
// millimeters. Returns false (with *errorOut set, if provided) on a
// file-open failure.
//
// Real, disclosed simplification: no per-net %TO.N% object attribute --
// that needs the same netlist connectivity computation Ratsnest.h already
// does, which this function has no access to (it only sees one layer's
// geometry, not a netlist); %TO.C% (component) is used instead since a
// footprint's own REFDES is directly available without it.
bool writeGerberLayer(const Document& doc, LayerId layer, const std::string& path, std::string* errorOut = nullptr);

// Writes an Excellon drill file (M48 header, one tool per distinct drill
// diameter among vias and through-hole pads, then per-tool coordinate
// blocks, M30 end) covering every layer.
bool writeExcellonDrill(const Document& doc, const std::string& path, std::string* errorOut = nullptr);

// Writes a pick-and-place file (CSV: RefDes,Footprint,X,Y,RotationDeg) for
// every footprint INSERT in doc. This is KumCAD's own simple CSV shape, not
// a specific fab house's required column layout -- documented here rather
// than pretending to match one.
bool writePickAndPlace(const Document& doc, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
