#pragma once

#include "core/document/Document.h"
#include "core/geometry/Point2D.h"
#include "core/geometry/Text.h"

#include <optional>
#include <string>
#include <vector>

namespace lcad {

// Core logic for a batch of AutoCAD Express Tools (TXT2MTXT, TCOUNT,
// TORIENT, BREAKLINE) and the layer tools (LAYMRG/LAYDEL/COPYTOLAYER
// helpers). Pure functions over entities/documents so each tool's actual
// behavior is unit-testable; the interactive prompting lives in the app's
// CommandDispatcher like every other command.

// TXT2MTXT: combine single-line TEXT entities into one MTEXT. Reading
// order is top-to-bottom then left-to-right (y descending, then x
// ascending), lines joined with '\n' (MTextEntity's own line separator).
// The result anchors at the first (topmost) text's position and takes its
// height/rotation. Real TXT2MTXT also offers word-wrap re-flow; this keeps
// one source text per line.
struct CombinedText {
    Point2D position;
    double height = 2.5;
    double rotation = 0.0;
    std::string text;
};
std::optional<CombinedText> combineTextEntities(const std::vector<const TextEntity*>& texts);

// TCOUNT: number a set of text strings. Numbering is assigned in
// top-to-bottom, left-to-right order of each item's position (same sort as
// TXT2MTXT), but results come back in INPUT order so the caller can zip
// them with its entity ids. Prefix/Suffix separate the number from the
// original text with a single space (disclosed simplification: real TCOUNT
// concatenates directly and offers a find-and-replace mode too).
enum class TCountPlacement { Prefix, Suffix, Replace };
struct TCountItem {
    Point2D position;
    std::string text;
};
std::vector<std::string> applyTcount(const std::vector<TCountItem>& items, int start, int increment,
                                     TCountPlacement placement);

// TORIENT: the rotation a text should take to read right-side-up while
// keeping its baseline direction: angles pointing into the "upside down"
// half-plane (past +/-90 degrees) are flipped by pi. Result is in
// (-pi/2, pi/2].
double torientRotation(double rotationRadians);

// BREAKLINE: polyline vertices for a break line from a to b with the
// standard zigzag symbol at the midpoint; size controls the symbol's
// extent along and across the line. Returns empty if a==b or size wouldn't
// fit (segment shorter than 2*size).
std::vector<Point2D> breaklinePoints(const Point2D& a, const Point2D& b, double size);

// Layer tools. Every entity id on the given layer across model space and
// every layout's paper space (block-definition children are not included:
// they belong to the definition, not a space -- same scoping PURGE's
// "layer is empty" check uses in reverse).
std::vector<EntityId> entityIdsOnLayer(const Document& doc, LayerId layer);
const Layer* findLayerByName(const Document& doc, const std::string& name);

} // namespace lcad
