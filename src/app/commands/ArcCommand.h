#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <array>

// AutoCAD-style 3-point ARC: start point, a second point the arc passes
// through, then the end point. The circumcircle through the three points
// determines center/radius; the sweep direction is chosen so the arc actually
// passes through the second point.
class ArcCommand : public DrawCommand {
public:
    explicit ArcCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_pointCount > 0 ? std::optional<lcad::Point2D>(m_points[m_pointCount - 1]) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::array<lcad::Point2D, 3> m_points;
    int m_pointCount = 0;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
