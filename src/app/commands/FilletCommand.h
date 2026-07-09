#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style FILLET on two pre-selected lines: type a radius to join them
// with a tangent arc (trimming/extending both lines to the tangent points),
// or press Enter for radius 0 (a sharp corner). One undo step.
class FilletCommand : public DrawCommand {
public:
    FilletCommand(lcad::Document& document, lcad::EntityId line1, lcad::EntityId line2)
        : m_document(document), m_line1(line1), m_line2(line2) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    QString apply(double radius);

    lcad::Document& m_document;
    lcad::EntityId m_line1;
    lcad::EntityId m_line2;
    std::optional<QString> m_result;
    bool m_finished = false;
};
