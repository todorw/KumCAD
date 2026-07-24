#include "core/document/BlockEdit.h"

#include <algorithm>

namespace lcad {

Document extractBlockForEditing(const Document& source, const std::string& blockName) {
    Document result;
    for (const Layer& l : source.layers()) {
        if (l.id != 0) result.addLayerRaw(l); // layer "0" already exists in a fresh Document
    }

    EntityId maxId = 0;
    if (const BlockDefinition* block = source.findBlock(blockName)) {
        for (const auto& entity : block->entities) {
            maxId = std::max(maxId, entity->id());
            result.addEntity(entity->clone());
        }
    }
    result.bumpNextEntityId(maxId + 1);
    return result;
}

void applyBlockEdit(BlockDefinition& target, const Document& edited) {
    std::vector<std::unique_ptr<Entity>> newEntities;
    for (const Entity* e : edited.entities()) newEntities.push_back(e->clone());
    target.entities = std::move(newEntities);
}

} // namespace lcad
