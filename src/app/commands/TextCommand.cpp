#include "commands/TextCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Text.h"

QString TextCommand::start() {
    return QStringLiteral("TEXT  Specify insertion point:");
}

std::optional<QString> TextCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == Stage::InsertionPoint) {
        m_position = pt;
        m_stage = Stage::Height;
        return QStringLiteral("Specify height:");
    }
    if (m_stage == Stage::Height) {
        const double h = m_position.distanceTo(pt);
        if (h < 1e-6) return QStringLiteral("Specify height:"); // degenerate pick, re-prompt
        m_height = h;
        m_stage = Stage::Content;
        return QStringLiteral("Enter text:");
    }
    return std::nullopt; // Content stage: points aren't expected here (dispatcher routes to onText instead)
}

std::optional<QString> TextCommand::onScalar(double value) {
    if (m_stage != Stage::Height || value < 1e-6) return std::nullopt;
    m_height = value;
    m_stage = Stage::Content;
    return QStringLiteral("Enter text:");
}

void TextCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> TextCommand::previewSegments() const {
    if (m_stage == Stage::Height && m_hasPreview) return {{m_position, m_previewPoint}};
    return {};
}

std::optional<QString> TextCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty()) {
        const auto id = m_document.reserveEntityId();
        auto entity =
            std::make_unique<lcad::TextEntity>(id, m_document.currentLayer(), m_position, trimmed.toStdString(), m_height);
        m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    }
    m_finished = true;
    return std::nullopt;
}
