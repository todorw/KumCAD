#pragma once

#include "core/document/Command.h"
#include "core/document/Document.h"
#include "core/geometry/Entity.h"

#include <memory>
#include <vector>

namespace lcad {

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

} // namespace lcad
