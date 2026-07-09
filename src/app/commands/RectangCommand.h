#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style RECTANG: two opposite corners produce a closed 4-vertex
// polyline on the current layer.
class RectangCommand : public DrawCommand {
public:
    explicit RectangCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_hasFirst ? std::optional<lcad::Point2D>(m_first) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::Point2D m_first;
    bool m_hasFirst = false;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
