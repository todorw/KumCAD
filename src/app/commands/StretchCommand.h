#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style STRETCH: specify a crossing window, then a base point and a
// second point; defining points inside the window move by that vector while
// geometry crossing the window edge stretches to follow. Entities that can't
// stretch partially (circles, text, blocks) move only when their primary
// point is inside the window.
class StretchCommand : public DrawCommand {
public:
    explicit StretchCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_stage == Stage::SecondPoint ? std::optional<lcad::Point2D>(m_base) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { WindowFirst, WindowSecond, BasePoint, SecondPoint };

    lcad::Document& m_document;
    Stage m_stage = Stage::WindowFirst;
    lcad::Point2D m_windowA;
    lcad::Point2D m_windowB;
    lcad::Point2D m_base;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
