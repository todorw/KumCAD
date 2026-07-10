#include "core/io/DxfReader.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Text.h"
#include "core/io/DxfColors.h"

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

    enum class Section { None, Header, Tables, Blocks, Entities } section = Section::None;
    bool inLayerTable = false;

    std::string curHeaderVar;

    std::string curLayerName;
    Color curLayerColor{255, 255, 255};
    bool curLayerHasTrueColor = false;
    bool curLayerVisible = true;
    bool curLayerLocked = false;
    LineType curLayerLinetype = LineType::Continuous;
    bool haveLayerName = false;

    auto flushLayer = [&]() {
        if (!haveLayerName) return;
        if (layerByName.find(curLayerName) == layerByName.end()) {
            const LayerId id = fresh.addLayer(curLayerName, curLayerColor);
            layerByName[curLayerName] = id;
            if (Layer* layer = fresh.findLayer(id)) {
                layer->visible = curLayerVisible;
                layer->locked = curLayerLocked;
                layer->linetype = curLayerLinetype;
            }
        }
        haveLayerName = false;
        curLayerColor = Color{255, 255, 255};
        curLayerHasTrueColor = false;
        curLayerVisible = true;
        curLayerLocked = false;
        curLayerLinetype = LineType::Continuous;
    };

    // Block-definition state: while a BLOCK ... ENDBLK is open, flushed
    // entities land in curBlockEntities instead of the document.
    bool inBlockHeader = false;
    bool inBlockBody = false;
    std::string curBlockName;
    Point2D curBlockBase;
    std::vector<std::unique_ptr<Entity>> curBlockEntities;

    auto finalizeBlock = [&]() {
        if (curBlockName.empty()) {
            curBlockEntities.clear();
            return;
        }
        // Normalize child geometry to be base-point-relative.
        if (std::abs(curBlockBase.x) > 1e-12 || std::abs(curBlockBase.y) > 1e-12) {
            const Point2D shift(-curBlockBase.x, -curBlockBase.y);
            for (auto& e : curBlockEntities) e->translate(shift);
        }
        if (!fresh.findBlock(curBlockName)) fresh.addBlock(curBlockName, std::move(curBlockEntities));
        curBlockEntities.clear();
        curBlockName.clear();
        curBlockBase = Point2D();
    };

    std::string curEntityType;
    std::string curLayerRef = "0";
    Point2D p10, p11, p13, p14;
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
    std::string textContent;
    int hatchVertsExpected = 0;
    std::string insertName;
    double insertScale = 1.0;
    int splineDegree = 3;
    std::vector<double> splineKnots;
    std::vector<Point2D> splineFit;
    double pendingFitX = 0.0;
    bool havePendingFitX = false;
    int entityAci = 0;             // per-entity color override, 0 = none seen
    bool entityHasTrueColor = false;
    Color entityTrueColor{255, 255, 255};
    std::string entityLinetypeName; // per-entity linetype override, empty = ByLayer

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
            made = std::make_unique<TextEntity>(id, layerId, p10, textContent, textHeight,
                                                textRotationDeg * M_PI / 180.0);
        } else if (curEntityType == "DIMENSION") {
            const bool aligned = (dimType & 7) == 1; // low bits: 0 = rotated/linear, 1 = aligned
            made = std::make_unique<DimensionEntity>(id, layerId, p13, p14, p10, aligned, dimTextHeight);
        } else if (curEntityType == "HATCH" && polyVerts.size() >= 3) {
            made = std::make_unique<HatchEntity>(id, layerId, polyVerts);
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
        }

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
            if (inBlockBody) {
                curBlockEntities.push_back(std::move(made));
            } else {
                fresh.addEntity(std::move(made));
            }
        }

        curEntityType.clear();
        curLayerRef = "0";
        p10 = Point2D();
        p11 = Point2D();
        p13 = Point2D();
        p14 = Point2D();
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
        textContent.clear();
        hatchVertsExpected = 0;
        insertName.clear();
        insertScale = 1.0;
        splineDegree = 3;
        splineKnots.clear();
        splineFit.clear();
        havePendingFitX = false;
        entityAci = 0;
        entityHasTrueColor = false;
        entityLinetypeName.clear();
    };

    for (const Group& g : groups) {
        const bool entityContext = section == Section::Entities || (section == Section::Blocks && inBlockBody);

        if (g.code == 0) {
            if (section == Section::Tables && inLayerTable) flushLayer();
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
                inBlockHeader = false;
                inBlockBody = false;
            } else if (g.value == "ENDTAB") {
                inLayerTable = false;
            } else if (g.value == "LAYER" && section == Section::Tables) {
                inLayerTable = true;
                haveLayerName = false;
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
            } else if (section == Section::Tables && inLayerTable) {
                curLayerName = g.value;
                haveLayerName = true;
            } else if (section == Section::Blocks && inBlockHeader) {
                curBlockName = g.value;
            } else if (entityContext && curEntityType == "INSERT") {
                insertName = g.value;
            }
            continue;
        }

        if (section == Section::Header) {
            if (g.code == 9) {
                curHeaderVar = g.value;
            } else if (g.code == 40 && curHeaderVar == "$LTSCALE") {
                fresh.setLineTypeScale(toDouble(g.value, 1.0));
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
                curLayerLocked = (toInt(g.value) & 4) != 0; // bit 2 = locked/frozen
            }
            continue;
        }

        if (section == Section::Blocks && inBlockHeader) {
            if (g.code == 10) curBlockBase.x = toDouble(g.value);
            else if (g.code == 20) curBlockBase.y = toDouble(g.value);
            continue;
        }

        if (!entityContext || curEntityType.empty()) continue;

        switch (g.code) {
        case 6:
            entityLinetypeName = g.value;
            break;
        case 8:
            curLayerRef = g.value;
            break;
        case 10:
            if (curEntityType == "LWPOLYLINE" || curEntityType == "SPLINE" || inVertex) {
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
            if (curEntityType == "LWPOLYLINE" || curEntityType == "SPLINE" || inVertex || curEntityType == "HATCH") {
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
        case 40:
            if (curEntityType == "ELLIPSE") ellipseRatio = toDouble(g.value, 1.0);
            else if (curEntityType == "TEXT") textHeight = toDouble(g.value);
            else if (curEntityType == "SPLINE") splineKnots.push_back(toDouble(g.value));
            else radius = toDouble(g.value);
            break;
        case 41:
            if (curEntityType == "INSERT") insertScale = toDouble(g.value, 1.0);
            break;
        case 42:
            // Segment bulge, on LWPOLYLINE vertices and old-style VERTEX
            // sub-entities only (42 means other things on ELLIPSE/INSERT/HATCH).
            if ((curEntityType == "LWPOLYLINE" || inVertex) && !polyBulges.empty()) {
                polyBulges.back() = toDouble(g.value);
            }
            break;
        case 50:
            if (curEntityType == "TEXT") textRotationDeg = toDouble(g.value);
            else startAngleDeg = toDouble(g.value);
            break;
        case 51:
            endAngleDeg = toDouble(g.value);
            break;
        case 62:
            entityAci = std::abs(toInt(g.value));
            break;
        case 420:
            entityTrueColor = colorFromTrueColor(toInt(g.value));
            entityHasTrueColor = true;
            break;
        case 70:
            // Closed flag (bit 0) on the polyline header; a VERTEX's own 70
            // holds unrelated vertex flags, so skip it there.
            if (curEntityType == "LWPOLYLINE" || (curEntityType == "POLYLINE" && !inVertex)) {
                closed = (toInt(g.value) & 1) != 0;
            } else if (curEntityType == "DIMENSION") {
                dimType = toInt(g.value, 32);
            }
            break;
        case 71:
            if (curEntityType == "SPLINE") splineDegree = std::max(1, toInt(g.value, 3));
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
        case 1:
            if (curEntityType == "TEXT") textContent = g.value;
            break;
        default:
            break;
        }
    }

    // Tolerate a file missing its final ENDSEC/EOF by flushing whatever's pending.
    if (inLayerTable) flushLayer();
    flushEntity();
    finalizeBlock();

    document = std::move(fresh);
    return true;
}

} // namespace lcad
