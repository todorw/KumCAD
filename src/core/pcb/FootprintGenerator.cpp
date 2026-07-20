#include "core/pcb/FootprintGenerator.h"

#include "core/document/Document.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <cmath>

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

bool generateChipPassiveFootprint(Document& doc, const std::string& name, const ChipPassiveParams& params) {
    if (doc.findBlock(name)) return false;
    if (params.padWidth <= 1e-9 || params.padLength <= 1e-9 || params.padSpacing <= 1e-9 || params.bodyWidth <= 1e-9 ||
        params.bodyLength <= 1e-9) {
        return false;
    }

    const double padCenterX = params.padSpacing / 2.0 + params.padLength / 2.0;
    std::vector<Pad> pads = {
        {"1", PadShape::Rect, Point2D(-padCenterX, 0.0), params.padLength, params.padWidth, 0.0},
        {"2", PadShape::Rect, Point2D(padCenterX, 0.0), params.padLength, params.padWidth, 0.0},
    };

    std::vector<std::unique_ptr<Entity>> body;
    const double hw = params.bodyWidth / 2.0, hl = params.bodyLength / 2.0;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0), std::vector<Point2D>{Point2D(-hl, -hw), Point2D(hl, -hw), Point2D(hl, hw), Point2D(-hl, hw)},
        true));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

bool generateSot23Footprint(Document& doc, const std::string& name, const SotParams& params) {
    if (doc.findBlock(name)) return false;
    if (params.pitch <= 1e-9 || params.padWidth <= 1e-9 || params.padLength <= 1e-9 || params.bodyWidth <= 1e-9 ||
        params.bodyLength <= 1e-9) {
        return false;
    }

    const double bottomY = params.bodyLength / 2.0;
    std::vector<Pad> pads = {
        {"1", PadShape::Rect, Point2D(-params.pitch / 2.0, -bottomY), params.padWidth, params.padLength, 0.0},
        {"2", PadShape::Rect, Point2D(params.pitch / 2.0, -bottomY), params.padWidth, params.padLength, 0.0},
        {"3", PadShape::Rect, Point2D(0.0, bottomY), params.padWidth, params.padLength, 0.0},
    };

    std::vector<std::unique_ptr<Entity>> body;
    const double hw = params.bodyWidth / 2.0, hl = params.bodyLength / 2.0;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0), std::vector<Point2D>{Point2D(-hw, -hl), Point2D(hw, -hl), Point2D(hw, hl), Point2D(-hw, hl)},
        true));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

bool generateSot223Footprint(Document& doc, const std::string& name, const Sot223Params& params) {
    if (doc.findBlock(name)) return false;
    if (params.pitch <= 1e-9 || params.padWidth <= 1e-9 || params.padLength <= 1e-9 || params.bodyWidth <= 1e-9 ||
        params.bodyLength <= 1e-9 || params.tabPadLength <= 1e-9) {
        return false;
    }

    const double hl = params.bodyLength / 2.0;
    std::vector<Pad> pads = {
        {"1", PadShape::Rect, Point2D(-params.pitch, -hl), params.padWidth, params.padLength, 0.0},
        {"2", PadShape::Rect, Point2D(0.0, -hl), params.padWidth, params.padLength, 0.0},
        {"3", PadShape::Rect, Point2D(params.pitch, -hl), params.padWidth, params.padLength, 0.0},
        // Pin 4: the wide heatsink/collector tab spanning the opposite side.
        {"4", PadShape::Rect, Point2D(0.0, hl), params.bodyWidth * 0.8, params.tabPadLength, 0.0},
    };

    std::vector<std::unique_ptr<Entity>> body;
    const double hw = params.bodyWidth / 2.0;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0), std::vector<Point2D>{Point2D(-hw, -hl), Point2D(hw, -hl), Point2D(hw, hl), Point2D(-hw, hl)},
        true));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

namespace {
// JEDEC's own BGA row-letter convention: A, B, C, ... skipping I, O, Q,
// S, X, Z (avoiding letters easily confused with digits/other letters).
// Wrapping into AA/AB/... for grids past 20 rows is a real JEDEC
// extension this doesn't implement (see BgaParams' own comment).
std::string jedecRowLetter(int rowIndex) {
    static const std::string kLetters = "ABCDEFGHJKLMNPRTUVWY"; // I,O,Q,S,X,Z skipped
    if (rowIndex < 0 || rowIndex >= static_cast<int>(kLetters.size())) return "?";
    return std::string(1, kLetters[static_cast<std::size_t>(rowIndex)]);
}
} // namespace

bool generateBgaFootprint(Document& doc, const std::string& name, const BgaParams& params) {
    if (doc.findBlock(name)) return false;
    if (params.rows <= 0 || params.cols <= 0 || params.pitch <= 1e-9 || params.ballDiameter <= 1e-9 ||
        params.bodyWidth <= 1e-9 || params.bodyLength <= 1e-9) {
        return false;
    }
    static const std::string kLetters = "ABCDEFGHJKLMNPRTUVWY";
    if (params.rows > static_cast<int>(kLetters.size())) return false;

    std::vector<Pad> pads;
    pads.reserve(static_cast<std::size_t>(params.rows * params.cols));
    const double rowSpan = static_cast<double>(params.rows - 1) * params.pitch / 2.0;
    const double colSpan = static_cast<double>(params.cols - 1) * params.pitch / 2.0;
    for (int r = 0; r < params.rows; ++r) {
        const double y = rowSpan - static_cast<double>(r) * params.pitch;
        for (int c = 0; c < params.cols; ++c) {
            const double x = -colSpan + static_cast<double>(c) * params.pitch;
            pads.push_back({jedecRowLetter(r) + std::to_string(c + 1), PadShape::Round, Point2D(x, y),
                           params.ballDiameter, params.ballDiameter, 0.0});
        }
    }

    std::vector<std::unique_ptr<Entity>> body;
    const double hw = params.bodyWidth / 2.0, hl = params.bodyLength / 2.0;
    body.push_back(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), LayerId(0), std::vector<Point2D>{Point2D(-hw, -hl), Point2D(hw, -hl), Point2D(hw, hl), Point2D(-hw, hl)},
        true));
    // Pin-A1 marker: same diagonal-chamfer convention generateGullWingFootprint uses.
    const double marker = std::min(hw, hl) * 0.2;
    addLine(doc, body, Point2D(-hw, hl - marker), Point2D(-hw + marker, hl));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

bool generateMountingHoleFootprint(Document& doc, const std::string& name, double drillDiameter, double padDiameter) {
    if (doc.findBlock(name)) return false;
    if (drillDiameter <= 1e-9 || padDiameter <= 1e-9) return false;

    // No pad number: a mounting hole isn't part of any net, matching
    // KiCad's own "MountingHole" library footprints.
    std::vector<Pad> pads = {{"", PadShape::Round, Point2D(0.0, 0.0), padDiameter, padDiameter, drillDiameter}};

    std::vector<std::unique_ptr<Entity>> body;
    const double r = std::max(drillDiameter, padDiameter) / 2.0 + 0.5;
    // A circular silkscreen outline, approximated as a many-sided
    // polyline the same way this codebase's other round-body markers do
    // (no dedicated circle-on-a-block-body concept here).
    std::vector<Point2D> circle;
    constexpr int kSegments = 32;
    for (int s = 0; s < kSegments; ++s) {
        const double t = 2.0 * M_PI * s / kSegments;
        circle.push_back(Point2D(r * std::cos(t), r * std::sin(t)));
    }
    body.push_back(std::make_unique<PolylineEntity>(doc.reserveEntityId(), LayerId(0), circle, true));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

bool generateFiducialFootprint(Document& doc, const std::string& name, double padDiameter) {
    if (doc.findBlock(name)) return false;
    if (padDiameter <= 1e-9) return false;

    std::vector<Pad> pads = {{"", PadShape::Round, Point2D(0.0, 0.0), padDiameter, padDiameter, 0.0}};

    std::vector<std::unique_ptr<Entity>> body;
    const double r = padDiameter; // silkscreen courtyard ring, larger than the copper pad itself
    std::vector<Point2D> circle;
    constexpr int kSegments = 32;
    for (int s = 0; s < kSegments; ++s) {
        const double t = 2.0 * M_PI * s / kSegments;
        circle.push_back(Point2D(r * std::cos(t), r * std::sin(t)));
    }
    body.push_back(std::make_unique<PolylineEntity>(doc.reserveEntityId(), LayerId(0), circle, true));

    doc.addBlock(name, std::move(body));
    if (BlockDefinition* block = doc.findBlock(name)) block->pads = std::move(pads);
    return true;
}

} // namespace lcad
