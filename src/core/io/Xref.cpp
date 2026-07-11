#include "core/io/Xref.h"

#include "core/io/DwgReader.h"
#include "core/io/DxfReader.h"

#include <filesystem>
#include <unordered_map>

namespace lcad {

namespace {

std::string xrefNameFor(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

// Finds (or creates) the target document's layer standing in for one of the
// xref's own layers, named "<xrefName>|<sourceLayerName>" like AutoCAD's own
// xref-bound layers, carrying the source layer's color/linetype/lineweight.
// Reused by name across attach/reload, so repeated reloads don't pile up
// duplicate layers. Visibility/lock start at the default (on, unlocked) so
// the host drawing controls them independently of the source file.
LayerId ensureXrefLayer(Document& target, const std::string& xrefName, const Layer& sourceLayer) {
    const std::string name = xrefName + "|" + sourceLayer.name;
    for (const Layer& l : target.layers()) {
        if (l.name == name) return l.id;
    }
    const LayerId id = target.addLayer(name, sourceLayer.color);
    if (Layer* layer = target.findLayer(id)) {
        layer->linetype = sourceLayer.linetype;
        layer->lineweight = sourceLayer.lineweight;
    }
    return id;
}

// Reads path into a scratch document and lifts its model-space entities into
// a flat list usable as block geometry: ids come from the target document,
// and each entity keeps its own layer's look via an xref-bound layer in the
// target document (see ensureXrefLayer) instead of flattening to layer 0.
bool loadSnapshot(Document& target, const std::string& path, std::vector<std::unique_ptr<Entity>>& out,
                  std::string* errorOut) {
    Document external;
    bool ok = false;
    if (path.size() > 4 && (path.substr(path.size() - 4) == ".dwg" || path.substr(path.size() - 4) == ".DWG")) {
        ok = readDwg(external, path, errorOut);
    } else {
        ok = readDxf(external, path, errorOut);
    }
    if (!ok) return false;

    const std::string xrefName = xrefNameFor(path);
    std::unordered_map<LayerId, LayerId> layerMap; // source layer id -> target layer id
    for (Entity* e : external.entities()) {
        const Layer* layer = external.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        std::unique_ptr<Entity> copy = e->clone();
        copy->setId(target.reserveEntityId());
        LayerId targetLayer = 0;
        if (layer) {
            auto it = layerMap.find(layer->id);
            if (it == layerMap.end()) {
                targetLayer = ensureXrefLayer(target, xrefName, *layer);
                layerMap.emplace(layer->id, targetLayer);
            } else {
                targetLayer = it->second;
            }
        }
        copy->setLayer(targetLayer);
        out.push_back(std::move(copy));
    }
    return true;
}

} // namespace

const BlockDefinition* attachXref(Document& document, const std::string& path, std::string* errorOut) {
    std::vector<std::unique_ptr<Entity>> snapshot;
    if (!loadSnapshot(document, path, snapshot, errorOut)) return nullptr;
    if (snapshot.empty()) {
        if (errorOut) *errorOut = "The referenced file has no supported entities";
        return nullptr;
    }

    const std::string name = xrefNameFor(path);
    if (BlockDefinition* existing = document.findBlock(name)) {
        if (!existing->isXref()) {
            if (errorOut) *errorOut = "A regular block named \"" + name + "\" already exists";
            return nullptr;
        }
        existing->entities = std::move(snapshot);
        existing->xrefPath = path;
        return existing;
    }

    const BlockDefinition* made = document.addBlock(name, std::move(snapshot));
    document.findBlock(name)->xrefPath = path;
    return made;
}

bool reloadXref(Document& document, const std::string& blockName, std::string* errorOut) {
    BlockDefinition* block = document.findBlock(blockName);
    if (!block || !block->isXref()) {
        if (errorOut) *errorOut = "\"" + blockName + "\" is not an attached xref";
        return false;
    }
    std::vector<std::unique_ptr<Entity>> snapshot;
    if (!loadSnapshot(document, block->xrefPath, snapshot, errorOut)) return false;
    block->entities = std::move(snapshot);
    return true;
}

int reloadAllXrefs(Document& document, const std::string& baseDir) {
    int refreshed = 0;
    for (const auto& blockPtr : document.blocks()) {
        BlockDefinition* block = document.findBlock(blockPtr->name);
        if (!block || !block->isXref()) continue;
        std::filesystem::path path(block->xrefPath);
        if (path.is_relative() && !baseDir.empty()) path = std::filesystem::path(baseDir) / path;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        std::vector<std::unique_ptr<Entity>> snapshot;
        if (!loadSnapshot(document, path.string(), snapshot, nullptr)) continue;
        block->entities = std::move(snapshot);
        ++refreshed;
    }
    return refreshed;
}

} // namespace lcad
