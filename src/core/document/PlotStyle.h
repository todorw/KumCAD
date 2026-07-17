#pragma once

#include "core/Color.h"
#include "core/document/LineType.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace lcad {

// AutoCAD Plot Style Table (STB-style, simplified): a named style that can
// override an object's plotted color/lineweight/linetype independent of its
// on-screen appearance. Assigned per-layer (Layer::plotStyle names one of
// Document::plotStyles() by name; empty means "plot as displayed", i.e.
// AutoCAD's "Normal" style). Real AutoCAD ships plot styles as an external,
// binary .stb/.ctb file edited only through its own Plot Style Editor and
// referenced per-layout; this keeps the table inside the drawing itself
// instead, the same simplification this codebase already made for other
// "manager" data (LAYERSTATE, DATALINK) -- no color-dependent (CTB) table,
// no screening/dithering/line-end-style controls, no per-layout table
// selection (one table applies to the whole document).
struct PlotStyle {
    std::string name;
    std::optional<Color> color;
    std::optional<double> lineweight; // mm
    std::optional<LineType> linetype;
    // Ink percentage, 100 = full intensity. Real plotters screen by dot
    // density; a screened plot on white paper reads as the color blended
    // toward white, which is exactly how it's applied here (see
    // applyScreening) rather than as a separate alpha channel the print
    // pipeline would have to understand.
    double screening = 100.0;
};

// One row of a CTB-style color-dependent plot style table: overrides keyed
// by AutoCAD Color Index (1-255) of the entity's *displayed* color, the way
// a real .ctb maps "everything drawn in color 1 plots with pen X". Coexists
// with the named (STB) table above; Document::plotStyleMode() selects which
// one plotAppearance() consults -- AutoCAD likewise makes a drawing either
// color-dependent or named, never both at once.
struct CtbEntry {
    int aci = 1; // 1-255
    std::optional<Color> color;
    std::optional<double> lineweight; // mm
    std::optional<LineType> linetype;
    double screening = 100.0;
};

enum class PlotStyleMode {
    Named,         // layers reference PlotStyle by name (STB behavior)
    ColorDependent // displayed color's ACI picks a CtbEntry (CTB behavior)
};

// Screening blends the plotted color toward paper white by ink percentage.
inline Color applyScreening(const Color& c, double screening) {
    const double f = std::clamp(screening, 0.0, 100.0) / 100.0;
    auto mix = [f](unsigned char v) {
        return static_cast<unsigned char>(std::lround(255.0 - (255.0 - v) * f));
    };
    return Color{mix(c.r), mix(c.g), mix(c.b)};
}

// The color/lineweight/linetype an entity actually plots with: layer, then
// entity override, then (if the layer has one assigned) the named plot
// style's overrides -- matching AutoCAD's own precedence, where a plot
// style can repaint an object regardless of its ByLayer/ByObject color.
struct PlotAppearance {
    Color color;
    double lineweight = 0.25; // mm
    LineType linetype = LineType::Continuous;
};

} // namespace lcad
