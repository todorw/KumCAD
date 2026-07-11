#include "commands/MTextCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/MText.h"

#include <algorithm>
#include <cmath>

QString MTextCommand::start() {
    return QStringLiteral("MTEXT  Specify first corner:");
}

std::optional<QString> MTextCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == Stage::FirstCorner) {
        m_corner1 = pt;
        m_stage = Stage::OppositeCorner;
        return QStringLiteral("Specify opposite corner:");
    }
    if (m_stage == Stage::OppositeCorner) {
        const double w = std::abs(pt.x - m_corner1.x);
        if (w < 1e-6) return QStringLiteral("Specify opposite corner:");
        m_topLeft = lcad::Point2D(std::min(m_corner1.x, pt.x), std::max(m_corner1.y, pt.y));
        m_width = w;
        m_height = w / 40.0; // MTEXT's default text height relative to the box
        m_stage = Stage::Height;
        return QStringLiteral("Specify text height <%1>:").arg(m_height);
    }
    return std::nullopt;
}

std::optional<QString> MTextCommand::onScalar(double value) {
    if (m_stage != Stage::Height || value < 1e-6) return std::nullopt;
    m_height = value;
    m_stage = Stage::Content;
    return QStringLiteral("Enter text (empty line to finish):");
}

void MTextCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MTextCommand::previewSegments() const {
    if (m_stage != Stage::OppositeCorner || !m_hasPreview) return {};
    const lcad::Point2D a = m_corner1;
    const lcad::Point2D b = m_previewPoint;
    return {{a, {b.x, a.y}}, {{b.x, a.y}, b}, {b, {a.x, b.y}}, {{a.x, b.y}, a}};
}

std::optional<QString> MTextCommand::onText(const QString& text) {
    if (m_stage == Stage::Height) {
        if (!text.isEmpty()) {
            bool ok = false;
            const double h = text.toDouble(&ok);
            if (!ok || h < 1e-6) return QStringLiteral("Specify text height <%1>:").arg(m_height);
            m_height = h;
        }
        m_stage = Stage::Content;
        return QStringLiteral("Enter text (empty line to finish):");
    }
    if (!text.isEmpty()) {
        m_lines << text;
        return QStringLiteral("Enter next line (empty line to finish):");
    }
    commit();
    m_finished = true;
    return std::nullopt;
}

void MTextCommand::commit() {
    if (m_lines.isEmpty()) return;
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::MTextEntity>(id, m_document.currentLayer(), m_topLeft,
                                                      m_lines.join(QLatin1Char('\n')).toStdString(), m_height,
                                                      m_width);
    entity->setStyleName(m_document.currentTextStyleName());
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
}
