#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <vector>

// AutoCAD GRADIENT: like HATCH (pre-selected closed polyline, or pick
// internal points), but fills with a two-color linear gradient instead of a
// pattern. Colors are entered as AutoCAD Color Index numbers (1-255), the
// same palette DXF ACI colors already use elsewhere in KumCAD.
class GradientCommand : public DrawCommand {
public:
    GradientCommand(lcad::Document& document, std::vector<lcad::EntityId> ids)
        : m_document(document), m_ids(std::move(ids)),
          m_stage(m_ids.empty() ? Stage::BoundaryPick : Stage::Color1) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override {
        if (m_finished) return m_result;
        if (m_stage == Stage::Color1) return QStringLiteral("Enter first color [ACI 1-255] <5>:");
        return std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { BoundaryPick, Color1, Color2, Angle };

    void commit();

    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    std::vector<std::vector<lcad::Point2D>> m_pickedBoundaries;
    Stage m_stage;
    int m_aci1 = 5;
    int m_aci2 = 150;
    double m_angleDeg = 0.0;
    std::optional<QString> m_result;
    bool m_finished = false;
};
