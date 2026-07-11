#include "core/io/Xref.h"

#include "core/io/DwgReader.h"
#include "core/io/DxfReader.h"

#include <filesystem>

namespace lcad {

namespace {

// Reads path into a scratch document and lifts its model-space entities into
// a flat list usable as block geometry: ids come from the target document,
// layers flatten to 0 with the source layer's color baked in as an override
// so the reference keeps its look.
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

    for (Entity* e : external.entities()) {
        const Layer* layer = external.findLayer(e->layer());
        if (layer && !layer->visible) continue;
        std::unique_ptr<Entity> copy = e->clone();
        copy->setId(target.reserveEntityId());
        if (!copy->colorOverride() && layer) copy->setColorOverride(layer->color);
        copy->setLayer(0);
        out.push_back(std::move(copy));
    }
    return true;
}

std::string xrefNameFor(const std::string& path) {
    return std::filesystem::path(path).stem().string();
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
