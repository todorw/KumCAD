#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"

#include <optional>
#include <vector>

// AutoCAD HATCH/GRADIENT's "pick internal point" boundary detection: gathers
// candidate boundary segments from every Line/Polyline/Arc/Circle in the
// document's current space (visible, unlocked layers only; arcs and circles
// are tessellated), then traces the tightest loop enclosing pt. Shared by
// HatchCommand and GradientCommand so both support pick-point in addition to
// pre-selecting a closed polyline.
std::optional<std::vector<lcad::Point2D>> pickHatchBoundary(lcad::Document& document, const lcad::Point2D& pt);
