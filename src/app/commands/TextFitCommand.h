#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// TEXTFIT: stretches/repositions a selected single-line TEXT to fit
// between two new points, keeping its height fixed and adjusting only
// TextEntity::widthFactor (real DXF group 41) -- real AutoCAD TEXTFIT's
// own contract, not a general reflow/resize.
class TextFitCommand : public DrawCommand {
public:
    TextFitCommand(lcad::Document& document, lcad::EntityId targetId)
        : m_document(document), m_targetId(targetId) {}

    QString start() override { return QStringLiteral("TEXTFIT  Start point:"); }
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::EntityId m_targetId;
    int m_stage = 0; // 0 = start point, 1 = end point
    lcad::Point2D m_start;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    std::optional<QString> m_result;
    bool m_finished = false;
};
