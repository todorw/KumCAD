#include "core/io/DxfReader.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <cmath>
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

    enum class Section { None, Header, Tables, Entities } section = Section::None;
    bool inLayerTable = false;

    std::string curLayerName;
    Color curLayerColor{255, 255, 255};
    bool curLayerVisible = true;
    bool curLayerLocked = false;
    bool haveLayerName = false;

    auto flushLayer = [&]() {
        if (!haveLayerName) return;
        if (layerByName.find(curLayerName) == layerByName.end()) {
            const LayerId id = fresh.addLayer(curLayerName, curLayerColor);
            layerByName[curLayerName] = id;
            if (Layer* layer = fresh.findLayer(id)) {
                layer->visible = curLayerVisible;
                layer->locked = curLayerLocked;
            }
        }
        haveLayerName = false;
        curLayerColor = Color{255, 255, 255};
        curLayerVisible = true;
        curLayerLocked = false;
    };

    std::string curEntityType;
    std::string curLayerRef = "0";
    Point2D p10, p11;
    double radius = 0.0;
    double startAngleDeg = 0.0;
    double endAngleDeg = 0.0;
    std::vector<Point2D> polyVerts;
    double pendingVertX = 0.0;
    bool havePendingVertX = false;
    bool closed = false;

    auto flushEntity = [&]() {
        if (curEntityType.empty()) return;
        const auto it = layerByName.find(curLayerRef);
        const LayerId layerId = it != layerByName.end() ? it->second : 0;
        const EntityId id = fresh.reserveEntityId();
        if (curEntityType == "LINE") {
            fresh.addEntity(std::make_unique<LineEntity>(id, layerId, p10, p11));
        } else if (curEntityType == "CIRCLE") {
            fresh.addEntity(std::make_unique<CircleEntity>(id, layerId, p10, radius));
        } else if (curEntityType == "ARC") {
            fresh.addEntity(std::make_unique<ArcEntity>(id, layerId, p10, radius, startAngleDeg * M_PI / 180.0,
                                                          endAngleDeg * M_PI / 180.0));
        } else if ((curEntityType == "LWPOLYLINE" || curEntityType == "POLYLINE") && polyVerts.size() >= 2) {
            fresh.addEntity(std::make_unique<PolylineEntity>(id, layerId, polyVerts, closed));
        }

        curEntityType.clear();
        curLayerRef = "0";
        p10 = Point2D();
        p11 = Point2D();
        radius = 0.0;
        startAngleDeg = 0.0;
        endAngleDeg = 0.0;
        polyVerts.clear();
        havePendingVertX = false;
        closed = false;
    };

    for (const Group& g : groups) {
        if (g.code == 0) {
            if (section == Section::Tables && inLayerTable) flushLayer();
            if (section == Section::Entities) flushEntity();

            if (g.value == "SECTION") {
                section = Section::None; // determined by the group 2 that follows
            } else if (g.value == "ENDSEC") {
                section = Section::None;
                inLayerTable = false;
            } else if (g.value == "ENDTAB") {
                inLayerTable = false;
            } else if (g.value == "LAYER" && section == Section::Tables) {
                inLayerTable = true;
                haveLayerName = false;
            } else if (section == Section::Entities) {
                curEntityType = g.value;
            }
            continue;
        }

        if (g.code == 2) {
            if (section == Section::None) {
                if (g.value == "HEADER") section = Section::Header;
                else if (g.value == "TABLES") section = Section::Tables;
                else if (g.value == "ENTITIES") section = Section::Entities;
            } else if (section == Section::Tables && inLayerTable) {
                curLayerName = g.value;
                haveLayerName = true;
            }
            continue;
        }

        if (section == Section::Tables && inLayerTable) {
            if (g.code == 420) {
                curLayerColor = colorFromTrueColor(toInt(g.value));
            } else if (g.code == 62) {
                curLayerVisible = toInt(g.value, 7) >= 0; // negative ACI = layer off
            } else if (g.code == 70) {
                curLayerLocked = (toInt(g.value) & 4) != 0; // bit 2 = locked/frozen
            }
            continue;
        }

        if (section != Section::Entities || curEntityType.empty()) continue;

        switch (g.code) {
        case 8:
            curLayerRef = g.value;
            break;
        case 10:
            if (curEntityType == "LWPOLYLINE") {
                pendingVertX = toDouble(g.value);
                havePendingVertX = true;
            } else {
                p10.x = toDouble(g.value);
            }
            break;
        case 20:
            if (curEntityType == "LWPOLYLINE") {
                if (havePendingVertX) {
                    polyVerts.emplace_back(pendingVertX, toDouble(g.value));
                    havePendingVertX = false;
                }
            } else {
                p10.y = toDouble(g.value);
            }
            break;
        case 11:
            p11.x = toDouble(g.value);
            break;
        case 21:
            p11.y = toDouble(g.value);
            break;
        case 40:
            radius = toDouble(g.value);
            break;
        case 50:
            startAngleDeg = toDouble(g.value);
            break;
        case 51:
            endAngleDeg = toDouble(g.value);
            break;
        case 70:
            if (curEntityType == "LWPOLYLINE") closed = (toInt(g.value) & 1) != 0;
            break;
        default:
            break;
        }
    }

    // Tolerate a file missing its final ENDSEC/EOF by flushing whatever's pending.
    if (inLayerTable) flushLayer();
    flushEntity();

    document = std::move(fresh);
    return true;
}

} // namespace lcad
