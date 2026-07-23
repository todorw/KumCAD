#pragma once

#include "core/geometry/Point2D.h"

#include <optional>
#include <string>
#include <vector>

namespace lcad {

// The hatch patterns KumCAD ships with. Solid is a filled polygon; the rest
// are line-family patterns clipped to the boundary. These are geometric
// reconstructions of the standard drafting conventions the acad.pat names
// denote (ANSI/ISO section-lining symbols, and common material/texture
// symbols) -- angles, spacing ratios and dash cadence are real and true to
// the drafting standard each name refers to, but the exact byte-for-byte
// offsets of Autodesk's own acad.pat are proprietary and not reproduced here.
enum class HatchPattern {
    Solid,
    Ansi31,
    Ansi32,
    Ansi33,
    Ansi34,
    Ansi35,
    Ansi36,
    Ansi37,
    Ansi38,
    Iso02W100,
    Iso03W100,
    Iso04W100,
    Iso05W100,
    Iso06W100,
    Iso07W100,
    Iso08W100,
    Iso09W100,
    Iso10W100,
    Iso11W100,
    Iso12W100,
    Iso13W100,
    Iso14W100,
    Iso15W100,
    Line,
    Dash,
    Dots,
    Cross,
    Net,
    Net3,
    Square,
    Hex,
    Honey,
    Triang,
    Zigzag,
    Angle,
    Brick,
    Grass,
    Gravel,
    Steel,
    Swamp,
    Earth,
};

// One family of parallel lines from a .pat definition, at pattern scale 1:
// successive lines are displaced by `offset` expressed in the line's own
// rotated coordinate system (x along the line -- staggers dashes -- and y
// perpendicular). Dash elements alternate pen-down/pen-up; empty = solid.
struct HatchPatternLine {
    double angleDeg = 0.0;
    Point2D base;
    Point2D offset;
    std::vector<double> dashes;
};

const char* hatchPatternName(HatchPattern pattern);
std::optional<HatchPattern> hatchPatternFromName(const std::string& name);
const std::vector<HatchPatternLine>& hatchPatternLines(HatchPattern pattern);
const std::vector<HatchPattern>& allHatchPatterns();

} // namespace lcad
