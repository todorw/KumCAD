#pragma once

#include <string>

namespace lcad {

class Document;

// Real KiCad gap: the built-in footprint library (SymbolLibrary.h) only
// ever had one-off hardcoded footprints -- adding a new package meant
// hand-typing its exact pad list. A real footprint LIBRARY workflow
// generates standard IPC-style package families from a handful of
// parameters instead, which is what this does.

struct GullWingParams {
    int pinCount = 44;        // total pins, must divide evenly by sideCount
    int sideCount = 4;         // 2 (SOIC/SOP-style: pins on left+right only) or 4 (QFP-style: all four sides)
    double pitch = 0.8;        // pin-to-pin spacing along each side
    double bodyWidth = 10.0;   // silkscreen body outline, X
    double bodyLength = 10.0; // silkscreen body outline, Y
    double padWidth = 0.4;    // gull-wing pad dimension along the side
    double padLength = 1.5;   // gull-wing pad dimension perpendicular to the side, toward the body
};

// Generates a real IPC-gull-wing-style footprint (QFP for sideCount==4,
// SOIC/SOP for sideCount==2) into doc as a new BlockDefinition named
// name, with pinCount pads distributed evenly around the body's own
// perimeter in standard pin-1-top-left, counterclockwise numbering (the
// same convention real IPC footprints use) -- pin 1 additionally gets a
// small silkscreen marker line at the body's own top-left corner.
// Returns false (nothing added) if pinCount doesn't divide evenly by
// sideCount, sideCount isn't 2 or 4, name is already a registered
// block, or any dimension is non-positive.
bool generateGullWingFootprint(Document& doc, const std::string& name, const GullWingParams& params);

struct PinHeaderParams {
    int pinCount = 4;   // pins per row
    int rowCount = 1;    // 1 or 2
    double pitch = 2.54; // standard 0.1in pitch
};

// Generates a through-hole pin header footprint (rowCount row(s) of
// pinCount round pads each, at pitch spacing, pin 1 at the top of the
// first row) into doc as a new BlockDefinition named name. Same failure
// conditions as generateGullWingFootprint.
bool generatePinHeaderFootprint(Document& doc, const std::string& name, const PinHeaderParams& params);

// A 2-terminal SMD passive (chip resistor/capacitor) -- pads split by
// padSpacing on either side of center, pin 1 on the left. Real EIA sizes
// map onto this directly: 0402 is roughly padWidth=0.5, padLength=0.6,
// padSpacing=0.5, bodyLength=1.0, bodyWidth=0.5; 0603 roughly
// padWidth=0.9, padLength=0.8, padSpacing=0.9, bodyLength=1.6,
// bodyWidth=0.8; 0805 roughly padWidth=1.0, padLength=1.2,
// padSpacing=1.5, bodyLength=2.0, bodyWidth=1.25 -- callers pass these
// directly rather than this codebase maintaining its own EIA size table,
// matching GullWingParams/PinHeaderParams' own fully-parametric style.
struct ChipPassiveParams {
    double padWidth = 0.9;   // pad dimension along the body's short axis
    double padLength = 0.8;  // pad dimension along the body's long axis (toward the gap)
    double padSpacing = 0.9; // gap between the two pads' facing edges
    double bodyWidth = 0.8;
    double bodyLength = 1.6;
};

// Same failure conditions as generateGullWingFootprint (name already
// registered, or any dimension non-positive).
bool generateChipPassiveFootprint(Document& doc, const std::string& name, const ChipPassiveParams& params);

struct SotParams {
    double pitch = 0.95;      // pin-to-pin spacing on the 2-pin side
    double padWidth = 0.4;
    double padLength = 0.6;
    double bodyWidth = 1.6;
    double bodyLength = 2.9;
};

// SOT-23: 3 gull-wing pads -- pins 1, 2 on the bottom side (spaced
// pitch apart, straddling center), pin 3 centered on the top side --
// the standard SOT-23 pinout real footprint libraries use.
bool generateSot23Footprint(Document& doc, const std::string& name, const SotParams& params);

// SOT-223: 4 gull-wing pads -- pins 1, 2, 3 on the bottom side (spaced
// pitch apart), pin 4 a single wide tab pad spanning the whole top side
// (the package's own heatsink/collector tab) -- padWidth/padLength
// apply to pins 1-3 only, the tab is sized from bodyWidth and its own
// tabPadLength.
struct Sot223Params {
    double pitch = 2.3;
    double padWidth = 0.9;
    double padLength = 1.2;
    double bodyWidth = 3.2;
    double bodyLength = 6.5;
    double tabPadLength = 1.5;
};
bool generateSot223Footprint(Document& doc, const std::string& name, const Sot223Params& params);

struct BgaParams {
    int rows = 4;
    int cols = 4;
    double pitch = 0.8;
    double ballDiameter = 0.4;
    double bodyWidth = 4.0;
    double bodyLength = 4.0;
};

// Generates a full (no depopulated balls) BGA footprint: rows x cols
// round pads on an (pitch, pitch) grid centered under the body, named by
// real JEDEC convention -- a letter for the row (skipping I, O, Q, S, X,
// Z, the same letters JEDEC itself skips to avoid confusion with digits/
// other letters) followed by a 1-based column number, e.g. "A1".."D4"
// for a 4x4 grid. Fails (same conditions as generateGullWingFootprint)
// if rows/cols aren't positive, or rows exceeds the number of available
// JEDEC letters (20 -- Z minus the 6 skipped, wrapping into AA/AB/... is
// a real JEDEC convention for very large grids this doesn't implement).
bool generateBgaFootprint(Document& doc, const std::string& name, const BgaParams& params);

// A single non-electrical hole for mechanically mounting the board (a
// standoff/screw) -- pad number left empty (not part of any net, the
// same convention KiCad's own "MountingHole" library uses) with
// drillDiameter driving Pad's own plated-through-hole rendering.
// padDiameter <= drillDiameter (a common real choice for a purely
// mechanical, non-plated hole) produces a copper-free hole: the pad
// itself is still added at padDiameter so the drill is real, just with
// no annular ring to speak of.
bool generateMountingHoleFootprint(Document& doc, const std::string& name, double drillDiameter, double padDiameter);

// A single round SMD fiducial pad (no drill, no pad number -- not part
// of any net) for pick-and-place optical alignment, matching KiCad's own
// "Fiducial" footprints.
bool generateFiducialFootprint(Document& doc, const std::string& name, double padDiameter);

} // namespace lcad
