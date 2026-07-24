#include "commands/PolygonCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/PolygonOps.h"

#include <cmath>

QString PolygonCommand::start() {
    return QStringLiteral("POLYGON  Enter number of sides <4>:");
}

bool PolygonCommand::wantsTextInput() const {
    return m_stage == Stage::Sides || m_stage == Stage::InscribedOrCircumscribed;
}

std::optional<QString> PolygonCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_stage == Stage::Sides) {
        if (trimmed.isEmpty()) {
            m_sides = 4;
        } else {
            bool ok = false;
            const int sides = trimmed.toInt(&ok);
            if (!ok || sides < 3 || sides > 1024) {
                return QStringLiteral("*Invalid number of sides (3-1024)*");
            }
            m_sides = sides;
        }
        m_stage = Stage::Center;
        return QStringLiteral("Specify center point:");
    }

    // Stage::InscribedOrCircumscribed
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("I"), Qt::CaseInsensitive) == 0) {
        m_inscribed = true;
    } else if (trimmed.compare(QStringLiteral("C"), Qt::CaseInsensitive) == 0) {
        m_inscribed = false;
    } else {
        return QStringLiteral("*Enter I or C*");
    }
    m_stage = Stage::Radius;
    return m_inscribed ? QStringLiteral("Specify radius of circle:") : QStringLiteral("Specify apothem:");
}

std::optional<QString> PolygonCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == Stage::Center) {
        m_center = pt;
        m_stage = Stage::InscribedOrCircumscribed;
        return QStringLiteral("Inscribed in circle or Circumscribed about circle [I/C] <I>:");
    }
    if (m_stage == Stage::Radius) {
        const double radius = pt.distanceTo(m_center);
        if (radius < 1e-9) return std::nullopt;
        finish(radius, std::atan2(pt.y - m_center.y, pt.x - m_center.x));
        return std::nullopt;
    }
    return std::nullopt; // Sides/InscribedOrCircumscribed are text-only stages
}

std::optional<QString> PolygonCommand::onScalar(double value) {
    if (m_stage != Stage::Radius || value <= 0.0) return std::nullopt;
    finish(value, 0.0);
    return std::nullopt;
}

void PolygonCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> PolygonCommand::previewSegments() const {
    if (m_stage != Stage::Radius || !m_hasPreview) return {};
    const double radius = m_previewPoint.distanceTo(m_center);
    if (radius < 1e-9) return {};
    const double startAngle = std::atan2(m_previewPoint.y - m_center.y, m_previewPoint.x - m_center.x);
    const auto verts = lcad::regularPolygonVertices(m_center, radius, m_sides, m_inscribed, startAngle);
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i < verts.size(); ++i) segs.emplace_back(verts[i], verts[(i + 1) % verts.size()]);
    return segs;
}

void PolygonCommand::finish(double radius, double startAngleRadians) {
    const auto verts = lcad::regularPolygonVertices(m_center, radius, m_sides, m_inscribed, startAngleRadians);
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::PolylineEntity>(id, m_document.currentLayer(), verts, /*closed=*/true);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    m_finished = true;
}
