#pragma once

#include "core/document/Command.h"
#include "core/document/Document.h"
#include "core/document/Layout.h"
#include "core/geometry/Entity.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lcad {

// Groups several commands into one undo step, e.g. erasing a multi-entity
// selection or placing a batch of copies. Children execute in order and undo
// in reverse.
class BatchCommand : public Command {
public:
    explicit BatchCommand(std::string description) : m_description(std::move(description)) {}

    void add(std::unique_ptr<Command> command) { m_commands.push_back(std::move(command)); }
    bool empty() const { return m_commands.empty(); }

    void execute() override {
        for (auto& command : m_commands) command->execute();
    }
    void undo() override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) (*it)->undo();
    }
    std::string description() const override { return m_description; }

private:
    std::string m_description;
    std::vector<std::unique_ptr<Command>> m_commands;
};

// Wraps Document::addEntity so it can be undone. Own the entity while it is
// not in the document (i.e. before execute() / after undo()).
class AddEntityCommand : public Command {
public:
    AddEntityCommand(Document& document, std::unique_ptr<Entity> entity)
        : m_document(document), m_entity(std::move(entity)), m_id(m_entity->id()) {}

    void execute() override { m_document.addEntity(std::move(m_entity)); }
    void undo() override { m_entity = m_document.removeEntity(m_id); }
    std::string description() const override { return "Add entity"; }

private:
    Document& m_document;
    std::unique_ptr<Entity> m_entity;
    EntityId m_id;
};

class DeleteEntityCommand : public Command {
public:
    DeleteEntityCommand(Document& document, EntityId id) : m_document(document), m_id(id) {}

    void execute() override { m_entity = m_document.removeEntity(m_id); }
    void undo() override { m_document.addEntity(std::move(m_entity)); }
    std::string description() const override { return "Delete entity"; }

private:
    Document& m_document;
    EntityId m_id;
    std::unique_ptr<Entity> m_entity;
};

// Rigid move of a set of entities by the same delta, e.g. from a click-drag.
// Undo re-applies the inverse delta rather than restoring a snapshot, so it
// composes fine with any other translate that happened in between.
class TranslateEntitiesCommand : public Command {
public:
    TranslateEntitiesCommand(Document& document, std::vector<EntityId> ids, Point2D delta)
        : m_document(document), m_ids(std::move(ids)), m_delta(delta) {}

    void execute() override { apply(m_delta); }
    void undo() override { apply(Point2D(-m_delta.x, -m_delta.y)); }
    std::string description() const override { return "Move"; }

private:
    void apply(const Point2D& delta) {
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) e->translate(delta);
        }
    }

    Document& m_document;
    std::vector<EntityId> m_ids;
    Point2D m_delta;
};

// Reshapes a single entity by moving one of its grip points, e.g. dragging a
// line endpoint or a circle's radius handle.
class MoveGripCommand : public Command {
public:
    MoveGripCommand(Document& document, EntityId id, std::size_t gripIndex, Point2D oldPos, Point2D newPos)
        : m_document(document), m_id(id), m_gripIndex(gripIndex), m_oldPos(oldPos), m_newPos(newPos) {}

    void execute() override {
        if (Entity* e = m_document.findEntity(m_id)) e->moveGripPoint(m_gripIndex, m_newPos);
    }
    void undo() override {
        if (Entity* e = m_document.findEntity(m_id)) e->moveGripPoint(m_gripIndex, m_oldPos);
    }
    std::string description() const override { return "Stretch"; }

private:
    Document& m_document;
    EntityId m_id;
    std::size_t m_gripIndex;
    Point2D m_oldPos;
    Point2D m_newPos;
};

// Rigid rotation of a set of entities about center, e.g. from the ROTATE command.
class RotateEntitiesCommand : public Command {
public:
    RotateEntitiesCommand(Document& document, std::vector<EntityId> ids, Point2D center, double angleRadians)
        : m_document(document), m_ids(std::move(ids)), m_center(center), m_angle(angleRadians) {}

    void execute() override { apply(m_angle); }
    void undo() override { apply(-m_angle); }
    std::string description() const override { return "Rotate"; }

private:
    void apply(double angle) {
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) e->rotate(m_center, angle);
        }
    }

    Document& m_document;
    std::vector<EntityId> m_ids;
    Point2D m_center;
    double m_angle;
};

// Uniform scale of a set of entities about center, e.g. from the SCALE command.
// factor must be strictly positive so 1/factor is a valid inverse for undo.
class ScaleEntitiesCommand : public Command {
public:
    ScaleEntitiesCommand(Document& document, std::vector<EntityId> ids, Point2D center, double factor)
        : m_document(document), m_ids(std::move(ids)), m_center(center), m_factor(factor) {}

    void execute() override { apply(m_factor); }
    void undo() override { apply(1.0 / m_factor); }
    std::string description() const override { return "Scale"; }

private:
    void apply(double factor) {
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) e->scale(m_center, factor);
        }
    }

    Document& m_document;
    std::vector<EntityId> m_ids;
    Point2D m_center;
    double m_factor;
};

// Sets or clears (ByLayer) the color override of a set of entities, e.g. from
// the Properties panel. Captures prior overrides on execute() so undo can
// restore a mixed-color selection.
class SetEntityColorCommand : public Command {
public:
    SetEntityColorCommand(Document& document, std::vector<EntityId> ids, std::optional<Color> color)
        : m_document(document), m_ids(std::move(ids)), m_color(color) {}

    void execute() override {
        m_oldColors.clear();
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) {
                m_oldColors.emplace_back(id, e->colorOverride());
                e->setColorOverride(m_color);
            }
        }
    }
    void undo() override {
        for (const auto& [id, color] : m_oldColors) {
            if (Entity* e = m_document.findEntity(id)) e->setColorOverride(color);
        }
    }
    std::string description() const override { return "Change Color"; }

private:
    Document& m_document;
    std::vector<EntityId> m_ids;
    std::optional<Color> m_color;
    std::vector<std::pair<EntityId, std::optional<Color>>> m_oldColors;
};

// Swaps an entity for a replacement (same id), e.g. PEDIT rewriting a
// polyline. Owns whichever version is currently out of the document.
class ReplaceEntityCommand : public Command {
public:
    ReplaceEntityCommand(Document& document, EntityId id, std::unique_ptr<Entity> replacement)
        : m_document(document), m_id(id), m_stored(std::move(replacement)) {}

    void execute() override { swap(); }
    void undo() override { swap(); }
    std::string description() const override { return "Edit entity"; }

private:
    void swap() {
        std::unique_ptr<Entity> current = m_document.removeEntity(m_id);
        m_document.addEntity(std::move(m_stored));
        m_stored = std::move(current);
    }

    Document& m_document;
    EntityId m_id;
    std::unique_ptr<Entity> m_stored;
};

// Sets or clears (ByLayer) the linetype override of a set of entities, e.g.
// from the Properties panel. Mirrors SetEntityColorCommand.
class SetEntityLinetypeCommand : public Command {
public:
    SetEntityLinetypeCommand(Document& document, std::vector<EntityId> ids, std::optional<LineType> linetype)
        : m_document(document), m_ids(std::move(ids)), m_linetype(linetype) {}

    void execute() override {
        m_oldLinetypes.clear();
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) {
                m_oldLinetypes.emplace_back(id, e->linetypeOverride());
                e->setLinetypeOverride(m_linetype);
            }
        }
    }
    void undo() override {
        for (const auto& [id, linetype] : m_oldLinetypes) {
            if (Entity* e = m_document.findEntity(id)) e->setLinetypeOverride(linetype);
        }
    }
    std::string description() const override { return "Change Linetype"; }

private:
    Document& m_document;
    std::vector<EntityId> m_ids;
    std::optional<LineType> m_linetype;
    std::vector<std::pair<EntityId, std::optional<LineType>>> m_oldLinetypes;
};

// Sets or clears (ByLayer) the lineweight override of a set of entities
// (LWEIGHT command). Mirrors SetEntityColorCommand.
class SetEntityLineweightCommand : public Command {
public:
    SetEntityLineweightCommand(Document& document, std::vector<EntityId> ids, std::optional<double> weight)
        : m_document(document), m_ids(std::move(ids)), m_weight(weight) {}

    void execute() override {
        m_oldWeights.clear();
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) {
                m_oldWeights.emplace_back(id, e->lineweightOverride());
                e->setLineweightOverride(m_weight);
            }
        }
    }
    void undo() override {
        for (const auto& [id, weight] : m_oldWeights) {
            if (Entity* e = m_document.findEntity(id)) e->setLineweightOverride(weight);
        }
    }
    std::string description() const override { return "Change Lineweight"; }

private:
    Document& m_document;
    std::vector<EntityId> m_ids;
    std::optional<double> m_weight;
    std::vector<std::pair<EntityId, std::optional<double>>> m_oldWeights;
};

// Copies display properties (layer, color/linetype/lineweight overrides)
// from a source entity onto targets (MATCHPROP). Snapshots targets' prior
// values on execute for undo.
class MatchPropertiesCommand : public Command {
public:
    MatchPropertiesCommand(Document& document, EntityId sourceId, std::vector<EntityId> targetIds)
        : m_document(document), m_sourceId(sourceId), m_targets(std::move(targetIds)) {}

    void execute() override {
        const Entity* source = m_document.findEntity(m_sourceId);
        if (!source) return;
        m_old.clear();
        for (EntityId id : m_targets) {
            if (Entity* e = m_document.findEntity(id)) {
                m_old.push_back({id, e->layer(), e->colorOverride(), e->linetypeOverride(), e->lineweightOverride()});
                e->setLayer(source->layer());
                e->setColorOverride(source->colorOverride());
                e->setLinetypeOverride(source->linetypeOverride());
                e->setLineweightOverride(source->lineweightOverride());
            }
        }
    }
    void undo() override {
        for (const auto& snap : m_old) {
            if (Entity* e = m_document.findEntity(snap.id)) {
                e->setLayer(snap.layer);
                e->setColorOverride(snap.color);
                e->setLinetypeOverride(snap.linetype);
                e->setLineweightOverride(snap.lineweight);
            }
        }
    }
    std::string description() const override { return "Match Properties"; }

private:
    struct Snapshot {
        EntityId id;
        LayerId layer;
        std::optional<Color> color;
        std::optional<LineType> linetype;
        std::optional<double> lineweight;
    };

    Document& m_document;
    EntityId m_sourceId;
    std::vector<EntityId> m_targets;
    std::vector<Snapshot> m_old;
};

// Reassigns a set of entities to a different layer, e.g. from the Properties
// panel. Captures each entity's prior layer on first execute() so undo can
// restore per-entity origins even if the selection had mixed layers.
class SetEntityLayerCommand : public Command {
public:
    SetEntityLayerCommand(Document& document, std::vector<EntityId> ids, LayerId newLayer)
        : m_document(document), m_ids(std::move(ids)), m_newLayer(newLayer) {}

    void execute() override {
        m_oldLayers.clear();
        for (EntityId id : m_ids) {
            if (Entity* e = m_document.findEntity(id)) {
                m_oldLayers.emplace_back(id, e->layer());
                e->setLayer(m_newLayer);
            }
        }
    }
    void undo() override {
        for (const auto& [id, layer] : m_oldLayers) {
            if (Entity* e = m_document.findEntity(id)) e->setLayer(layer);
        }
    }
    std::string description() const override { return "Change Layer"; }

private:
    Document& m_document;
    std::vector<EntityId> m_ids;
    LayerId m_newLayer;
    std::vector<std::pair<EntityId, LayerId>> m_oldLayers;
};

// Undoable replacement of the whole layouts vector (LAYOUT New/Copy/Rename,
// MVIEW, VPSCALE, PAGESETUP): simpler than fine-grained per-field commands
// since Layout and Viewport are small copyable value types with no owned
// entities of their own -- unlike deleting a layout, which also needs to
// remove/restore the entities on its sheet (see DeleteLayoutCommand).
class SetLayoutsCommand : public Command {
public:
    SetLayoutsCommand(Document& document, std::vector<Layout> newLayouts)
        : m_document(document), m_newLayouts(std::move(newLayouts)) {}

    void execute() override {
        m_oldLayouts = m_document.layouts();
        m_document.layouts() = m_newLayouts;
    }
    void undo() override { m_document.layouts() = m_oldLayouts; }
    std::string description() const override { return "Layout"; }

private:
    Document& m_document;
    std::vector<Layout> m_newLayouts;
    std::vector<Layout> m_oldLayouts;
};

// LAYOUT Delete: removes a layout and the entities drawn on its sheet.
// Unlike SetLayoutsCommand, this must also snapshot/restore those entities
// (Document::removeEntity erases them from the document entirely), so it's
// its own Command rather than a layouts-vector swap.
class DeleteLayoutCommand : public Command {
public:
    DeleteLayoutCommand(Document& document, int index) : m_document(document), m_index(index) {}

    void execute() override {
        m_layout = m_document.layouts()[static_cast<std::size_t>(m_index)];
        m_removedEntities.clear();
        for (EntityId id : m_layout.entityIds) {
            if (auto entity = m_document.removeEntity(id)) m_removedEntities.push_back(std::move(entity));
        }
        m_document.layouts().erase(m_document.layouts().begin() + m_index);
    }
    void undo() override {
        m_document.layouts().insert(m_document.layouts().begin() + m_index, m_layout);
        for (auto& entity : m_removedEntities) m_document.restoreEntity(std::move(entity));
        m_removedEntities.clear();
    }
    std::string description() const override { return "Delete Layout"; }

private:
    Document& m_document;
    int m_index;
    Layout m_layout;
    std::vector<std::unique_ptr<Entity>> m_removedEntities;
};

} // namespace lcad
