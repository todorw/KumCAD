#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style single-line TEXT: insertion point, then height (a point at
// that distance from the insertion point, or a typed number), then the text
// content itself (free text, routed via wantsTextInput()/onText()). Rotation
// isn't prompted for -- always horizontal, a deliberate simplification.
// Pressing Enter with no content cancels without creating anything, matching
// AutoCAD's own TEXT command.
class TextCommand : public DrawCommand {
public:
    explicit TextCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_stage != Stage::InsertionPoint ? std::optional<lcad::Point2D>(m_position) : std::nullopt;
    }
    bool wantsTextInput() const override { return m_stage == Stage::Content; }
    std::optional<QString> onText(const QString& text) override;
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { InsertionPoint, Height, Content };

    lcad::Document& m_document;
    Stage m_stage = Stage::InsertionPoint;
    lcad::Point2D m_position;
    double m_height = 0.0;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
