#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

// AutoCAD-style POLYGON: number of sides, center point, Inscribed/
// Circumscribed choice, then a radius -- typed directly, or picked as a
// point (which also orients the polygon's first vertex toward the
// click, matching real AutoCAD). Builds a closed straight-segment
// PolylineEntity (see core/geometry/PolygonOps.h for the real vertex
// geometry). One undo step.
class PolygonCommand : public DrawCommand {
public:
    explicit PolygonCommand(lcad::Document& document) : m_document(document) {}

    QString start() override;
    std::optional<QString> onPoint(const lcad::Point2D& pt) override;
    std::optional<QString> onScalar(double value) override;
    bool wantsTextInput() const override;
    std::optional<QString> onText(const QString& text) override;
    void onPreviewPoint(const lcad::Point2D& pt) override;
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override;
    std::optional<lcad::Point2D> anchorPoint() const override {
        const bool haveCenter = m_stage == Stage::InscribedOrCircumscribed || m_stage == Stage::Radius;
        return haveCenter ? std::optional<lcad::Point2D>(m_center) : std::nullopt;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    enum class Stage { Sides, Center, InscribedOrCircumscribed, Radius };
    void finish(double radius, double startAngleRadians);

    lcad::Document& m_document;
    Stage m_stage = Stage::Sides;
    int m_sides = 4;
    lcad::Point2D m_center;
    bool m_inscribed = true;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
