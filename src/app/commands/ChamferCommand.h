#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style CHAMFER on two pre-selected lines: type a distance to join
// them with a straight cut corner (trimming/extending both lines to the
// chamfer's own endpoints), or press Enter for distance 0 (a sharp
// corner) -- same UX shape as FilletCommand, with a straight segment
// instead of a tangent arc. A real, disclosed simplification: equal-
// distance chamfer only (real AutoCAD's own default), not independent
// Distance1/Distance2 or its separate Angle mode. One undo step.
class ChamferCommand : public DrawCommand {
public:
    ChamferCommand(lcad::Document& document, lcad::EntityId line1, lcad::EntityId line2)
        : m_document(document), m_line1(line1), m_line2(line2) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    QString apply(double distance);

    lcad::Document& m_document;
    lcad::EntityId m_line1;
    lcad::EntityId m_line2;
    std::optional<QString> m_result;
    bool m_finished = false;
};
