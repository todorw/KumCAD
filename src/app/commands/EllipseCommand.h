#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-inspired 2-point ELLIPSE: center point, then a corner point whose X
// and Y offsets from the center become the X and Y radii (axis-aligned only
// -- see EllipseEntity for why). Simpler than AutoCAD's real 3-point default,
// but keeps the same interaction shape as CIRCLE.
class EllipseCommand : public DrawCommand {
public:
    explicit EllipseCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_hasCenter ? std::optional<lcad::Point2D>(m_center) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    lcad::Point2D m_center;
    bool m_hasCenter = false;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
