#include "commands/NCopyCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Insert.h"

namespace {

QString shortTypeName(lcad::EntityType type) {
    switch (type) {
    case lcad::EntityType::Line: return QStringLiteral("LINE");
    case lcad::EntityType::Circle: return QStringLiteral("CIRCLE");
    case lcad::EntityType::Arc: return QStringLiteral("ARC");
    case lcad::EntityType::Polyline: return QStringLiteral("POLYLINE");
    case lcad::EntityType::Ellipse: return QStringLiteral("ELLIPSE");
    case lcad::EntityType::Spline: return QStringLiteral("SPLINE");
    case lcad::EntityType::Text: return QStringLiteral("TEXT");
    case lcad::EntityType::MText: return QStringLiteral("MTEXT");
    case lcad::EntityType::Hatch: return QStringLiteral("HATCH");
    case lcad::EntityType::Insert: return QStringLiteral("INSERT");
    default: return QStringLiteral("ENTITY");
    }
}

} // namespace

QString NCopyCommand::start() {
    const lcad::Entity* e = m_document.findEntity(m_insertId);
    if (!e || e->type() != lcad::EntityType::Insert) return QStringLiteral("NCOPY  *Selected object is not a block*");

    const auto& insert = static_cast<const lcad::InsertEntity&>(*e);
    const auto children = insert.instantiate();
    QString report = QStringLiteral("NCOPY  Nested entities:\n");
    for (std::size_t i = 0; i < children.size(); ++i) {
        report += QStringLiteral("  %1: %2\n").arg(i).arg(shortTypeName(children[i]->type()));
    }
    report += QStringLiteral("Index to copy:");
    return report;
}

std::optional<QString> NCopyCommand::onText(const QString& text) {
    m_finished = true;
    bool ok = false;
    const int index = text.trimmed().toInt(&ok);
    if (!ok || index < 0) {
        m_result = QStringLiteral("*Enter a valid index*");
        return m_result;
    }

    const lcad::Entity* e = m_document.findEntity(m_insertId);
    if (!e || e->type() != lcad::EntityType::Insert) {
        m_result = QStringLiteral("*Selected block no longer exists*");
        return m_result;
    }
    const auto& insert = static_cast<const lcad::InsertEntity&>(*e);
    auto children = insert.instantiate();
    if (index >= static_cast<int>(children.size())) {
        m_result = QStringLiteral("*Index out of range*");
        return m_result;
    }

    std::unique_ptr<lcad::Entity> copy = std::move(children[static_cast<std::size_t>(index)]);
    copy->setId(m_document.reserveEntityId());
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(copy)));
    m_result = QStringLiteral("*Nested entity copied to model space*");
    return m_result;
}
