#pragma once

#include "commands/DrawCommand.h"
#include "core/document/Document.h"

#include <QStringList>

#include <utility>
#include <vector>

// AutoCAD-style GROUP: names the current selection as a group — click-
// selecting any member afterwards selects the whole group. UNGROUP (ungroup
// == true) dissolves a named group.
class GroupCommand : public DrawCommand {
public:
    GroupCommand(lcad::Document& document, std::vector<lcad::EntityId> ids, bool ungroup)
        : m_document(document), m_ids(std::move(ids)), m_ungroup(ungroup) {}

    QString start() override {
        QStringList names;
        for (const auto& [name, members] : m_document.groups()) names << QString::fromStdString(name);
        const QString existing = names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", "));
        if (m_ungroup) return QStringLiteral("UNGROUP  Groups: %1\nGroup name to dissolve:").arg(existing);
        return QStringLiteral("GROUP  %1 objects. Groups: %2\nEnter group name:")
            .arg(m_ids.size())
            .arg(existing);
    }

    std::optional<QString> onPoint(const lcad::Point2D& pt) override {
        (void)pt;
        return std::nullopt;
    }
    bool wantsTextInput() const override { return true; }
    std::optional<QString> onText(const QString& text) override {
        const QString name = text.trimmed();
        m_finished = true;
        if (name.isEmpty()) return QStringLiteral("*Cancelled*");
        if (m_ungroup) {
            return m_document.removeGroup(name.toStdString())
                       ? QStringLiteral("*Group \"%1\" dissolved*").arg(name)
                       : QStringLiteral("*No group named \"%1\"*").arg(name);
        }
        m_document.setGroup(name.toStdString(), m_ids);
        return QStringLiteral("*Group \"%1\" created with %2 objects*").arg(name).arg(m_ids.size());
    }
    bool isFinished() const override { return m_finished; }
    void cancel() override { m_finished = true; }

private:
    lcad::Document& m_document;
    std::vector<lcad::EntityId> m_ids;
    bool m_ungroup;
    bool m_finished = false;
};
