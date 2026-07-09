#include "commands/MirrorCommand.h"

#include "core/document/Commands.h"

QString MirrorCommand::start() {
    return QStringLiteral("MIRROR  %1 found\nSpecify first point of mirror line:").arg(static_cast<int>(m_ids.size()));
}

std::optional<QString> MirrorCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasFirst) {
        m_first = pt;
        m_hasFirst = true;
        return QStringLiteral("Specify second point of mirror line:");
    }
    if (!m_hasSecond) {
        if (pt.distanceTo(m_first) < 1e-9) {
            return QStringLiteral("Specify second point of mirror line:"); // degenerate line, re-prompt
        }
        m_second = pt;
        m_hasSecond = true;
        return QStringLiteral("Erase source objects? [Yes/No] <No>:");
    }
    return std::nullopt;
}

std::optional<QString> MirrorCommand::onText(const QString& text) {
    const QString t = text.trimmed().toUpper();
    if (t.isEmpty() || t == QLatin1String("N") || t == QLatin1String("NO")) {
        commit(false);
        return std::nullopt;
    }
    if (t == QLatin1String("Y") || t == QLatin1String("YES")) {
        commit(true);
        return std::nullopt;
    }
    return QStringLiteral("Erase source objects? [Yes/No] <No>:");
}

void MirrorCommand::commit(bool eraseSource) {
    auto batch = std::make_unique<lcad::BatchCommand>("Mirror");
    for (lcad::EntityId id : m_ids) {
        const lcad::Entity* source = m_document.findEntity(id);
        if (!source) continue;
        std::unique_ptr<lcad::Entity> mirrored = source->clone();
        mirrored->mirror(m_first, m_second);
        mirrored->setId(m_document.reserveEntityId());
        batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(mirrored)));
        if (eraseSource) batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, id));
    }
    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
    m_finished = true;
}

void MirrorCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MirrorCommand::previewSegments() const {
    if (m_hasFirst && m_hasSecond) return {{m_first, m_second}};
    if (m_hasFirst && m_hasPreview) return {{m_first, m_previewPoint}};
    return {};
}
