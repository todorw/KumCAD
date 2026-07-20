#include "core/io/DxfReader.h"

#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/PointCloud.h"
#include "core/io/PointCloudFile.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Table.h"
#include "core/geometry/Text.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/geometry/Wipeout.h"
#include "core/geometry/Wire.h"
#include "core/io/DxfColors.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace lcad {

namespace {

struct Group {
    int code;
    std::string value;
};

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool readGroups(const std::string& path, std::vector<Group>& out) {
    std::ifstream in(path);
    if (!in) return false;

    std::string codeLine;
    std::string valueLine;
    while (std::getline(in, codeLine)) {
        if (!std::getline(in, valueLine)) break;
        try {
            out.push_back({std::stoi(trim(codeLine)), trim(valueLine)});
        } catch (...) {
            // Malformed group-code line: skip it rather than aborting the whole read.
        }
    }
    return true;
}

Color colorFromTrueColor(int tc) {
    return Color{static_cast<std::uint8_t>((tc >> 16) & 0xFF), static_cast<std::uint8_t>((tc >> 8) & 0xFF),
                 static_cast<std::uint8_t>(tc & 0xFF)};
}

double toDouble(const std::string& s, double fallback = 0.0) {
    try {
        return std::stod(s);
    } catch (...) {
        return fallback;
    }
}

int toInt(const std::string& s, int fallback = 0) {
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

} // namespace

bool readDxf(Document& document, const std::string& path, std::string* errorOut) {
    std::vector<Group> groups;
    if (!readGroups(path, groups)) {
        if (errorOut) *errorOut = "Could not open file for reading";
        return false;
    }
    if (groups.empty()) {
        if (errorOut) *errorOut = "File is empty or not a valid DXF (no group codes found)";
        return false;
    }

    Document fresh; // build into a scratch document; only replace on full success
    std::unordered_map<std::string, LayerId> layerByName;
    layerByName["0"] = 0;

    enum class Section { None, Header, Tables, Blocks, Entities, Objects } section = Section::None;
    bool inLayerTable = false;
    bool inStyleTable = false;
    bool inDimStyleTable = false;

    std::string curHeaderVar;
    // Current-style names from the header, applied after the tables exist.
    std::string pendingTextStyle;
    std::string pendingDimStyle;
    bool haveGeo = false;
    GeoLocation pendingGeo;
    std::vector<LayerState> pendingLayerStates;
    std::vector<PlotStyle> pendingPlotStyles;
    std::vector<CtbEntry> pendingCtbEntries;
    // $KUMCAD_GROUP pseudo-vars (name + member ordinals), applied at the
    // very end once entityOrdinals below is complete -- see DxfWriter's
    // own comment on why members are stored by ordinal, not raw id.
    std::vector<std::pair<std::string, std::vector<int>>> pendingGroups;
    // The Nth entity actually added to fresh's own model space (NOT a
    // BLOCK/paper-space body -- see flushEntity's own hookup below)
    // lands at entityOrdinals[N], the exact same order
    // document.entities() iterates at write time.
    std::vector<EntityId> entityOrdinals;

    // STYLE table entry being accumulated.
    TextStyle curTextStyle;
    bool haveTextStyle = false;
    auto flushTextStyle = [&]() {
        if (!haveTextStyle) return;
        if (!curTextStyle.name.empty() && curTextStyle.name[0] != '*') {
            fresh.addOrUpdateTextStyle(curTextStyle);
        }
        curTextStyle = TextStyle{};
        haveTextStyle = false;
    };

    // DIMSTYLE table entry being accumulated.
    NamedDimStyle curDimStyle;
    bool haveDimStyle = false;
    auto flushDimStyle = [&]() {
        if (!haveDimStyle) return;
        if (!curDimStyle.name.empty()) fresh.addOrUpdateDimStyle(curDimStyle.name, curDimStyle.style);
        curDimStyle = NamedDimStyle{};
        haveDimStyle = false;
    };

    // LAYOUT objects (OBJECTS section) apply to layouts in order; AutoCAD
    // also writes one for Model, recognized by its name and skipped.
    int layoutObjectIndex = 0;
    bool inLayoutObject = false;

    std::string curLayerName;
    Color curLayerColor{255, 255, 255};
    bool curLayerHasTrueColor = false;
    bool curLayerVisible = true;
    bool curLayerLocked = false;
    bool curLayerFrozen = false;
    LineType curLayerLinetype = LineType::Continuous;
    double curLayerLineweight = 0.25;
    std::string curLayerPlotStyle;
    bool haveLayerName = false;

    auto flushLayer = [&]() {
        if (!haveLayerName) return;
        const auto existing = layerByName.find(curLayerName);
        LayerId id;
        if (existing == layerByName.end()) {
            id = fresh.addLayer(curLayerName, curLayerColor);
            layerByName[curLayerName] = id;
        } else {
            // Pre-created layer (layer "0"): adopt the file's properties.
            id = existing->second;
        }
        if (Layer* layer = fresh.findLayer(id)) {
            layer->color = curLayerColor;
            layer->visible = curLayerVisible;
            layer->locked = curLayerLocked;
            layer->frozen = curLayerFrozen;
            layer->linetype = curLayerLinetype;
            layer->lineweight = curLayerLineweight;
            layer->plotStyle = curLayerPlotStyle;
        }
        haveLayerName = false;
        curLayerColor = Color{255, 255, 255};
        curLayerHasTrueColor = false;
        curLayerVisible = true;
        curLayerLocked = false;
        curLayerFrozen = false;
        curLayerLinetype = LineType::Continuous;
        curLayerLineweight = 0.25;
        curLayerPlotStyle.clear();
    };

    // Block-definition state: while a BLOCK ... ENDBLK is open, flushed
    // entities land in curBlockEntities instead of the document.
    bool inBlockHeader = false;
    bool inBlockBody = false;
    std::string curBlockName;
    std::string curBlockXrefPath;
    Point2D curBlockBase;
    std::vector<double> curBlockDynamicValues; // simplified dynamic-block linear parameter, see DxfWriter
    std::vector<double> curBlockFlipValues;     // group 41-44, see DxfWriter
    std::vector<double> curBlockRotationValues; // group 45-47
    std::vector<double> curBlockArrayValues;    // group 48-52
    int curBlockArrayMinCount = 1;              // group 73
    std::vector<std::pair<std::string, double>> curBlockLookupPresets; // groups 4 + 53, zipped by adjacency
    std::string curBlockPendingLookupLabel;
    std::vector<std::string> curBlockVisibilityStates;             // group 5
    std::vector<std::vector<int>> curBlockVisibilityIndices;       // group 74, appended to the last state
    std::vector<Pin> curBlockPins; // groups 7/9/71/91-94, zipped per pin like the lookup presets above
    std::string curBlockPendingPinName;
    std::string curBlockPendingPinNumber;
    int curBlockPendingPinType = 0;
    double curBlockPendingPinPosX = 0.0;
    double curBlockPendingPinPosY = 0.0;
    double curBlockPendingPinStubX = 0.0;
    std::vector<Pad> curBlockPads; // groups 63/72/95-99, zipped per pad
    std::string curBlockPendingPadNumber;
    int curBlockPendingPadShape = 0;
    double curBlockPendingPadPosX = 0.0;
    double curBlockPendingPadPosY = 0.0;
    double curBlockPendingPadWidth = 0.0;
    double curBlockPendingPadHeight = 0.0;
    std::vector<std::unique_ptr<Entity>> curBlockEntities;
    int curPaperIndex = -1; // >= 0 while inside a *Paper_Space block

    // "*Paper_Space" is the first layout, "*Paper_SpaceN" the (N+2)-th.
    auto paperIndexOf = [](const std::string& name) -> int {
        const std::string prefix = "*Paper_Space";
        if (name.rfind(prefix, 0) != 0) return -1;
        const std::string suffix = name.substr(prefix.size());
        if (suffix.empty()) return 0;
        try {
            return std::stoi(suffix) + 1;
        } catch (...) {
            return -1;
        }
    };

    auto finalizeBlock = [&]() {
        curPaperIndex = -1;
        fresh.setActiveSpace(-1);
        // Pseudo-blocks (*Model_Space, *Paper_Space, anonymous) aren't user
        // block definitions; their contents were already routed elsewhere.
        if (curBlockName.empty() || curBlockName[0] == '*') {
            curBlockEntities.clear();
            curBlockName.clear();
            curBlockXrefPath.clear();
            curBlockBase = Point2D();
            curBlockDynamicValues.clear();
            curBlockFlipValues.clear();
            curBlockRotationValues.clear();
            curBlockArrayValues.clear();
            curBlockArrayMinCount = 1;
            curBlockLookupPresets.clear();
            curBlockPendingLookupLabel.clear();
            curBlockVisibilityStates.clear();
            curBlockVisibilityIndices.clear();
            curBlockPins.clear();
            curBlockPads.clear();
            return;
        }
        // Normalize child geometry to be base-point-relative.
        if (std::abs(curBlockBase.x) > 1e-12 || std::abs(curBlockBase.y) > 1e-12) {
            const Point2D shift(-curBlockBase.x, -curBlockBase.y);
            for (auto& e : curBlockEntities) e->translate(shift);
        }
        if (!fresh.findBlock(curBlockName)) {
            fresh.addBlock(curBlockName, std::move(curBlockEntities));
            // Xref block: remember the referenced file. The inline entities
            // (if any) are the cached snapshot; the app refreshes it from
            // disk after open when the file is reachable.
            if (!curBlockXrefPath.empty()) fresh.findBlock(curBlockName)->xrefPath = curBlockXrefPath;
            if (curBlockDynamicValues.size() == 8) {
                DynamicLinearParameter dp;
                dp.basePoint = Point2D(curBlockDynamicValues[0], curBlockDynamicValues[1]);
                dp.endPoint = Point2D(curBlockDynamicValues[2], curBlockDynamicValues[3]);
                dp.frameMin = Point2D(curBlockDynamicValues[4], curBlockDynamicValues[5]);
                dp.frameMax = Point2D(curBlockDynamicValues[6], curBlockDynamicValues[7]);
                fresh.findBlock(curBlockName)->dynamicParam = dp;
            }
            BlockDefinition* newBlock = fresh.findBlock(curBlockName);
            if (curBlockFlipValues.size() == 4) {
                newBlock->dynamicFlip = DynamicFlipParameter{Point2D(curBlockFlipValues[0], curBlockFlipValues[1]),
                                                              Point2D(curBlockFlipValues[2], curBlockFlipValues[3])};
            }
            if (curBlockRotationValues.size() == 3) {
                newBlock->dynamicRotation = DynamicRotationParameter{
                    Point2D(curBlockRotationValues[0], curBlockRotationValues[1]), curBlockRotationValues[2]};
            }
            if (curBlockArrayValues.size() == 5) {
                newBlock->dynamicArray =
                    DynamicArrayParameter{Point2D(curBlockArrayValues[0], curBlockArrayValues[1]),
                                          Point2D(curBlockArrayValues[2], curBlockArrayValues[3]),
                                          curBlockArrayValues[4], curBlockArrayMinCount};
            }
            if (!curBlockLookupPresets.empty()) {
                newBlock->dynamicLookup = DynamicLookupParameter{"Lookup1", curBlockLookupPresets};
            }
            if (!curBlockVisibilityStates.empty()) {
                DynamicVisibilityParameter vp;
                vp.states = curBlockVisibilityStates;
                for (std::size_t s = 0; s < curBlockVisibilityStates.size(); ++s) {
                    std::vector<EntityId> ids;
                    if (s < curBlockVisibilityIndices.size()) {
                        for (int idx : curBlockVisibilityIndices[s]) {
                            if (idx >= 0 && static_cast<std::size_t>(idx) < newBlock->entities.size()) {
                                ids.push_back(newBlock->entities[static_cast<std::size_t>(idx)]->id());
                            }
                        }
                    }
                    vp.visibleIds[curBlockVisibilityStates[s]] = std::move(ids);
                }
                newBlock->dynamicVisibility = vp;
            }
            newBlock->pins = curBlockPins;
            newBlock->pads = curBlockPads;
        }
        curBlockEntities.clear();
        curBlockName.clear();
        curBlockXrefPath.clear();
        curBlockBase = Point2D();
        curBlockFlipValues.clear();
        curBlockRotationValues.clear();
        curBlockArrayValues.clear();
        curBlockArrayMinCount = 1;
        curBlockLookupPresets.clear();
        curBlockPendingLookupLabel.clear();
        curBlockVisibilityStates.clear();
        curBlockVisibilityIndices.clear();
        curBlockDynamicValues.clear();
        curBlockPins.clear();
        curBlockPads.clear();
    };

    std::string curEntityType;
    std::string curLayerRef = "0";
    Point2D p10, p11, p13, p14, p15;
    int dimType = 32;
    double dimTextHeight = 2.5;
    double radius = 0.0;
    double ellipseRatio = 1.0;
    double startAngleDeg = 0.0;
    double endAngleDeg = 0.0;
    std::vector<Point2D> polyVerts;
    std::vector<double> polyBulges;
    double pendingVertX = 0.0;
    bool havePendingVertX = false;
    bool inVertex = false; // inside a VERTEX sub-entity of an old-style POLYLINE
    bool closed = false;
    double textHeight = 0.0;
    double textRotationDeg = 0.0;
    double textWidthFactor = 1.0; // TEXT group 41
    std::string textContent;
    double mtextWidth = 0.0;
    int mtextAttachment = 1;
    std::string mtextChunks; // accumulated group 3 chunks; group 1 appends the tail
    int hatchVertsExpected = 0;
    std::string hatchPatternNameStr = "SOLID";
    bool hatchSolid = true;
    double hatchAngleDeg = 0.0;
    double hatchScale = 1.0;
    std::optional<Color> hatchGradientColor2; // HATCH group 421 (simplified GRADIENT marker)
    std::string hatchGradientPresetName;      // HATCH group 470
    std::string insertName;
    double insertScale = 1.0;
    Point2D vpViewCenter; // VIEWPORT group 12/22
    double vpViewHeight = 0.0;
    double vpWidth = 0.0;
    double vpHeight = 0.0;
    int splineDegree = 3;
    std::vector<double> splineKnots;
    std::vector<Point2D> splineFit;
    double pendingFitX = 0.0;
    bool havePendingFitX = false;
    int entityAci = 0;             // per-entity color override, 0 = none seen
    bool entityHasTrueColor = false;
    Color entityTrueColor{255, 255, 255};
    std::string entityLinetypeName; // per-entity linetype override, empty = ByLayer
    std::string entityTextStyle;    // TEXT/MTEXT style reference (group 7)
    int entityLineweight = 0;       // per-entity 370 override, 0 = none seen
    std::string attTag;             // ATTDEF/ATTRIB group 2
    std::string attPrompt;          // ATTDEF group 3
    int tableRows = 0;               // ACAD_TABLE group 90
    int tableCols = 0;               // ACAD_TABLE group 91
    double tableTextHeight = 2.5;    // ACAD_TABLE group 40
    std::vector<double> tableRowHeights;
    std::vector<double> tableColWidths;
    std::vector<std::string> tableCells;
    std::string pointCloudPath;      // POINTCLOUD group 1
    std::string imagePath;           // IMAGE group 1
    double imageWidth = 0.0;         // IMAGE group 40
    double imageHeight = 0.0;        // IMAGE group 41
    double imageRotationDeg = 0.0;   // IMAGE group 50
    int imagePdfPage = 0;            // IMAGE group 71 (only meaningful for a .pdf path)
    double mleaderArrowSize = 1.25;  // MULTILEADER group 40
    std::vector<Point2D> mleaderPoints; // flat, split into legs via mleaderLegSizes
    std::vector<int> mleaderLegSizes;   // one entry per leg, in order (group 70)
    double pendingLegX = 0.0;
    bool havePendingLegX = false;
    std::string netLabelName;  // NETLABEL group 1
    double netLabelHeight = 0.0; // NETLABEL group 40
    double trackWidth = 0.0;      // TRACK group 40
    double viaDiameter = 0.0;     // VIA group 40
    double viaDrillDiameter = 0.0; // VIA group 41
    bool wipeoutShowFrame = true;  // WIPEOUT group 290
    // The INSERT most recently flushed, so following ATTRIB records can
    // attach their values to it. Cleared by any non-ATTRIB entity.
    InsertEntity* lastInsert = nullptr;

    auto flushEntity = [&]() {
        if (curEntityType.empty()) return;
        const auto it = layerByName.find(curLayerRef);
        const LayerId layerId = it != layerByName.end() ? it->second : 0;
        const EntityId id = fresh.reserveEntityId();
        std::unique_ptr<Entity> made;

        if (curEntityType == "LINE") {
            made = std::make_unique<LineEntity>(id, layerId, p10, p11);
        } else if (curEntityType == "CIRCLE") {
            made = std::make_unique<CircleEntity>(id, layerId, p10, radius);
        } else if (curEntityType == "ARC") {
            made = std::make_unique<ArcEntity>(id, layerId, p10, radius, startAngleDeg * M_PI / 180.0,
                                               endAngleDeg * M_PI / 180.0);
        } else if ((curEntityType == "LWPOLYLINE" || curEntityType == "POLYLINE") && polyVerts.size() >= 2) {
            made = std::make_unique<PolylineEntity>(id, layerId, polyVerts, polyBulges, closed);
        } else if (curEntityType == "ELLIPSE") {
            // p11 is the major axis endpoint relative to center; its direction
            // is the ellipse's rotation. An ellipse is symmetric under 180
            // degrees, so normalize the rotation into [0, pi), and prefer the
            // representation with rotation < 90 degrees (swapping which local
            // axis is "major") so axis-aligned ellipses come back with
            // rotation exactly 0.
            const double majorRadius = std::sqrt(p11.x * p11.x + p11.y * p11.y);
            const double minorRadius = majorRadius * ellipseRatio;
            double rotation = std::atan2(p11.y, p11.x);
            rotation = std::fmod(rotation, M_PI);
            if (rotation < 0) rotation += M_PI;
            double rx = majorRadius;
            double ry = minorRadius;
            if (rotation >= M_PI / 2) {
                rotation -= M_PI / 2;
                rx = minorRadius;
                ry = majorRadius;
            }
            made = std::make_unique<EllipseEntity>(id, layerId, p10, rx, ry, rotation);
        } else if (curEntityType == "TEXT" && !textContent.empty()) {
            auto text = std::make_unique<TextEntity>(id, layerId, p10, textContent, textHeight,
                                                     textRotationDeg * M_PI / 180.0);
            if (!entityTextStyle.empty()) text->setStyleName(entityTextStyle);
            text->setWidthFactor(textWidthFactor);
            made = std::move(text);
        } else if (curEntityType == "MTEXT") {
            const std::string content = decodeMTextContent(mtextChunks + textContent);
            if (!content.empty() && textHeight > 1e-12) {
                auto mtext = std::make_unique<MTextEntity>(id, layerId, p10, content, textHeight, mtextWidth,
                                                           textRotationDeg * M_PI / 180.0);
                // Group 10 is the attachment point (71); we store top-left,
                // so shift by the block extents for the other anchors.
                const int col = (mtextAttachment - 1) % 3; // 0 left, 1 center, 2 right
                const int row = (mtextAttachment - 1) / 3; // 0 top, 1 middle, 2 bottom
                if (col != 0 || row != 0) {
                    const double w = mtext->blockWidth();
                    const double h = mtext->blockHeight();
                    const Point2D shift(-w * col / 2.0, h * row / 2.0);
                    mtext->translate(rotateAround(shift, Point2D(), mtext->rotation()));
                }
                if (!entityTextStyle.empty()) mtext->setStyleName(entityTextStyle);
                made = std::move(mtext);
            }
        } else if (curEntityType == "DIMENSION") {
            std::unique_ptr<DimensionEntity> dim;
            switch (dimType & 7) { // low bits select the dimension kind
            case 3: { // diameter: 10 and 15 are opposite chord points
                const Point2D center = (p10 + p15) * 0.5;
                const Point2D textAt = p11.distanceTo(Point2D()) > 1e-12 ? p11 : p15;
                dim = std::make_unique<DimensionEntity>(id, layerId, DimensionKind::Diameter, center, p15, textAt);
                break;
            }
            case 4: { // radius: 10 = center, 15 = point on the curve
                const Point2D textAt = p11.distanceTo(Point2D()) > 1e-12 ? p11 : p15;
                dim = std::make_unique<DimensionEntity>(id, layerId, DimensionKind::Radius, p10, p15, textAt);
                break;
            }
            case 5: // 3-point angular: 13/14 = rays, 15 = vertex, 10 = arc point
                dim = std::make_unique<DimensionEntity>(id, layerId, DimensionKind::Angular, p13, p14, p10, p15);
                break;
            case 1: // aligned
                dim = std::make_unique<DimensionEntity>(id, layerId, p13, p14, p10, true);
                break;
            default: // rotated/linear (and kinds we don't model yet)
                dim = std::make_unique<DimensionEntity>(id, layerId, p13, p14, p10, false);
                break;
            }
            dim->setStyle(dimTextHeight, fresh.dimStyle().arrowSize, fresh.dimStyle().decimals);
            made = std::move(dim);
        } else if (curEntityType == "HATCH" && polyVerts.size() >= 3) {
            HatchPattern pattern = HatchPattern::Solid;
            if (!hatchSolid) {
                // Unknown pattern names degrade to ANSI31 so the region still
                // reads as hatched rather than silently going solid.
                pattern = hatchPatternFromName(hatchPatternNameStr).value_or(HatchPattern::Ansi31);
            }
            auto hatch = std::make_unique<HatchEntity>(id, layerId, polyVerts, pattern, hatchScale,
                                                       hatchAngleDeg * M_PI / 180.0);
            if (hatchGradientColor2) {
                hatch->setGradientColor2(*hatchGradientColor2);
                if (const auto preset = gradientPresetFromName(hatchGradientPresetName)) hatch->setGradientPreset(*preset);
            }
            made = std::move(hatch);
        } else if (curEntityType == "VIEWPORT") {
            // Only paper-space viewports (from *Paper_Space blocks) become
            // layout viewports; the model-space VIEWPORT some writers emit is
            // skipped.
            if (inBlockBody && curPaperIndex >= 0 && vpWidth > 1e-9 && vpHeight > 1e-9 && vpViewHeight > 1e-9 &&
                curPaperIndex < static_cast<int>(fresh.layouts().size())) {
                fresh.layouts()[curPaperIndex].viewports.push_back(
                    Viewport{p10, vpWidth, vpHeight, vpViewCenter, vpHeight / vpViewHeight});
            }
        } else if (curEntityType == "LEADER" && polyVerts.size() >= 2) {
            made = std::make_unique<LeaderEntity>(id, layerId, polyVerts, fresh.dimStyle().arrowSize);
        } else if (curEntityType == "SPLINE" && polyVerts.size() >= 2) {
            // Control points were collected into polyVerts. Prefer the exact
            // control-point + knot form; fall back to re-fitting from fit
            // points when the knot vector is inconsistent.
            const std::size_t expectedKnots = polyVerts.size() + splineDegree + 1;
            if (splineKnots.size() == expectedKnots) {
                made = std::make_unique<SplineEntity>(id, layerId, splineDegree, polyVerts, splineKnots, splineFit);
            } else if (splineFit.size() >= 2) {
                made = SplineEntity::fromFitPoints(id, layerId, splineFit);
            }
        } else if (curEntityType == "INSERT" && !insertName.empty()) {
            if (const BlockDefinition* block = fresh.findBlock(insertName)) {
                made = std::make_unique<InsertEntity>(id, layerId, block, p10, insertScale,
                                                      startAngleDeg * M_PI / 180.0);
            }
        } else if (curEntityType == "POINT") {
            made = std::make_unique<PointEntity>(id, layerId, p10);
        } else if (curEntityType == "XLINE" || curEntityType == "RAY") {
            if (p11.distanceTo(Point2D()) > 1e-12) {
                made = std::make_unique<ConstructionLineEntity>(id, layerId, p10, p11, curEntityType == "RAY");
            }
        } else if (curEntityType == "ATTDEF" && !attTag.empty()) {
            made = std::make_unique<AttDefEntity>(id, layerId, p10, attTag, attPrompt, textContent,
                                                  textHeight > 1e-9 ? textHeight : 2.5,
                                                  textRotationDeg * M_PI / 180.0);
        } else if (curEntityType == "ATTRIB") {
            // Attribute value for the preceding INSERT; position/height are
            // derived from the block's ATTDEF, so only tag+value matter.
            if (lastInsert && !attTag.empty()) lastInsert->setAttribute(attTag, textContent);
        } else if (curEntityType == "ACAD_TABLE" && tableRows > 0 && tableCols > 0 &&
                  static_cast<int>(tableRowHeights.size()) == tableRows &&
                  static_cast<int>(tableColWidths.size()) == tableCols) {
            tableCells.resize(static_cast<std::size_t>(tableRows) * tableCols);
            made = std::make_unique<TableEntity>(id, layerId, p10, tableRowHeights, tableColWidths, tableCells,
                                                 tableTextHeight);
        } else if (curEntityType == "POINTCLOUD" && !pointCloudPath.empty()) {
            made = std::make_unique<PointCloudEntity>(id, layerId, pointCloudPath, readPointCloudFile(pointCloudPath));
        } else if (curEntityType == "IMAGE" && !imagePath.empty() && imageWidth > 1e-9 && imageHeight > 1e-9) {
            made = std::make_unique<ImageEntity>(id, layerId, imagePath, p10, imageWidth, imageHeight,
                                                 imageRotationDeg * M_PI / 180.0, imagePdfPage);
        } else if (curEntityType == "MULTILEADER" && !mleaderLegSizes.empty()) {
            std::vector<std::vector<Point2D>> legs;
            std::size_t idx = 0;
            for (int sz : mleaderLegSizes) {
                std::vector<Point2D> leg;
                for (int i = 0; i < sz && idx < mleaderPoints.size(); ++i) leg.push_back(mleaderPoints[idx++]);
                legs.push_back(std::move(leg));
            }
            made = std::make_unique<MLeaderEntity>(id, layerId, legs, p10, mleaderArrowSize);
        } else if (curEntityType == "WIRE" && polyVerts.size() >= 2) {
            made = std::make_unique<WireEntity>(id, layerId, polyVerts);
        } else if (curEntityType == "JUNCTION") {
            made = std::make_unique<JunctionEntity>(id, layerId, p10);
        } else if (curEntityType == "NOCONNECT") {
            made = std::make_unique<NoConnectEntity>(id, layerId, p10);
        } else if (curEntityType == "NETLABEL" && !netLabelName.empty()) {
            made = std::make_unique<NetLabelEntity>(id, layerId, p10, netLabelName,
                                                    netLabelHeight > 1e-9 ? netLabelHeight : 2.5);
        } else if (curEntityType == "TRACK" && polyVerts.size() >= 2) {
            made = std::make_unique<TrackEntity>(id, layerId, polyVerts, trackWidth > 1e-9 ? trackWidth : 0.25);
        } else if (curEntityType == "VIA") {
            made = std::make_unique<ViaEntity>(id, layerId, p10, viaDiameter > 1e-9 ? viaDiameter : 0.6,
                                               viaDrillDiameter > 1e-9 ? viaDrillDiameter : 0.3);
        } else if (curEntityType == "WIPEOUT" && polyVerts.size() >= 3) {
            made = std::make_unique<WipeoutEntity>(id, layerId, polyVerts, wipeoutShowFrame);
        }

        const bool madeSomething = made != nullptr;
        if (made) {
            if (entityHasTrueColor) {
                made->setColorOverride(entityTrueColor);
            } else if (entityAci >= 1 && entityAci <= 255) {
                made->setColorOverride(aciToColor(entityAci));
            }
            // Unknown names (including BYLAYER/BYBLOCK) stay ByLayer.
            if (const auto linetype = lineTypeFromName(entityLinetypeName)) {
                made->setLinetypeOverride(*linetype);
            }
            if (entityLineweight > 0) made->setLineweightOverride(entityLineweight / 100.0);
            lastInsert = curEntityType == "INSERT" ? static_cast<InsertEntity*>(made.get()) : nullptr;
            if (inBlockBody && curPaperIndex < 0) {
                curBlockEntities.push_back(std::move(made));
            } else {
                // Model space, or directly into the paper block's layout via
                // the document's active space.
                if (!inBlockBody) entityOrdinals.push_back(id); // real model space only, matching document.entities()
                fresh.addEntity(std::move(made));
            }
        }

        // Anything that isn't an ATTRIB (or its SEQEND) ends the "ATTRIBs
        // attach to the previous INSERT" window.
        if (!madeSomething && curEntityType != "ATTRIB" && curEntityType != "SEQEND") lastInsert = nullptr;

        curEntityType.clear();
        curLayerRef = "0";
        p10 = Point2D();
        p11 = Point2D();
        p13 = Point2D();
        p14 = Point2D();
        p15 = Point2D();
        dimType = 32;
        dimTextHeight = 2.5;
        radius = 0.0;
        ellipseRatio = 1.0;
        startAngleDeg = 0.0;
        endAngleDeg = 0.0;
        polyVerts.clear();
        polyBulges.clear();
        havePendingVertX = false;
        inVertex = false;
        closed = false;
        textHeight = 0.0;
        textRotationDeg = 0.0;
        textWidthFactor = 1.0;
        textContent.clear();
        mtextWidth = 0.0;
        mtextAttachment = 1;
        mtextChunks.clear();
        hatchVertsExpected = 0;
        hatchPatternNameStr = "SOLID";
        hatchSolid = true;
        hatchAngleDeg = 0.0;
        hatchScale = 1.0;
        hatchGradientColor2.reset();
        hatchGradientPresetName.clear();
        insertName.clear();
        insertScale = 1.0;
        splineDegree = 3;
        splineKnots.clear();
        splineFit.clear();
        havePendingFitX = false;
        vpViewCenter = Point2D();
        vpViewHeight = 0.0;
        vpWidth = 0.0;
        vpHeight = 0.0;
        entityAci = 0;
        entityHasTrueColor = false;
        entityLinetypeName.clear();
        entityTextStyle.clear();
        entityLineweight = 0;
        attTag.clear();
        attPrompt.clear();
        tableRows = 0;
        tableCols = 0;
        tableTextHeight = 2.5;
        tableRowHeights.clear();
        tableColWidths.clear();
        tableCells.clear();
        pointCloudPath.clear();
        imagePath.clear();
        imageWidth = 0.0;
        imageHeight = 0.0;
        imageRotationDeg = 0.0;
        imagePdfPage = 0;
        mleaderArrowSize = 1.25;
        mleaderPoints.clear();
        mleaderLegSizes.clear();
        havePendingLegX = false;
        netLabelName.clear();
        netLabelHeight = 0.0;
        trackWidth = 0.0;
        viaDiameter = 0.0;
        viaDrillDiameter = 0.0;
        wipeoutShowFrame = true;
    };

    for (const Group& g : groups) {
        const bool entityContext = section == Section::Entities || (section == Section::Blocks && inBlockBody);

        if (g.code == 0) {
            if (section == Section::Tables && inLayerTable) flushLayer();
            if (section == Section::Tables && inStyleTable) flushTextStyle();
            if (section == Section::Tables && inDimStyleTable) flushDimStyle();
            if (section == Section::Objects) {
                if (g.value == "ENDSEC") {
                    section = Section::None;
                    inLayoutObject = false;
                    continue;
                }
                inLayoutObject = g.value == "LAYOUT";
                if (inLayoutObject) ++layoutObjectIndex;
                continue;
            }
            // Old-style POLYLINE carries its points as VERTEX sub-entities
            // terminated by SEQEND, so those two must not flush the pending
            // polyline the way a new top-level entity would.
            if (entityContext && g.value == "VERTEX" && curEntityType == "POLYLINE") {
                inVertex = true;
                continue;
            }
            if (entityContext) flushEntity();
            if (g.value == "SEQEND") continue;

            if (g.value == "SECTION") {
                section = Section::None; // determined by the group 2 that follows
            } else if (g.value == "ENDSEC") {
                if (section == Section::Blocks) finalizeBlock();
                section = Section::None;
                inLayerTable = false;
                inStyleTable = false;
                inDimStyleTable = false;
                inLayoutObject = false;
                inBlockHeader = false;
                inBlockBody = false;
            } else if (g.value == "ENDTAB") {
                inLayerTable = false;
                inStyleTable = false;
                inDimStyleTable = false;
            } else if (g.value == "LAYER" && section == Section::Tables) {
                inLayerTable = true;
                haveLayerName = false;
            } else if (g.value == "STYLE" && section == Section::Tables) {
                inStyleTable = true;
                inLayerTable = false;
                inDimStyleTable = false;
            } else if (g.value == "DIMSTYLE" && section == Section::Tables) {
                inDimStyleTable = true;
                inLayerTable = false;
                inStyleTable = false;
            } else if (section == Section::Blocks) {
                if (g.value == "BLOCK") {
                    finalizeBlock(); // tolerate a missing ENDBLK
                    inBlockHeader = true;
                    inBlockBody = false;
                } else if (g.value == "ENDBLK") {
                    finalizeBlock();
                    inBlockHeader = false;
                    inBlockBody = false;
                } else if (inBlockHeader || inBlockBody) {
                    inBlockHeader = false;
                    inBlockBody = true;
                    curEntityType = g.value;
                }
            } else if (section == Section::Entities) {
                curEntityType = g.value;
            }
            continue;
        }

        if (g.code == 2) {
            if (section == Section::None) {
                if (g.value == "HEADER") section = Section::Header;
                else if (g.value == "TABLES") section = Section::Tables;
                else if (g.value == "BLOCKS") section = Section::Blocks;
                else if (g.value == "ENTITIES") section = Section::Entities;
                else if (g.value == "OBJECTS") section = Section::Objects;
            } else if (section == Section::Header) {
                if (curHeaderVar == "$DIMSTYLE") pendingDimStyle = g.value;
            } else if (section == Section::Tables && inLayerTable) {
                curLayerName = g.value;
                haveLayerName = true;
            } else if (section == Section::Tables && inStyleTable) {
                curTextStyle = TextStyle{};
                curTextStyle.name = g.value;
                haveTextStyle = true;
            } else if (section == Section::Tables && inDimStyleTable) {
                curDimStyle = NamedDimStyle{};
                curDimStyle.name = g.value;
                haveDimStyle = true;
            } else if (section == Section::Blocks && inBlockHeader) {
                curBlockName = g.value;
                curPaperIndex = paperIndexOf(curBlockName);
                if (curPaperIndex >= 0) {
                    while (static_cast<int>(fresh.layouts().size()) <= curPaperIndex) {
                        Layout layout;
                        layout.name = "Layout" + std::to_string(fresh.layouts().size() + 1);
                        fresh.layouts().push_back(layout);
                    }
                    fresh.setActiveSpace(curPaperIndex);
                } else {
                    fresh.setActiveSpace(-1);
                }
            } else if (entityContext && curEntityType == "INSERT") {
                insertName = g.value;
            } else if (entityContext && curEntityType == "HATCH") {
                hatchPatternNameStr = g.value;
            } else if (entityContext && (curEntityType == "ATTDEF" || curEntityType == "ATTRIB")) {
                attTag = g.value;
            }
            continue;
        }

        if (section == Section::Objects) {
            if (inLayoutObject) {
                // AutoCAD also emits a LAYOUT for Model; ours are written in
                // layouts() order. Apply by name when it matches, else by
                // order, skipping the Model entry.
                if (g.code == 1) {
                    if (g.value == "Model") {
                        inLayoutObject = false;
                        --layoutObjectIndex;
                    } else {
                        const int idx = layoutObjectIndex - 1;
                        while (static_cast<int>(fresh.layouts().size()) <= idx) {
                            fresh.layouts().push_back(Layout{});
                        }
                        fresh.layouts()[idx].name = g.value;
                    }
                } else if (g.code == 44 || g.code == 45) {
                    const int idx = layoutObjectIndex - 1;
                    if (idx >= 0 && idx < static_cast<int>(fresh.layouts().size())) {
                        const double v = toDouble(g.value);
                        if (v > 1.0) {
                            if (g.code == 44) fresh.layouts()[idx].paperWidth = v;
                            else fresh.layouts()[idx].paperHeight = v;
                        }
                    }
                }
            }
            continue;
        }

        if (section == Section::Header) {
            if (g.code == 9) {
                curHeaderVar = g.value;
            } else if (g.code == 7 && curHeaderVar == "$TEXTSTYLE") {
                pendingTextStyle = g.value;
            } else if (g.code == 40 && curHeaderVar == "$LTSCALE") {
                fresh.setLineTypeScale(toDouble(g.value, 1.0));
            } else if (g.code == 40 && curHeaderVar == "$DIMTXT") {
                const double v = toDouble(g.value, 2.5);
                if (v > 1e-9) fresh.dimStyle().textHeight = v;
            } else if (g.code == 40 && curHeaderVar == "$DIMASZ") {
                const double v = toDouble(g.value, 1.25);
                if (v > 1e-9) fresh.dimStyle().arrowSize = v;
            } else if (g.code == 70 && curHeaderVar == "$DIMDEC") {
                fresh.dimStyle().decimals = std::clamp(toInt(g.value, 2), 0, 8);
            } else if (g.code == 70 && curHeaderVar == "$PDMODE") {
                fresh.setPointMode(toInt(g.value, 3));
            } else if (g.code == 40 && curHeaderVar == "$PDSIZE") {
                const double v = toDouble(g.value);
                if (v > 1e-9) fresh.setPointSize(v);
            } else if (g.code == 40 && curHeaderVar == "$KUMCAD_ANNOSCALE") {
                fresh.setAnnotationScale(toDouble(g.value, 1.0));
            } else if (curHeaderVar == "$KUMCAD_GEOLOCATION") {
                haveGeo = true;
                if (g.code == 10) pendingGeo.designPoint.x = toDouble(g.value);
                else if (g.code == 20) pendingGeo.designPoint.y = toDouble(g.value);
                else if (g.code == 40) pendingGeo.latitude = toDouble(g.value);
                else if (g.code == 41) pendingGeo.longitude = toDouble(g.value);
                else if (g.code == 50) pendingGeo.northRotation = toDouble(g.value) * M_PI / 180.0;
            } else if (curHeaderVar == "$KUMCAD_LAYERSTATE") {
                if (g.code == 1) {
                    pendingLayerStates.push_back(LayerState{});
                    pendingLayerStates.back().name = g.value;
                } else if (!pendingLayerStates.empty()) {
                    LayerState& st = pendingLayerStates.back();
                    if (g.code == 90) {
                        st.entries.push_back(LayerStateEntry{});
                        st.entries.back().layerId = static_cast<LayerId>(toInt(g.value));
                    } else if (!st.entries.empty()) {
                        LayerStateEntry& en = st.entries.back();
                        if (g.code == 290) en.visible = toInt(g.value) != 0;
                        else if (g.code == 280) en.locked = toInt(g.value) != 0;
                        else if (g.code == 420) en.color = colorFromTrueColor(toInt(g.value));
                        else if (g.code == 6) en.linetype = lineTypeFromName(g.value).value_or(LineType::Continuous);
                        else if (g.code == 40) en.lineweight = toDouble(g.value, 0.25);
                    }
                }
            } else if (curHeaderVar == "$KUMCAD_GROUP") {
                if (g.code == 1) {
                    pendingGroups.push_back({g.value, {}});
                } else if (g.code == 90 && !pendingGroups.empty()) {
                    pendingGroups.back().second.push_back(toInt(g.value));
                }
                // g.code == 70 (member count) is purely informational --
                // the actual member list is however many 90s follow, so
                // it doesn't need to be tracked separately here.
            } else if (curHeaderVar == "$KUMCAD_PLOTSTYLE") {
                if (g.code == 1) {
                    pendingPlotStyles.push_back(PlotStyle{});
                    pendingPlotStyles.back().name = g.value;
                } else if (!pendingPlotStyles.empty()) {
                    PlotStyle& ps = pendingPlotStyles.back();
                    if (g.code == 420) ps.color = colorFromTrueColor(toInt(g.value));
                    else if (g.code == 40) ps.lineweight = toDouble(g.value);
                    else if (g.code == 6) ps.linetype = lineTypeFromName(g.value);
                    else if (g.code == 141) ps.screening = std::clamp(toDouble(g.value, 100.0), 0.0, 100.0);
                }
            } else if (curHeaderVar == "$KUMCAD_PLOTMODE") {
                if (g.code == 70 && toInt(g.value) == 1) fresh.setPlotStyleMode(PlotStyleMode::ColorDependent);
            } else if (curHeaderVar == "$KUMCAD_CTBSTYLE") {
                if (g.code == 62) {
                    pendingCtbEntries.push_back(CtbEntry{});
                    pendingCtbEntries.back().aci = std::clamp(toInt(g.value, 1), 1, 255);
                } else if (!pendingCtbEntries.empty()) {
                    CtbEntry& ce = pendingCtbEntries.back();
                    if (g.code == 420) ce.color = colorFromTrueColor(toInt(g.value));
                    else if (g.code == 40) ce.lineweight = toDouble(g.value);
                    else if (g.code == 6) ce.linetype = lineTypeFromName(g.value);
                    else if (g.code == 141) ce.screening = std::clamp(toDouble(g.value, 100.0), 0.0, 100.0);
                }
            }
            continue;
        }

        if (section == Section::Tables && inStyleTable) {
            if (g.code == 3) curTextStyle.font = g.value;
            else if (g.code == 40) curTextStyle.fixedHeight = std::max(0.0, toDouble(g.value));
            else if (g.code == 41) curTextStyle.widthFactor = std::clamp(toDouble(g.value, 1.0), 0.1, 10.0);
            else if (g.code == 50) curTextStyle.obliqueDeg = toDouble(g.value);
            else if (g.code == 290) curTextStyle.annotative = toInt(g.value) != 0;
            continue;
        }

        if (section == Section::Tables && inDimStyleTable) {
            if (g.code == 140) {
                const double v = toDouble(g.value);
                if (v > 1e-9) curDimStyle.style.textHeight = v;
            } else if (g.code == 41) {
                const double v = toDouble(g.value);
                if (v > 1e-9) curDimStyle.style.arrowSize = v;
            } else if (g.code == 271) {
                curDimStyle.style.decimals = std::clamp(toInt(g.value, 2), 0, 8);
            }
            continue;
        }

        if (section == Section::Tables && inLayerTable) {
            if (g.code == 6) {
                curLayerLinetype = lineTypeFromName(g.value).value_or(LineType::Continuous);
            } else if (g.code == 420) {
                curLayerColor = colorFromTrueColor(toInt(g.value));
                curLayerHasTrueColor = true;
            } else if (g.code == 62) {
                const int aci = toInt(g.value, 7);
                curLayerVisible = aci >= 0; // negative ACI = layer off
                // Most files carry only the indexed color; a 420 true color
                // (before or after this group) always wins.
                if (!curLayerHasTrueColor) curLayerColor = aciToColor(std::abs(aci));
            } else if (g.code == 70) {
                const int flags = toInt(g.value);
                curLayerFrozen = (flags & 1) != 0; // bit 0 = frozen
                curLayerLocked = (flags & 4) != 0; // bit 2 = locked
            } else if (g.code == 370) {
                const int hundredths = toInt(g.value, 25);
                if (hundredths > 0) curLayerLineweight = hundredths / 100.0;
            } else if (g.code == 1) {
                curLayerPlotStyle = g.value; // simplified plot style name; see the writer for why
            }
            continue;
        }

        if (section == Section::Blocks && inBlockHeader) {
            if (g.code == 10) curBlockBase.x = toDouble(g.value);
            else if (g.code == 20) curBlockBase.y = toDouble(g.value);
            else if (g.code == 1) curBlockXrefPath = g.value; // xref file path
            else if (g.code == 40) curBlockDynamicValues.push_back(toDouble(g.value));
            else if (g.code >= 41 && g.code <= 44) curBlockFlipValues.push_back(toDouble(g.value));
            else if (g.code >= 45 && g.code <= 47) curBlockRotationValues.push_back(toDouble(g.value));
            else if (g.code >= 48 && g.code <= 52) curBlockArrayValues.push_back(toDouble(g.value));
            else if (g.code == 73) curBlockArrayMinCount = toInt(g.value, 1);
            else if (g.code == 4) curBlockPendingLookupLabel = g.value; // paired with the 53 that follows
            else if (g.code == 53) curBlockLookupPresets.emplace_back(curBlockPendingLookupLabel, toDouble(g.value));
            else if (g.code == 6) {
                curBlockVisibilityStates.push_back(g.value);
                curBlockVisibilityIndices.emplace_back();
            } else if (g.code == 74 && !curBlockVisibilityIndices.empty()) {
                curBlockVisibilityIndices.back().push_back(toInt(g.value));
            } else if (g.code == 7) {
                curBlockPendingPinName = g.value;
            } else if (g.code == 9) {
                curBlockPendingPinNumber = g.value;
            } else if (g.code == 71) {
                curBlockPendingPinType = toInt(g.value, 0);
            } else if (g.code == 91) {
                curBlockPendingPinPosX = toDouble(g.value);
            } else if (g.code == 92) {
                curBlockPendingPinPosY = toDouble(g.value);
            } else if (g.code == 93) {
                curBlockPendingPinStubX = toDouble(g.value);
            } else if (g.code == 94) {
                Pin pin;
                pin.name = curBlockPendingPinName;
                pin.number = curBlockPendingPinNumber;
                pin.electricalType = static_cast<PinElectricalType>(curBlockPendingPinType);
                pin.position = Point2D(curBlockPendingPinPosX, curBlockPendingPinPosY);
                pin.stubStart = Point2D(curBlockPendingPinStubX, toDouble(g.value));
                curBlockPins.push_back(pin);
            } else if (g.code == 63) {
                curBlockPendingPadNumber = g.value;
            } else if (g.code == 72) {
                curBlockPendingPadShape = toInt(g.value, 0);
            } else if (g.code == 95) {
                curBlockPendingPadPosX = toDouble(g.value);
            } else if (g.code == 96) {
                curBlockPendingPadPosY = toDouble(g.value);
            } else if (g.code == 97) {
                curBlockPendingPadWidth = toDouble(g.value);
            } else if (g.code == 98) {
                curBlockPendingPadHeight = toDouble(g.value);
            } else if (g.code == 99) {
                Pad pad;
                pad.number = curBlockPendingPadNumber;
                pad.shape = static_cast<PadShape>(curBlockPendingPadShape);
                pad.position = Point2D(curBlockPendingPadPosX, curBlockPendingPadPosY);
                pad.width = curBlockPendingPadWidth;
                pad.height = curBlockPendingPadHeight;
                pad.drillDiameter = toDouble(g.value);
                curBlockPads.push_back(pad);
            }
            continue;
        }

        if (!entityContext || curEntityType.empty()) continue;

        switch (g.code) {
        case 6:
            entityLinetypeName = g.value;
            break;
        case 7:
            if (curEntityType == "TEXT" || curEntityType == "MTEXT") entityTextStyle = g.value;
            break;
        case 8:
            curLayerRef = g.value;
            break;
        case 10:
            if (curEntityType == "LWPOLYLINE" || curEntityType == "SPLINE" || curEntityType == "LEADER" ||
                curEntityType == "WIRE" || curEntityType == "TRACK" || curEntityType == "WIPEOUT" || inVertex) {
                pendingVertX = toDouble(g.value);
                havePendingVertX = true;
            } else if (curEntityType == "HATCH") {
                if (hatchVertsExpected > 0) {
                    pendingVertX = toDouble(g.value);
                    havePendingVertX = true;
                }
            } else {
                p10.x = toDouble(g.value);
            }
            break;
        case 20:
            if (curEntityType == "LWPOLYLINE" || curEntityType == "SPLINE" || curEntityType == "LEADER" || inVertex ||
                curEntityType == "HATCH" || curEntityType == "WIRE" || curEntityType == "TRACK" ||
                curEntityType == "WIPEOUT") {
                if (havePendingVertX) {
                    polyVerts.emplace_back(pendingVertX, toDouble(g.value));
                    polyBulges.push_back(0.0); // a following group 42 may overwrite this
                    havePendingVertX = false;
                    if (curEntityType == "HATCH" && hatchVertsExpected > 0) --hatchVertsExpected;
                }
            } else {
                p10.y = toDouble(g.value);
            }
            break;
        case 11:
            if (curEntityType == "SPLINE") {
                pendingFitX = toDouble(g.value);
                havePendingFitX = true;
            } else if (curEntityType == "MULTILEADER") {
                pendingLegX = toDouble(g.value);
                havePendingLegX = true;
            } else {
                p11.x = toDouble(g.value);
            }
            break;
        case 21:
            if (curEntityType == "SPLINE") {
                if (havePendingFitX) {
                    splineFit.emplace_back(pendingFitX, toDouble(g.value));
                    havePendingFitX = false;
                }
            } else if (curEntityType == "MULTILEADER") {
                if (havePendingLegX) {
                    mleaderPoints.emplace_back(pendingLegX, toDouble(g.value));
                    havePendingLegX = false;
                }
            } else {
                p11.y = toDouble(g.value);
            }
            break;
        case 13:
            p13.x = toDouble(g.value);
            break;
        case 23:
            p13.y = toDouble(g.value);
            break;
        case 14:
            p14.x = toDouble(g.value);
            break;
        case 24:
            p14.y = toDouble(g.value);
            break;
        case 15:
            p15.x = toDouble(g.value);
            break;
        case 25:
            p15.y = toDouble(g.value);
            break;
        case 40:
            if (curEntityType == "ELLIPSE") ellipseRatio = toDouble(g.value, 1.0);
            else if (curEntityType == "TEXT" || curEntityType == "MTEXT" || curEntityType == "ATTDEF" ||
                     curEntityType == "ATTRIB") textHeight = toDouble(g.value);
            else if (curEntityType == "SPLINE") splineKnots.push_back(toDouble(g.value));
            else if (curEntityType == "VIEWPORT") vpWidth = toDouble(g.value);
            else if (curEntityType == "ACAD_TABLE") tableTextHeight = toDouble(g.value, 2.5);
            else if (curEntityType == "MULTILEADER") mleaderArrowSize = toDouble(g.value, 1.25);
            else if (curEntityType == "IMAGE") imageWidth = toDouble(g.value);
            else if (curEntityType == "NETLABEL") netLabelHeight = toDouble(g.value, 2.5);
            else if (curEntityType == "TRACK") trackWidth = toDouble(g.value, 0.25);
            else if (curEntityType == "VIA") viaDiameter = toDouble(g.value, 0.6);
            else radius = toDouble(g.value);
            break;
        case 41:
            if (curEntityType == "INSERT") insertScale = toDouble(g.value, 1.0);
            else if (curEntityType == "HATCH") hatchScale = toDouble(g.value, 1.0);
            else if (curEntityType == "MTEXT") mtextWidth = toDouble(g.value);
            else if (curEntityType == "VIEWPORT") vpHeight = toDouble(g.value);
            else if (curEntityType == "IMAGE") imageHeight = toDouble(g.value);
            else if (curEntityType == "VIA") viaDrillDiameter = toDouble(g.value, 0.3);
            else if (curEntityType == "TEXT") textWidthFactor = toDouble(g.value, 1.0);
            break;
        case 290:
            if (curEntityType == "WIPEOUT") wipeoutShowFrame = toInt(g.value, 1) != 0;
            break;
        case 12:
            if (curEntityType == "VIEWPORT") vpViewCenter.x = toDouble(g.value);
            break;
        case 22:
            if (curEntityType == "VIEWPORT") vpViewCenter.y = toDouble(g.value);
            break;
        case 45:
            if (curEntityType == "VIEWPORT") vpViewHeight = toDouble(g.value);
            break;
        case 42:
            // Segment bulge, on LWPOLYLINE vertices and old-style VERTEX
            // sub-entities only (42 means other things on ELLIPSE/INSERT/HATCH).
            if ((curEntityType == "LWPOLYLINE" || inVertex) && !polyBulges.empty()) {
                polyBulges.back() = toDouble(g.value);
            }
            break;
        case 50:
            if (curEntityType == "TEXT" || curEntityType == "MTEXT" || curEntityType == "ATTDEF" ||
                curEntityType == "ATTRIB") textRotationDeg = toDouble(g.value);
            else if (curEntityType == "IMAGE") imageRotationDeg = toDouble(g.value);
            else startAngleDeg = toDouble(g.value);
            break;
        case 51:
            endAngleDeg = toDouble(g.value);
            break;
        case 52:
            if (curEntityType == "HATCH") hatchAngleDeg = toDouble(g.value);
            break;
        case 62:
            entityAci = std::abs(toInt(g.value));
            break;
        case 370:
            entityLineweight = toInt(g.value);
            break;
        case 420:
            entityTrueColor = colorFromTrueColor(toInt(g.value));
            entityHasTrueColor = true;
            break;
        case 421:
            if (curEntityType == "HATCH") hatchGradientColor2 = colorFromTrueColor(toInt(g.value));
            break;
        case 470:
            if (curEntityType == "HATCH") hatchGradientPresetName = g.value;
            break;
        case 70:
            // Closed flag (bit 0) on the polyline header; a VERTEX's own 70
            // holds unrelated vertex flags, so skip it there.
            if (curEntityType == "LWPOLYLINE" || (curEntityType == "POLYLINE" && !inVertex)) {
                closed = (toInt(g.value) & 1) != 0;
            } else if (curEntityType == "DIMENSION") {
                dimType = toInt(g.value, 32);
            } else if (curEntityType == "HATCH") {
                hatchSolid = (toInt(g.value) & 1) != 0;
            } else if (curEntityType == "MULTILEADER") {
                mleaderLegSizes.push_back(toInt(g.value));
            }
            break;
        case 71:
            if (curEntityType == "SPLINE") splineDegree = std::max(1, toInt(g.value, 3));
            else if (curEntityType == "MTEXT") mtextAttachment = toInt(g.value, 1);
            else if (curEntityType == "IMAGE") imagePdfPage = toInt(g.value);
            break;
        case 93:
            if (curEntityType == "HATCH") hatchVertsExpected = toInt(g.value);
            break;
        case 98:
            if (curEntityType == "HATCH") hatchVertsExpected = 0; // seed points follow; stop collecting
            break;
        case 140:
            if (curEntityType == "DIMENSION") dimTextHeight = toDouble(g.value, 2.5);
            break;
        case 90:
            if (curEntityType == "ACAD_TABLE") tableRows = std::max(0, toInt(g.value));
            break;
        case 91:
            if (curEntityType == "ACAD_TABLE") tableCols = std::max(0, toInt(g.value));
            break;
        case 141:
            if (curEntityType == "ACAD_TABLE") tableRowHeights.push_back(std::max(0.1, toDouble(g.value, 1.0)));
            break;
        case 142:
            if (curEntityType == "ACAD_TABLE") tableColWidths.push_back(std::max(0.1, toDouble(g.value, 1.0)));
            break;
        case 1:
            if (curEntityType == "TEXT" || curEntityType == "MTEXT" || curEntityType == "ATTDEF" ||
                curEntityType == "ATTRIB") textContent = g.value;
            else if (curEntityType == "ACAD_TABLE") tableCells.push_back(g.value);
            else if (curEntityType == "IMAGE") imagePath = g.value;
            else if (curEntityType == "POINTCLOUD") pointCloudPath = g.value;
            else if (curEntityType == "NETLABEL") netLabelName = g.value;
            break;
        case 3:
            if (curEntityType == "MTEXT") mtextChunks += g.value;
            else if (curEntityType == "ATTDEF") attPrompt = g.value;
            break;
        default:
            break;
        }
    }

    // Tolerate a file missing its final ENDSEC/EOF by flushing whatever's pending.
    if (inLayerTable) flushLayer();
    if (inStyleTable) flushTextStyle();
    if (inDimStyleTable) flushDimStyle();
    flushEntity();
    finalizeBlock();
    fresh.setActiveSpace(-1);

    if (!pendingTextStyle.empty()) fresh.setCurrentTextStyle(pendingTextStyle);
    if (!pendingDimStyle.empty()) fresh.setCurrentDimStyle(pendingDimStyle);
    if (haveGeo) fresh.setGeoLocation(pendingGeo);
    for (LayerState& state : pendingLayerStates) fresh.saveLayerState(std::move(state));
    for (PlotStyle& style : pendingPlotStyles) fresh.savePlotStyle(std::move(style));
    for (const CtbEntry& entry : pendingCtbEntries) fresh.saveCtbEntry(entry);
    for (const auto& [name, ordinals] : pendingGroups) {
        std::vector<EntityId> members;
        for (int ordinal : ordinals) {
            // Out-of-range (e.g. hand-edited/truncated file) is skipped,
            // not an error -- same "dead members tolerated" policy
            // Document::groupOf itself documents.
            if (ordinal >= 0 && static_cast<std::size_t>(ordinal) < entityOrdinals.size()) {
                members.push_back(entityOrdinals[static_cast<std::size_t>(ordinal)]);
            }
        }
        if (!members.empty()) fresh.setGroup(name, std::move(members));
    }

    document = std::move(fresh);
    return true;
}

} // namespace lcad
