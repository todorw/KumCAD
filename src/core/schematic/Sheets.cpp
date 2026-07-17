#include "core/schematic/Sheets.h"

#include "core/document/Document.h"

namespace lcad {

namespace {
constexpr const char* kSheetPrefix = "SHEET:";
} // namespace

LayerId createSheet(Document& doc, const std::string& name) {
    return doc.addLayer(kSheetPrefix + name, Color{255, 255, 255});
}

std::vector<Sheet> listSheets(const Document& doc) {
    std::vector<Sheet> sheets;
    for (const Layer& layer : doc.layers()) {
        if (layer.name.rfind(kSheetPrefix, 0) == 0) {
            sheets.push_back({layer.id, layer.name.substr(std::string(kSheetPrefix).size())});
        }
    }
    return sheets;
}

bool goToSheet(Document& doc, const std::string& name) {
    const std::string targetLayerName = kSheetPrefix + name;
    bool found = false;
    for (const Layer& layer : doc.layers()) {
        if (layer.name == targetLayerName) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    for (const Layer& layer : doc.layers()) {
        if (layer.name.rfind(kSheetPrefix, 0) != 0) continue;
        Layer* mutableLayer = doc.findLayer(layer.id);
        mutableLayer->visible = (layer.name == targetLayerName);
    }
    return true;
}

} // namespace lcad
