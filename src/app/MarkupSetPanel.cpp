#include "MarkupSetPanel.h"

#include "DrawingView.h"
#include "core/geometry/Leader.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"
#include "core/geometry/Text.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace {

QString oneLine(const std::string& text) {
    QString s = QString::fromStdString(text);
    s.replace(QLatin1Char('\n'), QStringLiteral(" / "));
    if (s.size() > 60) s = s.left(57) + QStringLiteral("...");
    return s.isEmpty() ? QStringLiteral("(empty)") : s;
}

} // namespace

MarkupSetPanel::MarkupSetPanel(lcad::Document& document, DrawingView& view, QWidget* parent)
    : QWidget(parent), m_document(document), m_view(view) {
    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &MarkupSetPanel::onItemDoubleClicked);

    auto* hint = new QLabel(
        QStringLiteral("This drawing's text/leader annotations. Double-click one to zoom to it."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888;"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_list);

    refresh();
}

void MarkupSetPanel::refresh() {
    m_list->clear();
    for (const lcad::Entity* e : m_document.entities()) {
        QString label;
        switch (e->type()) {
        case lcad::EntityType::Text:
            label = QStringLiteral("[Text] %1").arg(oneLine(static_cast<const lcad::TextEntity*>(e)->text()));
            break;
        case lcad::EntityType::MText:
            label = QStringLiteral("[MText] %1").arg(oneLine(static_cast<const lcad::MTextEntity*>(e)->text()));
            break;
        case lcad::EntityType::Leader:
            label = QStringLiteral("[Leader]");
            break;
        case lcad::EntityType::MLeader:
            label = QStringLiteral("[Multileader]");
            break;
        default:
            continue;
        }
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, static_cast<qulonglong>(e->id()));
    }
    if (m_list->count() == 0) {
        auto* placeholder = new QListWidgetItem(QStringLiteral("(no annotations in this drawing yet)"), m_list);
        placeholder->setFlags(Qt::NoItemFlags);
    }
}

void MarkupSetPanel::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    const auto id = static_cast<lcad::EntityId>(item->data(Qt::UserRole).toULongLong());
    if (id == 0) return;
    m_view.zoomToEntity(id);
}
