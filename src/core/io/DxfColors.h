#pragma once

#include "core/document/Layer.h"

namespace lcad {

// AutoCAD Color Index (ACI) <-> RGB, for DXF group code 62. The wheel colors
// (10-249) use the standard hue/shade construction rather than a hardcoded
// 256-entry table; results match AutoCAD's palette closely but not bit-exactly.
// Indices outside 1-255 map to white.
Color aciToColor(int aci);

// Nearest ACI index (1-255) for an RGB color, by squared RGB distance.
int colorToAci(const Color& color);

} // namespace lcad
