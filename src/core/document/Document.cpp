#include "core/document/Document.h"

#include "core/geometry/Dimension.h"

#include <algorithm>

namespace lcad {

Document::Document() {
    // Layer "0" is always present, mirroring AutoCAD's default layer.
    m_layers.push_back(Layer{0, "0", Color{255, 255, 255}, LineType::Continuous, true, false});
    m_nextLayerId = 1;
    m_currentLayer = 0;
}

LayerId Document::addLayer(const std::string& name, Color color) {
    const LayerId id = m_nextLayerId++;
    m_layers.push_back(Layer{id, name, color, LineType::Continuous, true, false});
    return id;
}

Layer* Document::findLayer(LayerId id) {
    auto it = std::find_if(m_layers.begin(), m_layers.end(), [id](const Layer& l) { return l.id == id; });
    return it != m_layers.end() ? &(*it) : nullptr;
}

const Layer* Document::findLayer(LayerId id) const {
    auto it = std::find_if(m_layers.begin(), m_layers.end(), [id](const Layer& l) { return l.id == id; });
    return it != m_layers.end() ? &(*it) : nullptr;
}

void Document::addEntity(std::unique_ptr<Entity> entity) {
    const EntityId id = entity->id();
    m_entityOrder.push_back(id);
    m_entityMap.emplace(id, std::move(entity));
}

std::unique_ptr<Entity> Document::removeEntity(EntityId id) {
    auto it = m_entityMap.find(id);
    if (it == m_entityMap.end()) return nullptr;
    std::unique_ptr<Entity> entity = std::move(it->second);
    m_entityMap.erase(it);
    m_entityOrder.erase(std::remove(m_entityOrder.begin(), m_entityOrder.end(), id), m_entityOrder.end());
    return entity;
}

Entity* Document::findEntity(EntityId id) {
    auto it = m_entityMap.find(id);
    return it != m_entityMap.end() ? it->second.get() : nullptr;
}

const Entity* Document::findEntity(EntityId id) const {
    auto it = m_entityMap.find(id);
    return it != m_entityMap.end() ? it->second.get() : nullptr;
}

std::vector<Entity*> Document::entities() {
    std::vector<Entity*> result;
    result.reserve(m_entityOrder.size());
    for (EntityId id : m_entityOrder) result.push_back(m_entityMap.at(id).get());
    return result;
}

std::vector<const Entity*> Document::entities() const {
    std::vector<const Entity*> result;
    result.reserve(m_entityOrder.size());
    for (EntityId id : m_entityOrder) result.push_back(m_entityMap.at(id).get());
    return result;
}

const BlockDefinition* Document::addBlock(std::string name, std::vector<std::unique_ptr<Entity>> entities) {
    auto block = std::make_unique<BlockDefinition>();
    block->name = std::move(name);
    block->entities = std::move(entities);
    m_blocks.push_back(std::move(block));
    return m_blocks.back().get();
}

void Document::reassociateDimensions() {
    // Resolves one anchor to the referenced entity's current snap point of
    // that kind/index; nullopt means the reference no longer resolves and the
    // dimension should go non-associative for that point.
    auto resolve = [this](const SnapRef& ref) -> std::optional<Point2D> {
        const Entity* target = findEntity(ref.entityId);
        if (!target) return std::nullopt;
        int seen = 0;
        for (const SnapPoint& sp : target->snapCandidates()) {
            if (sp.kind != ref.kind) continue;
            if (seen == ref.index) return sp.point;
            ++seen;
        }
        return std::nullopt;
    };

    for (EntityId id : m_entityOrder) {
        Entity* e = m_entityMap.at(id).get();
        if (e->type() != EntityType::Dimension) continue;
        auto* dim = static_cast<DimensionEntity*>(e);

        if (const auto& anchor = dim->anchor1()) {
            if (const auto pt = resolve(*anchor)) dim->setPoint1(*pt);
            else dim->setAnchor1(std::nullopt);
        }
        if (const auto& anchor = dim->anchor2()) {
            if (const auto pt = resolve(*anchor)) dim->setPoint2(*pt);
            else dim->setAnchor2(std::nullopt);
        }
    }
}

const BlockDefinition* Document::findBlock(const std::string& name) const {
    for (const auto& block : m_blocks) {
        if (block->name == name) return block.get();
    }
    return nullptr;
}

} // namespace lcad
