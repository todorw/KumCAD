#include "commands/LayoutCommand.h"

#include "core/document/Commands.h"

#include <QStringList>

QString LayoutCommand::layoutList() const {
    QStringList names;
    for (const lcad::Layout& layout : m_document.layouts()) names << QString::fromStdString(layout.name);
    return names.join(QStringLiteral(", "));
}

int LayoutCommand::findLayout(const QString& name) const {
    const auto& layouts = m_document.layouts();
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        if (QString::fromStdString(layouts[i].name).compare(name, Qt::CaseInsensitive) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QString LayoutCommand::uniqueName(const QString& base) const {
    if (findLayout(base) < 0) return base;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (findLayout(candidate) < 0) return candidate;
    }
}

QString LayoutCommand::start() {
    return QStringLiteral("LAYOUT  Layouts: %1\nOption [New/Copy/Rename/Delete]:").arg(layoutList());
}

std::optional<QString> LayoutCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    const QString upper = trimmed.toUpper();

    switch (m_stage) {
    case Stage::Option:
        if (upper.isEmpty()) {
            m_finished = true;
            return QStringLiteral("*LAYOUT done*");
        }
        if (upper == QLatin1String("N") || upper == QLatin1String("NEW")) {
            m_stage = Stage::NewName;
            return QStringLiteral("New layout name <%1>:")
                .arg(uniqueName(QStringLiteral("Layout%1").arg(m_document.layouts().size() + 1)));
        }
        if (upper == QLatin1String("C") || upper == QLatin1String("COPY")) {
            m_stage = Stage::CopyName;
            return QStringLiteral("Name for the copy <%1>:")
                .arg(uniqueName(QString::fromStdString(
                         m_document.layouts()[std::max(0, m_activeIndex)].name) + QStringLiteral(" copy")));
        }
        if (upper == QLatin1String("R") || upper == QLatin1String("RENAME")) {
            m_stage = Stage::RenameOld;
            return QStringLiteral("Layout to rename:");
        }
        if (upper == QLatin1String("D") || upper == QLatin1String("DELETE")) {
            m_stage = Stage::DeleteName;
            return QStringLiteral("Layout to delete:");
        }
        return QStringLiteral("*Invalid option* [New/Copy/Rename/Delete]:");
    case Stage::NewName: {
        const QString name =
            uniqueName(trimmed.isEmpty() ? QStringLiteral("Layout%1").arg(m_document.layouts().size() + 1) : trimmed);
        std::vector<lcad::Layout> layouts = m_document.layouts();
        lcad::Layout layout;
        layout.name = name.toStdString();
        layouts.push_back(layout);
        m_document.commandStack().execute(std::make_unique<lcad::SetLayoutsCommand>(m_document, std::move(layouts)));
        m_finished = true;
        return QStringLiteral("*Layout \"%1\" created*").arg(name);
    }
    case Stage::CopyName: {
        const lcad::Layout& source = m_document.layouts()[std::max(0, m_activeIndex)];
        const QString name = uniqueName(
            trimmed.isEmpty() ? QString::fromStdString(source.name) + QStringLiteral(" copy") : trimmed);
        lcad::Layout copy;
        copy.name = name.toStdString();
        copy.paperWidth = source.paperWidth;
        copy.paperHeight = source.paperHeight;
        copy.viewports = source.viewports;
        // Paper entities stay with the source; only the sheet setup copies.
        std::vector<lcad::Layout> layouts = m_document.layouts();
        layouts.push_back(copy);
        m_document.commandStack().execute(std::make_unique<lcad::SetLayoutsCommand>(m_document, std::move(layouts)));
        m_finished = true;
        return QStringLiteral("*Layout \"%1\" created from \"%2\" (sheet and viewports; entities not copied)*")
            .arg(name, QString::fromStdString(source.name));
    }
    case Stage::DeleteName: {
        const int index = findLayout(trimmed);
        if (index < 0) return QStringLiteral("*No layout named \"%1\"*\nLayout to delete:").arg(trimmed);
        if (m_document.layouts().size() == 1) {
            m_finished = true;
            return QStringLiteral("*Cannot delete the last layout*");
        }
        const QString name = QString::fromStdString(m_document.layouts()[index].name);
        m_document.commandStack().execute(std::make_unique<lcad::DeleteLayoutCommand>(m_document, index));
        m_finished = true;
        return QStringLiteral("*Layout \"%1\" deleted*").arg(name);
    }
    case Stage::RenameOld: {
        m_renameIndex = findLayout(trimmed);
        if (m_renameIndex < 0) return QStringLiteral("*No layout named \"%1\"*\nLayout to rename:").arg(trimmed);
        m_stage = Stage::RenameNew;
        return QStringLiteral("New name:");
    }
    case Stage::RenameNew: {
        if (trimmed.isEmpty()) return QStringLiteral("New name:");
        const QString name = uniqueName(trimmed);
        std::vector<lcad::Layout> layouts = m_document.layouts();
        layouts[static_cast<std::size_t>(m_renameIndex)].name = name.toStdString();
        m_document.commandStack().execute(std::make_unique<lcad::SetLayoutsCommand>(m_document, std::move(layouts)));
        m_finished = true;
        return QStringLiteral("*Renamed to \"%1\"*").arg(name);
    }
    }
    return std::nullopt;
}
