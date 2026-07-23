#pragma once

#include <string>

namespace lcad {

class Document;
struct BlockDefinition;

// Writes block as a real KiCad .kicad_mod footprint file (modern
// "footprint" s-expression form, KiCad 6+): name, a smd/through_hole
// attr (derived from whether any pad has a drill), every Pad as a KiCad
// (pad ...), and every Line/Arc/Circle/Text child entity of block as
// fp_line/fp_arc/fp_circle/fp_text respectively. A real, interoperable
// subset -- not modeled: fp_poly graphic items, 3D model references,
// net-tie groups, private (front/back-independent) layers, roundrect/
// trapezoid pad corner geometry (written back as a plain rect), and any
// child entity kind other than the four listed (Polyline/Spline/MText/
// etc. children are silently skipped), the same "real subset, disclosed"
// spirit as SpecctraWriter.h. Pad-level rotation is dropped on write
// (Pad, Block.h, has no rotation field of its own). Coordinate/rotation
// sign conventions are written literally (no Y-flip attempted for
// KiCad's own Y-down board coordinate system) -- this keeps a KumCAD-to-
// KumCAD round trip exact, but this hasn't been empirically verified
// against real KiCad software (none available to test against here), so
// treat orientation against an actual KiCad install as unverified.
bool writeKiCadMod(const Document& doc, const BlockDefinition& block, const std::string& path,
                   std::string* errorOut = nullptr);

// Reads path (a real .kicad_mod file, or this writer's own output) as a
// new BlockDefinition added to doc (renamed with a numeric suffix if the
// file's own footprint name is already taken -- addBlock never
// overwrites). Returns the added block, or nullptr on a read/parse
// failure (*errorOut set). Accepts both the modern "footprint" root tag
// and the legacy pre-6 "module" tag.
const BlockDefinition* readKiCadMod(Document& doc, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
