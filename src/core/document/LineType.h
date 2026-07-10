#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lcad {

// The standard AutoCAD linetypes KumCAD ships with (acad.lin subset).
// Continuous is the implicit default everywhere.
enum class LineType {
    Continuous,
    Dashed,
    Dot,
    DashDot,
    Center,
    Hidden,
    Phantom,
};

// Canonical DXF/AutoCAD name, e.g. "DASHED".
const char* lineTypeName(LineType type);

// Case-insensitive lookup; nullopt for unknown names (callers usually fall
// back to Continuous, matching how AutoCAD substitutes missing linetypes).
std::optional<LineType> lineTypeFromName(const std::string& name);

// AutoCAD .lin pattern elements at scale 1, in drawing units: positive =
// dash, negative = gap, zero = dot. Empty for Continuous (solid).
const std::vector<double>& lineTypePattern(LineType type);

// All linetypes, for UI lists and the DXF LTYPE table.
const std::vector<LineType>& allLineTypes();

} // namespace lcad
