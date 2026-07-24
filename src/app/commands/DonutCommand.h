#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style DONUT: inside diameter, outside diameter, then a center
// point per click, placing one filled ring (see core/geometry/DonutOps.h)
// each time, repeating until Enter -- matching real AutoCAD's own
// "keep asking for centers" loop. Each donut is its own undo step (same
// convention PcbCommands.h's ViaCommand already uses for repeated
// clicks), built as a RegionEntity so it renders truly solid-filled with
// a real hole, not just two outline circles.
class DonutCommand : public DrawCommand {
public:
    explicit DonutCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    bool wantsTextInput() const override;
    std::optional<QString> onText(const QString& text) override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { InsideDiameter, OutsideDiameter, Center };
    lcad::Document& m_document;
    Stage m_stage = Stage::InsideDiameter;
    double m_insideDiameter = 0.5;
    double m_outsideDiameter = 1.0;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
