#pragma once

#include "core/document/Command.h"
#include "core/document/Document.h"
#include "core/geometry/Entity.h"

#include <memory>
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

} // namespace lcad
