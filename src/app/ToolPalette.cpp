#include "ToolPalette.h"

#include "CommandDispatcher.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

ToolPalette::ToolPalette(lcad::Document& document, CommandDispatcher& dispatcher, QWidget* parent)
    : QWidget(parent), m_document(document), m_dispatcher(dispatcher) {
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemClicked, this, &ToolPalette::onItemClicked);

    auto* hint = new QLabel(QStringLiteral("Click a block, then pick its insertion point on the canvas."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888;"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_list);

    refresh();
}

void ToolPalette::refresh() {
    m_list->clear();
    for (const auto& block : m_document.blocks()) {
        const QString label =
            block->isXref() ? QString::fromStdString(block->name) + QStringLiteral(" (xref)")
                            : QString::fromStdString(block->name);
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, QString::fromStdString(block->name));
    }
    if (m_document.blocks().empty()) {
        auto* placeholder = new QListWidgetItem(QStringLiteral("(no blocks defined yet -- use BLOCK)"), m_list);
        placeholder->setFlags(Qt::NoItemFlags);
    }
}

void ToolPalette::onItemClicked(QListWidgetItem* item) {
    if (!item) return;
    const QString name = item->data(Qt::UserRole).toString();
    if (name.isEmpty()) return;
    m_dispatcher.handleCommandText(QStringLiteral("INSERT"));
    m_dispatcher.handleCommandText(name);
}
