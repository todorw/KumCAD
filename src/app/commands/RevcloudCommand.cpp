#include "commands/RevcloudCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/PolylineOps.h"

#include <cmath>

QString RevcloudCommand::start() {
    return QStringLiteral("REVCLOUD  Arc length <2>:");
}

std::optional<QString> RevcloudCommand::onText(const QString& text) {
    m_finished = true;

    double archLength = 2.0;
    if (!text.trimmed().isEmpty()) {
        bool ok = false;
        const double v = text.trimmed().toDouble(&ok);
        if (!ok || v <= 0.0) {
            m_finished = false;
            return QStringLiteral("*Enter a positive arc length*");
        }
        archLength = v;
    }

    const lcad::Entity* e = m_document.findEntity(m_targetId);
    if (!e) {
        m_result = QStringLiteral("*Selected object no longer exists*");
        return m_result;
    }

    std::vector<lcad::Point2D> boundary;
    bool closed = false;
    if (e->type() == lcad::EntityType::Polyline) {
        const auto& pl = static_cast<const lcad::PolylineEntity&>(*e);
        boundary = pl.vertices();
        closed = pl.closed();
    } else if (e->type() == lcad::EntityType::Circle) {
        const auto& circle = static_cast<const lcad::CircleEntity&>(*e);
        constexpr int kSegments = 72;
        boundary.reserve(kSegments);
        for (int i = 0; i < kSegments; ++i) {
            const double angle = 2.0 * M_PI * i / kSegments;
            boundary.emplace_back(circle.center().x + circle.radius() * std::cos(angle),
                                  circle.center().y + circle.radius() * std::sin(angle));
        }
        closed = true;
    } else {
        m_result = QStringLiteral("*REVCLOUD Object mode needs a polyline or circle*");
        return m_result;
    }

    auto cloud = lcad::revisionCloud(m_document.reserveEntityId(), e->layer(), boundary, closed, archLength);
    if (!cloud) {
        m_result = QStringLiteral("*Could not build a revision cloud from that object*");
        return m_result;
    }

    m_document.commandStack().execute(std::make_unique<lcad::ReplaceEntityCommand>(m_document, m_targetId, std::move(cloud)));
    m_result = QStringLiteral("*Revision cloud created*");
    return m_result;
}
