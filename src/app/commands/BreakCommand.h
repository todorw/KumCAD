#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style BREAK: the pick that selects the object doubles as the first
// break point (type F to re-specify it), then the second point removes the
// stretch between them ("@" breaks at the first point without removing
// anything). Lines, arcs, circles (become arcs, removing CCW from first to
// second), and straight polylines.
class BreakCommand : public DrawCommand {
public:
    BreakCommand(lcad::Document& document, double pickTolerance)
        : m_document(document), m_pickTolerance(pickTolerance) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onOption(const QString& option) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { SelectObject, FirstPoint, SecondPoint };

    QString applyBreak(const lcad::Point2D& secondPt);

    lcad::Document& m_document;
    double m_pickTolerance;
    Stage m_stage = Stage::SelectObject;
    lcad::EntityId m_targetId = 0;
    lcad::Point2D m_firstPoint;
    bool m_finished = false;
};
