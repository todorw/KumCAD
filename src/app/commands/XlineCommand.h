#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Commands.h"
#include "core/document/Document.h"
#include "core/geometry/ConstructionLine.h"

// AutoCAD XLINE/RAY: base point, then each through-point creates one
// construction line (or ray) until Enter.
class XlineCommand : public DrawCommand {
public:
    XlineCommand(lcad::Document& document, bool ray) : m_document(document), m_ray(ray) {}

    QString start() override {
        return (m_ray ? QStringLiteral("RAY") : QStringLiteral("XLINE")) + QStringLiteral("  Specify base point:");
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        if (!m_hasBase) {
            m_base = pt;
            m_hasBase = true;
            return QStringLiteral("Specify through point (Enter to finish):");
        }
        if (pt.distanceTo(m_base) < 1e-9) return QStringLiteral("Specify through point (Enter to finish):");
        m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::ConstructionLineEntity>(
                            m_document.reserveEntityId(), m_document.currentLayer(), m_base, pt - m_base, m_ray)));
        return QStringLiteral("Specify through point (Enter to finish):");
    }

    void onPreviewPoint(const lcad::Point2D& pt) override {
        m_previewPoint = pt;
        m_hasPreview = true;
    }
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> previewSegments() const override {
        if (!m_hasBase || !m_hasPreview) return {};
        const lcad::Point2D d = m_previewPoint - m_base;
        if (d.length() < 1e-9) return {};
        const lcad::Point2D dir = d * (1.0 / d.length());
        const lcad::Point2D far = m_base + dir * 1e5;
        return {{m_ray ? m_base : m_base - dir * 1e5, far}};
    }
    std::optional<lcad::Point2D> anchorPoint() const override {
        return m_hasBase ? std::optional<lcad::Point2D>(m_base) : std::nullopt;
    }
    bool requestFinish() override {
        m_finished = true;
        return true;
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    bool m_ray;
    lcad::Point2D m_base;
    bool m_hasBase = false;
    lcad::Point2D m_previewPoint;
    bool m_hasPreview = false;
    bool m_finished = false;
};
