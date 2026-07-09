#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD-style EXTEND: the current selection acts as the boundary edges
// (empty selection = every entity), then clicking near an end of a line or
// arc lengthens that end to the nearest boundary intersection.
class ExtendCommand : public DrawCommand {
public:
    ExtendCommand(lcad::Document& document, std::vector<lcad::EntityId> edgeIds, double pickTolerance)
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
    std::vector<lcad::Entity*> boundaryEdges(lcad::EntityId excludeId) const;
    QString extendLine(lcad::Entity& target, const lcad::Point2D& pt);
    QString extendArc(lcad::Entity& target, const lcad::Point2D& pt);

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_edgeIds;
    double m_pickTolerance;
    bool m_finished = false;
};
