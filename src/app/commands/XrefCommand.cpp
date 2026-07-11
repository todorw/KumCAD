#include "commands/XrefCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Insert.h"
#include "core/io/Xref.h"

#include <QStringList>

QString XrefCommand::xrefList() const {
    QStringList names;
    for (const auto& block : m_document.blocks()) {
        if (block->isXref()) {
            names << QStringLiteral("%1 (%2)").arg(QString::fromStdString(block->name),
                                                   QString::fromStdString(block->xrefPath));
        }
    }
    return names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", "));
}

QString XrefCommand::start() {
    return QStringLiteral("XREF  Attached: %1\nOption [Attach/Reload/Detach]:").arg(xrefList());
}

std::optional<QString> XrefCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    const QString upper = trimmed.toUpper();

    switch (m_stage) {
    case Stage::Option:
        if (upper.isEmpty()) {
            m_finished = true;
            return QStringLiteral("*XREF done*");
        }
        if (upper == QLatin1String("A") || upper == QLatin1String("ATTACH")) {
            m_stage = Stage::AttachPath;
            return QStringLiteral("Path to DXF/DWG file:");
        }
        if (upper == QLatin1String("R") || upper == QLatin1String("RELOAD")) {
            m_stage = Stage::ReloadName;
            return QStringLiteral("Xref name to reload:");
        }
        if (upper == QLatin1String("D") || upper == QLatin1String("DETACH")) {
            m_stage = Stage::DetachName;
            return QStringLiteral("Xref name to detach:");
        }
        return QStringLiteral("*Invalid option* [Attach/Reload/Detach]:");
    case Stage::AttachPath: {
        if (trimmed.isEmpty()) {
            m_finished = true;
            return QStringLiteral("*Cancelled*");
        }
        std::string error;
        m_attached = lcad::attachXref(m_document, trimmed.toStdString(), &error);
        if (!m_attached) return QStringLiteral("*%1*\nPath to DXF/DWG file:").arg(QString::fromStdString(error));
        m_stage = Stage::InsertPoint;
        return QStringLiteral("\"%1\" loaded (%2 entities). Specify insertion point:")
            .arg(QString::fromStdString(m_attached->name))
            .arg(m_attached->entities.size());
    }
    case Stage::ReloadName: {
        std::string error;
        m_finished = true;
        if (!lcad::reloadXref(m_document, trimmed.toStdString(), &error)) {
            return QStringLiteral("*%1*").arg(QString::fromStdString(error));
        }
        return QStringLiteral("*Xref \"%1\" reloaded*").arg(trimmed);
    }
    case Stage::DetachName: {
        const lcad::BlockDefinition* block = m_document.findBlock(trimmed.toStdString());
        m_finished = true;
        if (!block || !block->isXref()) return QStringLiteral("*\"%1\" is not an attached xref*").arg(trimmed);
        auto batch = std::make_unique<lcad::BatchCommand>("Detach xref");
        int removed = 0;
        for (const lcad::Entity* e : m_document.entities()) {
            if (e->type() != lcad::EntityType::Insert) continue;
            if (static_cast<const lcad::InsertEntity*>(e)->block() != block) continue;
            batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, e->id()));
            ++removed;
        }
        if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
        return QStringLiteral("*Detached \"%1\": %2 insert(s) removed*").arg(trimmed).arg(removed);
    }
    case Stage::InsertPoint:
        return std::nullopt; // point stage: handled by onPoint
    }
    return std::nullopt;
}

std::optional<QString> XrefCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::InsertPoint || !m_attached) return std::nullopt;
    auto insert = std::make_unique<lcad::InsertEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                       m_attached, pt);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(insert)));
    m_finished = true;
    return QStringLiteral("*Xref \"%1\" attached*").arg(QString::fromStdString(m_attached->name));
}
