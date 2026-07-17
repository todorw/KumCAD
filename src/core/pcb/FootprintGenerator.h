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

} // namespace lcad
