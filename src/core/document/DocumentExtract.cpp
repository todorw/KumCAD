#include "core/document/DocumentExtract.h"

#include "core/document/Block.h"
#include "core/geometry/Insert.h"

#include <unordered_map>

namespace lcad {

namespace {

std::unique_ptr<Entity> remapEntity(const Entity& e, Document& dest,
                                    const std::unordered_map<LayerId, LayerId>& layerMap) {
    const auto remapLayer = [&](LayerId id) {
        const auto it = layerMap.find(id);
        return it != layerMap.end() ? it->second : dest.currentLayer();
    };

    if (e.type() == EntityType::Insert) {
        const auto& insert = static_cast<const InsertEntity&>(e);
        // Every block is already shelled into dest by name before any
        // entity gets remapped (see extractSubset), so this always
        // resolves -- including a block nested inside another block.
        const BlockDefinition* newBlock = insert.block() ? dest.findBlock(insert.block()->name) : nullptr;
        auto copy = std::make_unique<InsertEntity>(dest.reserveEntityId(), remapLayer(insert.layer()), newBlock,
                                                    insert.position(), insert.scaleFactor(), insert.rotation());
        for (const auto& [tag, value] : insert.attributes()) copy->setAttribute(tag, value);
        copy->setDynamicStretch(insert.dynamicStretch());
        copy->setDynamicFlipped(insert.dynamicFlipped());
        copy->setDynamicRotationAngle(insert.dynamicRotationAngle());
        copy->setDynamicArrayCount(insert.dynamicArrayCount());
        copy->setVisibilityState(insert.visibilityState());
        copy->setLookupValue(insert.lookupValue());
        return copy;
    }

    auto copy = e.clone();
    copy->setId(dest.reserveEntityId());
    copy->setLayer(remapLayer(e.layer()));
    return copy;
}

} // namespace

Document extractSubset(const Document& source, const std::vector<EntityId>& ids) {
    Document dest;

    std::unordered_map<LayerId, LayerId> layerMap;
    for (const Layer& srcLayer : source.layers()) {
        const LayerId newId = dest.addLayer(srcLayer.name, srcLayer.color);
        if (Layer* destLayer = dest.findLayer(newId)) {
            destLayer->linetype = srcLayer.linetype;
            destLayer->lineweight = srcLayer.lineweight;
            destLayer->plotStyle = srcLayer.plotStyle;
        }
        layerMap[srcLayer.id] = newId;
    }

    // Shell every block first (empty), so a same-named block is always
    // resolvable by the time any entity (including one nested inside
    // another block) gets remapped below, regardless of processing order.
    for (const auto& block : source.blocks()) dest.addBlock(block->name, {});

    for (const auto& block : source.blocks()) {
        BlockDefinition* destBlock = dest.findBlock(block->name);
        if (!destBlock) continue;
        std::vector<std::unique_ptr<Entity>> children;
        children.reserve(block->entities.size());
        for (const auto& child : block->entities) children.push_back(remapEntity(*child, dest, layerMap));
        destBlock->entities = std::move(children);
        destBlock->pins = block->pins;
        destBlock->pads = block->pads;
        destBlock->dynamicParam = block->dynamicParam;
        destBlock->dynamicFlip = block->dynamicFlip;
        destBlock->dynamicRotation = block->dynamicRotation;
        destBlock->dynamicVisibility = block->dynamicVisibility;
        destBlock->dynamicArray = block->dynamicArray;
        destBlock->dynamicLookup = block->dynamicLookup;
        // xrefPath intentionally not copied: an extracted subset is a
        // real standalone file, not an xref cache snapshot.
    }

    for (EntityId id : ids) {
        const Entity* e = source.findEntity(id);
        if (!e) continue;
        dest.addEntity(remapEntity(*e, dest, layerMap));
    }

    return dest;
}

} // namespace lcad
