#include "core/document/LineType.h"

#include <algorithm>
#include <cctype>

namespace lcad {

const char* lineTypeName(LineType type) {
    switch (type) {
    case LineType::Continuous: return "CONTINUOUS";
    case LineType::Dashed: return "DASHED";
    case LineType::Dot: return "DOT";
    case LineType::DashDot: return "DASHDOT";
    case LineType::Center: return "CENTER";
    case LineType::Hidden: return "HIDDEN";
    case LineType::Phantom: return "PHANTOM";
    }
    return "CONTINUOUS";
}

std::optional<LineType> lineTypeFromName(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (LineType type : allLineTypes()) {
        if (upper == lineTypeName(type)) return type;
    }
    // AutoCAD's placeholder for "inherit from layer"; treat as unknown here --
    // readers handle ByLayer by simply not setting an override.
    return std::nullopt;
}

const std::vector<double>& lineTypePattern(LineType type) {
    // Element values from acad.lin at LTSCALE 1.
    static const std::vector<double> kContinuous{};
    static const std::vector<double> kDashed{0.5, -0.25};
    static const std::vector<double> kDot{0.0, -0.25};
    static const std::vector<double> kDashDot{0.5, -0.25, 0.0, -0.25};
    static const std::vector<double> kCenter{1.25, -0.25, 0.25, -0.25};
    static const std::vector<double> kHidden{0.25, -0.125};
    static const std::vector<double> kPhantom{1.25, -0.25, 0.25, -0.25, 0.25, -0.25};
    switch (type) {
    case LineType::Continuous: return kContinuous;
    case LineType::Dashed: return kDashed;
    case LineType::Dot: return kDot;
    case LineType::DashDot: return kDashDot;
    case LineType::Center: return kCenter;
    case LineType::Hidden: return kHidden;
    case LineType::Phantom: return kPhantom;
    }
    return kContinuous;
}

const std::vector<LineType>& allLineTypes() {
    static const std::vector<LineType> kAll{
        LineType::Continuous, LineType::Dashed, LineType::Dot,    LineType::DashDot,
        LineType::Center,     LineType::Hidden, LineType::Phantom,
    };
    return kAll;
}

} // namespace lcad
