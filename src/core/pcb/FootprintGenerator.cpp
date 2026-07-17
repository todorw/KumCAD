#include "core/pcb/FootprintGenerator.h"

#include "core/document/Document.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

namespace lcad {

namespace {
void addLine(Document& doc, std::vector<std::unique_ptr<Entity>>& body, Point2D a, Point2D b) {
    body.push_back(std::make_unique<LineEntity>(doc.reserveEntityId(), LayerId(0), a, b));
}
} // namespace

bool generateGullWingFootprint(Document& doc, const std::string& name, const GullWingParams& params) {
    if (doc.findBlock(name)) return false;
    if (params.sideCount != 2 && params.sideCount != 4) return false;
    if (params.pinCount <= 0 || params.pinCount % params.sideCount != 0) return false;
    if (params.pitch <= 1e-9 || params.bodyWidth <= 1e-9 || params.bodyLength <= 1e-9 || params.padWidth <= 1e-9 ||
        params.padLength <= 1e-9) {
        return false;
    }

    const int perSide = params.pinCount / params.sideCount;
    std::vector<Pad> pads;
    pads.reserve(static_cast<std::size_t>(params.pinCount));
    int pinNumber = 1;

    // Pad center positions offset outward from the body edge by half the
    // pad's own perpendicular length, matching how a real gull-wing lead
    // actually lands just past the package body.
    const double leftRightX = params.bodyWidth / 2.0 + params.padLength / 2.0;
    const double topBottomY = params.bodyLength / 2.0 + params.padLength / 2.0;
    // Evenly spanning perSide pins centered on the side's own midline.
    const double span = static_cast<double>(perSide - 1) * params.pitch / 2.0;

    if (params.sideCount == 2) {
        // Left side: pin 1 at the top, going down. Right side: continues
        // counterclockwise, bottom to top -- standard SOIC/SOP numbering.
        for (int i = 0; i < perSide; ++i) {
            const double y = span - static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Rect, Point2D(-leftRightX, y), params.padLength,
                           params.padWidth, 0.0});
        }
        for (int i = 0; i < perSide; ++i) {
            const double y = -span + static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Rect, Point2D(leftRightX, y), params.padLength,
                           params.padWidth, 0.0});
        }
    } else {
        // QFP numbering, counterclockwise from the top-left corner: left
        // (top to bottom), bottom (left to right), right (bottom to
        // top), top (right to left).
        for (int i = 0; i < perSide; ++i) {
            const double y = span - static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Rect, Point2D(-leftRightX, y), params.padLength,
                           params.padWidth, 0.0});
        }
        for (int i = 0; i < perSide; ++i) {
            const double x = -span + static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Rect, Point2D(x, -topBottomY), params.padWidth,
                           params.padLength, 0.0});
        }
        for (int i = 0; i < perSide; ++i) {
            const double y = -span + static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Rect, Point2D(leftRightX, y), params.padLength,
                           params.padWidth, 0.0});
        }
        for (int i = 0; i < perSide; ++i) {
            const double x = span - static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Rect, Point2D(x, topBottomY), params.padWidth,
                           params.padLength, 0.0});
        }
    }

    std::vector<std::unique_ptr<Entity>> body;
    const double hw = params.bodyWidth / 2.0, hl = params.bodyLength / 2.0;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0), std::vector<Point2D>{Point2D(-hw, -hl), Point2D(hw, -hl), Point2D(hw, hl), Point2D(-hw, hl)},
        true));
    // Pin-1 marker: a short diagonal chamfer line at the top-left corner,
    // the standard real-footprint convention for indicating orientation.
    const double marker = std::min(hw, hl) * 0.2;
    addLine(doc, body, Point2D(-hw, hl - marker), Point2D(-hw + marker, hl));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

bool generatePinHeaderFootprint(Document& doc, const std::string& name, const PinHeaderParams& params) {
    if (doc.findBlock(name)) return false;
    if (params.rowCount != 1 && params.rowCount != 2) return false;
    if (params.pinCount <= 0 || params.pitch <= 1e-9) return false;

    std::vector<Pad> pads;
    pads.reserve(static_cast<std::size_t>(params.pinCount * params.rowCount));
    int pinNumber = 1;
    for (int row = 0; row < params.rowCount; ++row) {
        const double x = params.rowCount == 1 ? 0.0 : (row == 0 ? -params.pitch / 2.0 : params.pitch / 2.0);
        for (int i = 0; i < params.pinCount; ++i) {
            const double y = static_cast<double>(i) * params.pitch;
            pads.push_back({std::to_string(pinNumber++), PadShape::Round, Point2D(x, y), 1.7, 1.7, 1.0});
        }
    }

    std::vector<std::unique_ptr<Entity>> body;
    const double outlineMargin = 1.5;
    const double maxY = static_cast<double>(params.pinCount - 1) * params.pitch;
    const double halfWidth = (params.rowCount == 1 ? 0.0 : params.pitch / 2.0) + outlineMargin;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0),
        std::vector<Point2D>{Point2D(-halfWidth, -outlineMargin), Point2D(halfWidth, -outlineMargin),
                            Point2D(halfWidth, maxY + outlineMargin), Point2D(-halfWidth, maxY + outlineMargin)},
        true));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

} // namespace lcad
