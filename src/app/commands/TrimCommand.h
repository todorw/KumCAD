#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style TRIM: the current selection acts as the cutting edges (empty
// selection = every entity is an edge, matching modern AutoCAD's quick-trim
// default), then each click removes the picked portion of the clicked entity
// between the nearest intersections. Lines, circles (become arcs), and arcs
// are trimmable; each click is one undo step.
class TrimCommand : public DrawCommand {
public:
    TrimCommand(lcad::Document& document, std::vector<lcad::EntityId> edgeIds, double pickTolerance)
        : m_document(document), m_edgeIds(std::move(edgeIds)), m_pickTolerance(pickTolerance) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Entity* pickTarget(const lcad::Point2D& pt) const;
    std::vector<lcad::Entity*> cuttingEdges(lcad::EntityId excludeId) const;
    QString trimLine(lcad::Entity& target, const lcad::Point2D& pt);
    QString trimCircle(lcad::Entity& target, const lcad::Point2D& pt);
    QString trimArc(lcad::Entity& target, const lcad::Point2D& pt);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_edgeIds;
    double m_pickTolerance;
    bool m_finished = false;
};
