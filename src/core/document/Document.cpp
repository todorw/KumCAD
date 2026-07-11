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
    if (m_activeSpace >= 0 && m_activeSpace < static_cast<int>(m_layouts.size())) {
        m_layouts[m_activeSpace].entityIds.push_back(id);
    } else {
        m_entityOrder.push_back(id);
    }
    m_entityMap.emplace(id, std::move(entity));
}

std::unique_ptr<Entity> Document::removeEntity(EntityId id) {
    auto it = m_entityMap.find(id);
    if (it == m_entityMap.end()) return nullptr;
    std::unique_ptr<Entity> entity = std::move(it->second);
    m_entityMap.erase(it);
    m_entityOrder.erase(std::remove(m_entityOrder.begin(), m_entityOrder.end(), id), m_entityOrder.end());
    for (Layout& layout : m_layouts) {
        layout.entityIds.erase(std::remove(layout.entityIds.begin(), layout.entityIds.end(), id),
                               layout.entityIds.end());
    }
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

std::vector<Entity*> Document::paperEntities(int layoutIndex) {
    std::vector<Entity*> result;
    if (layoutIndex < 0 || layoutIndex >= static_cast<int>(m_layouts.size())) return result;
    const auto& ids = m_layouts[layoutIndex].entityIds;
    result.reserve(ids.size());
    for (EntityId id : ids) result.push_back(m_entityMap.at(id).get());
    return result;
}

std::vector<const Entity*> Document::paperEntities(int layoutIndex) const {
    std::vector<const Entity*> result;
    if (layoutIndex < 0 || layoutIndex >= static_cast<int>(m_layouts.size())) return result;
    const auto& ids = m_layouts[layoutIndex].entityIds;
    result.reserve(ids.size());
    for (EntityId id : ids) result.push_back(m_entityMap.at(id).get());
    return result;
}

bool Document::removeLayout(int index) {
    if (index < 0 || index >= static_cast<int>(m_layouts.size()) || m_layouts.size() == 1) return false;
    for (EntityId id : m_layouts[index].entityIds) m_entityMap.erase(id);
    m_layouts.erase(m_layouts.begin() + index);
    if (m_activeSpace >= static_cast<int>(m_layouts.size())) m_activeSpace = -1;
    return true;
}

NamedDimStyle& Document::currentNamedDimStyle() {
    for (NamedDimStyle& s : m_dimStyles) {
        if (s.name == m_currentDimStyle) return s;
    }
    return m_dimStyles.front();
}

const NamedDimStyle& Document::currentNamedDimStyle() const {
    for (const NamedDimStyle& s : m_dimStyles) {
        if (s.name == m_currentDimStyle) return s;
    }
    return m_dimStyles.front();
}

NamedDimStyle* Document::findDimStyle(const std::string& name) {
    for (NamedDimStyle& s : m_dimStyles) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

NamedDimStyle& Document::addOrUpdateDimStyle(const std::string& name, const DimStyle& style) {
    if (NamedDimStyle* existing = findDimStyle(name)) {
        existing->style = style;
        return *existing;
    }
    m_dimStyles.push_back(NamedDimStyle{name, style});
    return m_dimStyles.back();
}

bool Document::setCurrentDimStyle(const std::string& name) {
    if (!findDimStyle(name)) return false;
    m_currentDimStyle = name;
    return true;
}

const TextStyle* Document::findTextStyle(const std::string& name) const {
    for (const TextStyle& s : m_textStyles) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

TextStyle& Document::addOrUpdateTextStyle(const TextStyle& style) {
    for (TextStyle& s : m_textStyles) {
        if (s.name == style.name) {
            s = style;
            return s;
        }
    }
    m_textStyles.push_back(style);
    return m_textStyles.back();
}

bool Document::setCurrentTextStyle(const std::string& name) {
    if (!findTextStyle(name)) return false;
    m_currentTextStyle = name;
    return true;
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

    for (auto& [id, entityPtr] : m_entityMap) {
        Entity* e = entityPtr.get();
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
