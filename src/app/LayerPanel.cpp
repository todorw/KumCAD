#include "LayerPanel.h"

#include <QAction>
#include <QFont>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Small preset palette to auto-assign colors to new layers, cycling by index.
const lcad::Color kPalette[] = {
    {255, 255, 255}, {255, 90, 90}, {90, 200, 255}, {120, 220, 120},
    {255, 200, 80},  {200, 130, 255}, {255, 140, 190}, {130, 220, 220},
};

QIcon swatchIcon(const lcad::Color& color) {
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(QColor(0, 0, 0, 80));
    painter.setBrush(QColor(color.r, color.g, color.b));
    painter.drawRect(0, 0, 13, 13);
    return QIcon(pixmap);
}

} // namespace

LayerPanel::LayerPanel(lcad::Document& document, QWidget* parent) : QWidget(parent), m_document(document) {
    m_list = new QListWidget(this);

    auto* addButton = new QPushButton(QStringLiteral("Add Layer"), this);
    connect(addButton, &QPushButton::clicked, this, &LayerPanel::onAddLayer);
    connect(m_list, &QListWidget::itemChanged, this, &LayerPanel::onItemChanged);
    connect(m_list, &QListWidget::currentRowChanged, this, &LayerPanel::onCurrentRowChanged);

    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &LayerPanel::onContextMenuRequested);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_list);
    layout->addWidget(addButton);

    refresh();
}

void LayerPanel::refresh() {
    m_updating = true;
    m_list->clear();

    int currentRow = 0;
    const auto& layers = m_document.layers();
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const lcad::Layer& layer = layers[i];
        QString text = QString::fromStdString(layer.name);
        if (layer.linetype != lcad::LineType::Continuous) {
            text += QStringLiteral(" [%1]").arg(QLatin1String(lcad::lineTypeName(layer.linetype)));
        }
        if (layer.locked) text += QStringLiteral(" (locked)");
        auto* item = new QListWidgetItem(swatchIcon(layer.color), text);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, static_cast<qulonglong>(layer.id));
        if (layer.locked) {
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
        }
        m_list->addItem(item);
        if (layer.id == m_document.currentLayer()) currentRow = static_cast<int>(i);
    }
    m_list->setCurrentRow(currentRow);
    m_updating = false;
}

void LayerPanel::onAddLayer() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Add Layer"), QStringLiteral("Layer name:"),
                                                 QLineEdit::Normal, QStringLiteral("Layer"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const lcad::Color color = kPalette[m_document.layers().size() % (sizeof(kPalette) / sizeof(kPalette[0]))];
    const lcad::LayerId id = m_document.addLayer(name.trimmed().toStdString(), color);
    m_document.setCurrentLayer(id);
    refresh();
    emit layersChanged();
}

void LayerPanel::onItemChanged(QListWidgetItem* item) {
    if (m_updating) return;
    const lcad::LayerId id = static_cast<lcad::LayerId>(item->data(Qt::UserRole).toULongLong());
    if (lcad::Layer* layer = m_document.findLayer(id)) {
        layer->visible = (item->checkState() == Qt::Checked);
        emit layersChanged();
    }
}

void LayerPanel::onCurrentRowChanged(int row) {
    if (m_updating || row < 0) return;
    QListWidgetItem* item = m_list->item(row);
    if (!item) return;
    const lcad::LayerId id = static_cast<lcad::LayerId>(item->data(Qt::UserRole).toULongLong());
    m_document.setCurrentLayer(id);
}

void LayerPanel::onContextMenuRequested(const QPoint& pos) {
    QListWidgetItem* item = m_list->itemAt(pos);
    if (!item) return;
    const lcad::LayerId id = static_cast<lcad::LayerId>(item->data(Qt::UserRole).toULongLong());
    lcad::Layer* layer = m_document.findLayer(id);
    if (!layer) return;

    QMenu menu(this);
    QAction* toggleLockAction = menu.addAction(layer->locked ? QStringLiteral("Unlock Layer") : QStringLiteral("Lock Layer"));
    QMenu* linetypeMenu = menu.addMenu(QStringLiteral("Linetype"));
    for (lcad::LineType type : lcad::allLineTypes()) {
        QAction* action = linetypeMenu->addAction(QLatin1String(lcad::lineTypeName(type)));
        action->setCheckable(true);
        action->setChecked(layer->linetype == type);
        action->setData(static_cast<int>(type));
    }
    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == toggleLockAction) {
        layer->locked = !layer->locked;
        refresh();
        emit layersChanged();
    } else if (chosen && chosen->parent() == linetypeMenu) {
        layer->linetype = static_cast<lcad::LineType>(chosen->data().toInt());
        refresh();
        emit layersChanged();
    }
}
