#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"
#include "core/geometry/HatchPattern.h"

#include <vector>

// AutoCAD-style HATCH: with a pre-selected closed polyline, fills it
// directly; with nothing selected, prompts to pick internal points instead
// and derives the boundary from whatever Line/Polyline/Arc/Circle entities
// enclose each point (HatchBoundary.h). Either way, prompts for a pattern
// (SOLID or an ANSI line pattern), then scale and angle for patterns. One
// undo step. Enter accepts the defaults for all remaining prompts.
class HatchCommand : public DrawCommand {
public:
    HatchCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)), m_stage(m_ids.empty() ? Stage::BoundaryPick : Stage::Pattern) {
    }

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onOption(const QString& option) override;
    std::optional<QString> onScalar(double value) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override {
        if (m_finished) return m_result;
        if (m_stage == Stage::Pattern) {
            return QStringLiteral("Enter pattern [SOLID/ANSI31/ANSI32/ANSI33/ANSI37] <SOLID>:");
        }
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BoundaryPick, Pattern, Scale, Angle };

    void commit();

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    std::vector<std::vector<lcad::Point2D>> m_pickedBoundaries;
    Stage m_stage;
    lcad::HatchPattern m_pattern = lcad::HatchPattern::Solid;
    double m_scale = 1.0;
    double m_angleDeg = 0.0;
    std::optional<QString> m_result;
    bool m_finished = false;
};
