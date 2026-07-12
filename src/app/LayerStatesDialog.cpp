#include "LayerStatesDialog.h"

#include "core/document/Commands.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

LayerStatesDialog::LayerStatesDialog(lcad::Document& document, QWidget* parent)
    : QDialog(parent), m_document(document) {
    setWindowTitle(QStringLiteral("Layer States Manager"));
    resize(320, 360);

    m_list = new QListWidget(this);

    auto* saveButton = new QPushButton(QStringLiteral("Save New..."), this);
    auto* restoreButton = new QPushButton(QStringLiteral("Restore"), this);
    auto* deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);
    connect(saveButton, &QPushButton::clicked, this, &LayerStatesDialog::onSaveNew);
    connect(restoreButton, &QPushButton::clicked, this, &LayerStatesDialog::onRestore);
    connect(deleteButton, &QPushButton::clicked, this, &LayerStatesDialog::onDelete);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &LayerStatesDialog::onRestore);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(saveButton);
    buttons->addWidget(restoreButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    refresh();
}

void LayerStatesDialog::refresh() {
    m_list->clear();
    for (const lcad::LayerState& state : m_document.layerStates()) {
        m_list->addItem(QString::fromStdString(state.name));
    }
}

void LayerStatesDialog::onSaveNew() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Layer State"), QStringLiteral("State name:"),
                                                 QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    m_document.saveLayerState(name.trimmed().toStdString());
    refresh();
}

void LayerStatesDialog::onRestore() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    const std::string name = item->text().toStdString();
    for (const lcad::LayerState& state : m_document.layerStates()) {
        if (state.name == name) {
            m_document.commandStack().execute(std::make_unique<lcad::RestoreLayerStateCommand>(m_document, state));
            emit layerStateApplied();
            return;
        }
    }
}

void LayerStatesDialog::onDelete() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    const std::string name = item->text().toStdString();
    if (QMessageBox::question(this, QStringLiteral("Delete Layer State"),
                               QStringLiteral("Delete layer state \"%1\"?").arg(item->text()))
        != QMessageBox::Yes) {
        return;
    }
    m_document.deleteLayerState(name);
    refresh();
}
