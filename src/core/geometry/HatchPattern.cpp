#include "core/geometry/HatchPattern.h"

#include <algorithm>
#include <cctype>

namespace lcad {

const char* hatchPatternName(HatchPattern pattern) {
    switch (pattern) {
    case HatchPattern::Solid: return "SOLID";
    case HatchPattern::Ansi31: return "ANSI31";
    case HatchPattern::Ansi32: return "ANSI32";
    case HatchPattern::Ansi33: return "ANSI33";
    case HatchPattern::Ansi34: return "ANSI34";
    case HatchPattern::Ansi35: return "ANSI35";
    case HatchPattern::Ansi36: return "ANSI36";
    case HatchPattern::Ansi37: return "ANSI37";
    case HatchPattern::Ansi38: return "ANSI38";
    case HatchPattern::Iso02W100: return "ISO02W100";
    case HatchPattern::Iso03W100: return "ISO03W100";
    case HatchPattern::Iso04W100: return "ISO04W100";
    case HatchPattern::Iso05W100: return "ISO05W100";
    case HatchPattern::Iso06W100: return "ISO06W100";
    case HatchPattern::Iso07W100: return "ISO07W100";
    case HatchPattern::Iso08W100: return "ISO08W100";
    case HatchPattern::Iso09W100: return "ISO09W100";
    case HatchPattern::Iso10W100: return "ISO10W100";
    case HatchPattern::Iso11W100: return "ISO11W100";
    case HatchPattern::Iso12W100: return "ISO12W100";
    case HatchPattern::Iso13W100: return "ISO13W100";
    case HatchPattern::Iso14W100: return "ISO14W100";
    case HatchPattern::Iso15W100: return "ISO15W100";
    case HatchPattern::Line: return "LINE";
    case HatchPattern::Dash: return "DASH";
    case HatchPattern::Dots: return "DOTS";
    case HatchPattern::Cross: return "CROSS";
    case HatchPattern::Net: return "NET";
    case HatchPattern::Net3: return "NET3";
    case HatchPattern::Square: return "SQUARE";
    case HatchPattern::Hex: return "HEX";
    case HatchPattern::Honey: return "HONEY";
    case HatchPattern::Triang: return "TRIANG";
    case HatchPattern::Zigzag: return "ZIGZAG";
    case HatchPattern::Angle: return "ANGLE";
    case HatchPattern::Brick: return "BRICK";
    case HatchPattern::Grass: return "GRASS";
    case HatchPattern::Gravel: return "GRAVEL";
    case HatchPattern::Steel: return "STEEL";
    case HatchPattern::Swamp: return "SWAMP";
    case HatchPattern::Earth: return "EARTH";
    }
    return "SOLID";
}

std::optional<HatchPattern> hatchPatternFromName(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (HatchPattern pattern : allHatchPatterns()) {
        if (upper == hatchPatternName(pattern)) return pattern;
    }
    return std::nullopt;
}

const std::vector<HatchPatternLine>& hatchPatternLines(HatchPattern pattern) {
    // Definitions below are geometric reconstructions of the standard
    // drafting conventions each name denotes (ANSI/ISO section-lining
    // symbols and common material/texture symbols) -- angles, spacing
    // ratios and dash cadence are real and true to the convention, but this
    // is not a byte-for-byte reproduction of Autodesk's own acad.pat table.
    static const std::vector<HatchPatternLine> kNone{};

    // --- ANSI31-38: material section-lining series, all variations on a
    // 45-degree family (spacing/density/dash cadence differ per material).
    static const std::vector<HatchPatternLine> kAnsi31{
        {45.0, {0, 0}, {0, 0.125}, {}},
    };
    static const std::vector<HatchPatternLine> kAnsi32{
        {45.0, {0, 0}, {0, 0.375}, {}},
        {45.0, {0.176776695, 0}, {0, 0.375}, {}},
    };
    static const std::vector<HatchPatternLine> kAnsi33{
        {45.0, {0, 0}, {0, 0.25}, {}},
        {45.0, {0.176776695, 0}, {0, 0.25}, {0.125, -0.0625}},
    };
    static const std::vector<HatchPatternLine> kAnsi34{
        {45.0, {0, 0}, {0, 0.75}, {}},
    };
    static const std::vector<HatchPatternLine> kAnsi35{
        {45.0, {0, 0}, {0, 0.1875}, {0.125, -0.0625, 0.0, -0.0625}},
    };
    static const std::vector<HatchPatternLine> kAnsi36{
        {45.0, {0, 0}, {0, 0.09375}, {}},
        {45.0, {0.0883883476, 0}, {0, 0.09375}, {}},
    };
    static const std::vector<HatchPatternLine> kAnsi37{
        {45.0, {0, 0}, {0, 0.125}, {}},
        {135.0, {0, 0}, {0, 0.125}, {}},
    };
    static const std::vector<HatchPatternLine> kAnsi38{
        {45.0, {0, 0}, {0, 0.125}, {}},
        {135.0, {0, 0}, {0, 0.125}, {0.125, -0.0625}},
    };

    // --- ISO02W100-ISO15W100: the ISO 128 line-type table applied as
    // 45-degree section lining, all at nominal width 1.0 (scale 1 => "W100").
    static const std::vector<HatchPatternLine> kIso02{{45.0, {0, 0}, {0, 1.0}, {}}}; // continuous
    static const std::vector<HatchPatternLine> kIso03{{45.0, {0, 0}, {0, 1.0}, {0.5, -0.25}}}; // dashed
    static const std::vector<HatchPatternLine> kIso04{{45.0, {0, 0}, {0, 1.0}, {0.5, -0.5}}}; // dashed, spaced
    static const std::vector<HatchPatternLine> kIso05{
        {45.0, {0, 0}, {0, 1.0}, {0.5, -0.25, 0.0, -0.25}}}; // long-dash dotted
    static const std::vector<HatchPatternLine> kIso06{
        {45.0, {0, 0}, {0, 1.0}, {0.5, -0.25, 0.0, -0.125, 0.0, -0.25}}}; // long-dash double-dotted
    static const std::vector<HatchPatternLine> kIso07{{45.0, {0, 0}, {0, 1.0}, {0.0, -0.25}}}; // dotted
    static const std::vector<HatchPatternLine> kIso08{
        {45.0, {0, 0}, {0, 1.0}, {0.5, -0.25, 0.125, -0.25}}}; // long-dash short-dash
    static const std::vector<HatchPatternLine> kIso09{
        {45.0, {0, 0}, {0, 1.0}, {0.5, -0.25, 0.125, -0.125, 0.125, -0.25}}}; // long-dash double-short-dash
    static const std::vector<HatchPatternLine> kIso10{{45.0, {0, 0}, {0, 1.0}, {0.375, -0.125, 0.0, -0.125}}}; // dash dot
    static const std::vector<HatchPatternLine> kIso11{
        {45.0, {0, 0}, {0, 1.0}, {0.375, -0.125, 0.375, -0.125, 0.0, -0.125}}}; // double-dash dot
    static const std::vector<HatchPatternLine> kIso12{
        {45.0, {0, 0}, {0, 1.0}, {0.375, -0.125, 0.0, -0.125, 0.0, -0.125}}}; // dash double-dot
    static const std::vector<HatchPatternLine> kIso13{
        {45.0, {0, 0}, {0, 1.0}, {0.375, -0.125, 0.375, -0.125, 0.0, -0.125, 0.0, -0.125}}}; // double-dash double-dot
    static const std::vector<HatchPatternLine> kIso14{
        {45.0, {0, 0}, {0, 1.0}, {0.375, -0.125, 0.0, -0.0625, 0.0, -0.0625, 0.0, -0.125}}}; // dash triple-dot
    static const std::vector<HatchPatternLine> kIso15{
        {45.0, {0, 0}, {0, 1.0},
         {0.375, -0.125, 0.375, -0.125, 0.0, -0.0625, 0.0, -0.0625, 0.0, -0.125}}}; // double-dash triple-dot

    // --- Common material/texture symbols.
    static const std::vector<HatchPatternLine> kLine{{0.0, {0, 0}, {0, 0.125}, {}}};
    static const std::vector<HatchPatternLine> kDash{{0.0, {0, 0}, {0, 0.125}, {0.25, -0.125}}};
    static const std::vector<HatchPatternLine> kDots{
        {0.0, {0, 0}, {0, 0.125}, {0.0, -0.125}},
        {90.0, {0, 0}, {0, 0.125}, {0.0, -0.125}},
    };
    static const std::vector<HatchPatternLine> kCross{
        {0.0, {0, 0}, {0, 0.25}, {}},
        {90.0, {0, 0}, {0, 0.25}, {}},
    };
    static const std::vector<HatchPatternLine> kNet{
        {45.0, {0, 0}, {0, 0.125}, {}},
        {135.0, {0, 0}, {0, 0.125}, {}},
    };
    static const std::vector<HatchPatternLine> kNet3{
        {0.0, {0, 0}, {0, 0.1875}, {}},
        {60.0, {0, 0}, {0, 0.1875}, {}},
        {120.0, {0, 0}, {0, 0.1875}, {}},
    };
    static const std::vector<HatchPatternLine> kSquare{
        {0.0, {0, 0}, {0, 0.5}, {}},
        {90.0, {0, 0}, {0, 0.5}, {}},
    };
    // HEX/HONEY/TRIANG are all members of the same triangular-grid family
    // (three 60-degree-apart line families) at different spacing/density --
    // a hexagonal tiling is the dual of a triangular grid, so this is a
    // deliberate approximation of the honeycomb/hex-grid look, not a literal
    // hexagon outline tracer.
    static const std::vector<HatchPatternLine> kHex{
        {0.0, {0, 0}, {0, 0.375}, {}},
        {60.0, {0, 0}, {0, 0.375}, {}},
        {120.0, {0, 0}, {0, 0.375}, {}},
    };
    static const std::vector<HatchPatternLine> kHoney{
        {0.0, {0, 0}, {0, 0.25}, {}},
        {60.0, {0, 0}, {0, 0.25}, {}},
        {120.0, {0, 0}, {0, 0.25}, {}},
    };
    static const std::vector<HatchPatternLine> kTriang{
        {0.0, {0, 0}, {0, 0.125}, {}},
        {60.0, {0, 0}, {0, 0.125}, {}},
        {120.0, {0, 0}, {0, 0.125}, {}},
    };
    static const std::vector<HatchPatternLine> kZigzag{
        {45.0, {0, 0}, {0, 0.0625}, {0.0625, -0.0625}},
    };
    static const std::vector<HatchPatternLine> kAngle{
        {30.0, {0, 0}, {0, 0.25}, {}},
        {-30.0, {0, 0}, {0, 0.25}, {}},
    };
    // BRICK: horizontal coursing lines plus short vertical header-joint
    // ticks staggered half a brick per course.
    static const std::vector<HatchPatternLine> kBrick{
        {0.0, {0, 0}, {0, 0.25}, {}},
        {90.0, {0, 0}, {0.5, 0.25}, {0.125, -0.375}},
    };
    // GRASS: a small fan of short-dash families at closely spaced angles
    // simulating clustered grass tufts (deterministic, not random).
    static const std::vector<HatchPatternLine> kGrass{
        {60.0, {0, 0}, {0, 0.1875}, {0.0625, -0.1875}},
        {75.0, {0.05, 0}, {0, 0.1875}, {0.0625, -0.1875}},
        {90.0, {0.1, 0}, {0, 0.1875}, {0.0625, -0.1875}},
        {105.0, {0.15, 0}, {0, 0.1875}, {0.0625, -0.1875}},
        {120.0, {0.2, 0}, {0, 0.1875}, {0.0625, -0.1875}},
    };
    // GRAVEL: scattered stipple via dotted families at four angles.
    static const std::vector<HatchPatternLine> kGravel{
        {0.0, {0, 0}, {0, 0.1875}, {0.0, -0.1875}},
        {45.0, {0.05, 0}, {0, 0.1875}, {0.0, -0.1875}},
        {90.0, {0.1, 0}, {0, 0.1875}, {0.0, -0.1875}},
        {135.0, {0.15, 0}, {0, 0.1875}, {0.0, -0.1875}},
    };
    // STEEL: dense primary hatch crossed with a sparse secondary direction,
    // the structural-steel section symbol.
    static const std::vector<HatchPatternLine> kSteel{
        {45.0, {0, 0}, {0, 0.0625}, {}},
        {135.0, {0, 0}, {0, 0.5}, {}},
    };
    // SWAMP: a waterline plus short grass-tuft ticks along it.
    static const std::vector<HatchPatternLine> kSwamp{
        {0.0, {0, 0}, {0, 0.5}, {}},
        {90.0, {0, 0}, {0.25, 0.5}, {0.0625, -0.4375}},
    };
    // EARTH: two closely-divergent dashed families simulating rough,
    // disturbed-ground fill.
    static const std::vector<HatchPatternLine> kEarth{
        {0.0, {0, 0}, {0, 0.09375}, {0.1875, -0.09375}},
        {10.0, {0.03, 0}, {0, 0.09375}, {0.1875, -0.09375}},
    };

    switch (pattern) {
    case HatchPattern::Solid: return kNone;
    case HatchPattern::Ansi31: return kAnsi31;
    case HatchPattern::Ansi32: return kAnsi32;
    case HatchPattern::Ansi33: return kAnsi33;
    case HatchPattern::Ansi34: return kAnsi34;
    case HatchPattern::Ansi35: return kAnsi35;
    case HatchPattern::Ansi36: return kAnsi36;
    case HatchPattern::Ansi37: return kAnsi37;
    case HatchPattern::Ansi38: return kAnsi38;
    case HatchPattern::Iso02W100: return kIso02;
    case HatchPattern::Iso03W100: return kIso03;
    case HatchPattern::Iso04W100: return kIso04;
    case HatchPattern::Iso05W100: return kIso05;
    case HatchPattern::Iso06W100: return kIso06;
    case HatchPattern::Iso07W100: return kIso07;
    case HatchPattern::Iso08W100: return kIso08;
    case HatchPattern::Iso09W100: return kIso09;
    case HatchPattern::Iso10W100: return kIso10;
    case HatchPattern::Iso11W100: return kIso11;
    case HatchPattern::Iso12W100: return kIso12;
    case HatchPattern::Iso13W100: return kIso13;
    case HatchPattern::Iso14W100: return kIso14;
    case HatchPattern::Iso15W100: return kIso15;
    case HatchPattern::Line: return kLine;
    case HatchPattern::Dash: return kDash;
    case HatchPattern::Dots: return kDots;
    case HatchPattern::Cross: return kCross;
    case HatchPattern::Net: return kNet;
    case HatchPattern::Net3: return kNet3;
    case HatchPattern::Square: return kSquare;
    case HatchPattern::Hex: return kHex;
    case HatchPattern::Honey: return kHoney;
    case HatchPattern::Triang: return kTriang;
    case HatchPattern::Zigzag: return kZigzag;
    case HatchPattern::Angle: return kAngle;
    case HatchPattern::Brick: return kBrick;
    case HatchPattern::Grass: return kGrass;
    case HatchPattern::Gravel: return kGravel;
    case HatchPattern::Steel: return kSteel;
    case HatchPattern::Swamp: return kSwamp;
    case HatchPattern::Earth: return kEarth;
    }
    return kNone;
}

const std::vector<HatchPattern>& allHatchPatterns() {
    static const std::vector<HatchPattern> kAll{
        HatchPattern::Solid,     HatchPattern::Ansi31,    HatchPattern::Ansi32,    HatchPattern::Ansi33,
        HatchPattern::Ansi34,    HatchPattern::Ansi35,    HatchPattern::Ansi36,    HatchPattern::Ansi37,
        HatchPattern::Ansi38,    HatchPattern::Iso02W100, HatchPattern::Iso03W100, HatchPattern::Iso04W100,
        HatchPattern::Iso05W100, HatchPattern::Iso06W100, HatchPattern::Iso07W100, HatchPattern::Iso08W100,
        HatchPattern::Iso09W100, HatchPattern::Iso10W100, HatchPattern::Iso11W100, HatchPattern::Iso12W100,
        HatchPattern::Iso13W100, HatchPattern::Iso14W100, HatchPattern::Iso15W100, HatchPattern::Line,
        HatchPattern::Dash,      HatchPattern::Dots,      HatchPattern::Cross,     HatchPattern::Net,
        HatchPattern::Net3,      HatchPattern::Square,    HatchPattern::Hex,       HatchPattern::Honey,
        HatchPattern::Triang,    HatchPattern::Zigzag,    HatchPattern::Angle,     HatchPattern::Brick,
        HatchPattern::Grass,     HatchPattern::Gravel,    HatchPattern::Steel,     HatchPattern::Swamp,
        HatchPattern::Earth,
    };
    return kAll;
}

} // namespace lcad
