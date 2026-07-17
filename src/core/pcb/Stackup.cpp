#include "core/pcb/Stackup.h"

#include "core/document/Document.h"

namespace lcad {

CopperStackup buildStackup(const Document& doc, const std::vector<std::string>& orderedLayerNames) {
    CopperStackup stackup;
    for (const std::string& name : orderedLayerNames) {
        for (const Layer& layer : doc.layers()) {
            if (layer.name == name) {
                stackup.layers.push_back(layer.id);
                break;
            }
        }
    }
    return stackup;
}

} // namespace lcad
