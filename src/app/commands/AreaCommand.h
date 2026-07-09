#pragma once

#include "commands/DrawCommand.h"

#include <vector>

// AutoCAD-style AREA: pick the corners of a region, press Enter, and get the
// enclosed area (shoelace formula) and perimeter reported. Changes nothing.
class AreaCommand : public DrawCommand {
public:
    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_points.empty() ? std::nullopt : std::optional<lcad::Point2D>(m_points.back());
    }
    bool requestFinish() override;
    std::optional<QString> resultMessage() const override { return m_result; }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    std::vector<lcad::Point2D> m_points;
    std::optional<QString> m_result;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
