#include "commands/InsertCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Insert.h"

#include <QStringList>

QString InsertCommand::availableBlocks() const {
    if (m_document.blocks().empty()) return QStringLiteral("(no blocks defined yet -- use BLOCK first)");
    QStringList names;
    for (const auto& block : m_document.blocks()) names << QString::fromStdString(block->name);
    return names.join(QStringLiteral(", "));
}

QString InsertCommand::start() {
    return QStringLiteral("INSERT  Available blocks: %1\nEnter block name:").arg(availableBlocks());
}

QString InsertCommand::nextAttributePromptOrFinish() {
    if (m_attIndex < m_attDefs.size()) {
        const lcad::AttDefEntity* def = m_attDefs[m_attIndex];
        return QStringLiteral("%1 <%2>:").arg(QString::fromStdString(def->prompt()),
                                              QString::fromStdString(def->defaultValue()));
    }

    auto insert = std::make_unique<lcad::InsertEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                       m_block, m_position);
    for (const auto& [tag, value] : m_values) insert->setAttribute(tag, value);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(insert)));
    m_finished = true;
    return QStringLiteral("*Inserted \"%1\"*").arg(QString::fromStdString(m_block->name));
}

std::optional<QString> InsertCommand::onText(const QString& text) {
    if (m_stage == Stage::Name) {
        const QString name = text.trimmed();
        if (name.isEmpty()) return QStringLiteral("Enter block name:");
        m_block = m_document.findBlock(name.toStdString());
        if (!m_block) {
            return QStringLiteral("*No block named \"%1\". Available: %2*\nEnter block name:")
                .arg(name, availableBlocks());
        }
        m_stage = Stage::Position;
        return QStringLiteral("Specify insertion point:");
    }

    if (m_stage == Stage::Attributes) {
        const lcad::AttDefEntity* def = m_attDefs[m_attIndex];
        const QString value = text.trimmed();
        m_values.emplace_back(def->tag(), value.isEmpty() ? def->defaultValue() : value.toStdString());
        ++m_attIndex;
        return nextAttributePromptOrFinish();
    }
    return std::nullopt;
}

std::optional<QString> InsertCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Position || !m_block) return std::nullopt;
    m_position = pt;

    for (const auto& child : m_block->entities) {
        if (child->type() == lcad::EntityType::AttDef) {
            m_attDefs.push_back(static_cast<const lcad::AttDefEntity*>(child.get()));
        }
    }
    m_stage = Stage::Attributes;
    return nextAttributePromptOrFinish();
}
