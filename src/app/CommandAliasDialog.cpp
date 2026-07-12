#include "CommandAliasDialog.h"

#include "CommandAliases.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CommandAliasDialog::CommandAliasDialog(CommandAliases& aliases, QWidget* parent)
    : QDialog(parent), m_aliases(aliases) {
    setWindowTitle(QStringLiteral("Customize Command Aliases"));
    resize(340, 380);

    m_list = new QListWidget(this);

    auto* addButton = new QPushButton(QStringLiteral("Add..."), this);
    auto* editButton = new QPushButton(QStringLiteral("Edit..."), this);
    auto* deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);
    connect(addButton, &QPushButton::clicked, this, &CommandAliasDialog::onAdd);
    connect(editButton, &QPushButton::clicked, this, &CommandAliasDialog::onEdit);
    connect(deleteButton, &QPushButton::clicked, this, &CommandAliasDialog::onDelete);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &CommandAliasDialog::onEdit);

    auto* hint = new QLabel(
        QStringLiteral("Type the alias at the command line to run the command it points to, "
                       "e.g. \"MY\" for MOVE. Saved to a file like AutoCAD's acad.pgp."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888;"));

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    refresh();
}

void CommandAliasDialog::refresh() {
    m_list->clear();
    for (auto it = m_aliases.all().constBegin(); it != m_aliases.all().constEnd(); ++it) {
        m_list->addItem(QStringLiteral("%1  ->  %2").arg(it.key(), it.value()));
    }
}

void CommandAliasDialog::onAdd() {
    if (editAlias(QString())) refresh();
}

void CommandAliasDialog::onEdit() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    const QString alias = item->text().section(QStringLiteral("  ->  "), 0, 0);
    if (editAlias(alias)) refresh();
}

void CommandAliasDialog::onDelete() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    const QString alias = item->text().section(QStringLiteral("  ->  "), 0, 0);
    if (QMessageBox::question(this, QStringLiteral("Delete Alias"), QStringLiteral("Delete alias \"%1\"?").arg(alias))
        != QMessageBox::Yes) {
        return;
    }
    m_aliases.remove(alias);
    refresh();
}

bool CommandAliasDialog::editAlias(const QString& existingAlias) {
    const bool isNew = existingAlias.isEmpty();

    QDialog dlg(this);
    dlg.setWindowTitle(isNew ? QStringLiteral("Add Alias") : QStringLiteral("Edit Alias"));

    auto* aliasEdit = new QLineEdit(existingAlias, &dlg);
    aliasEdit->setEnabled(isNew);
    auto* commandEdit = new QLineEdit(&dlg);
    if (!isNew) {
        if (const auto command = m_aliases.resolve(existingAlias)) commandEdit->setText(*command);
    }
    commandEdit->setPlaceholderText(QStringLiteral("e.g. MOVE"));

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Alias:"), aliasEdit);
    form->addRow(QStringLiteral("Command:"), commandEdit);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* dlgLayout = new QVBoxLayout(&dlg);
    dlgLayout->addLayout(form);
    dlgLayout->addWidget(buttonBox);

    if (dlg.exec() != QDialog::Accepted) return false;

    const QString alias = aliasEdit->text().trimmed();
    const QString command = commandEdit->text().trimmed();
    if (alias.isEmpty() || command.isEmpty()) return false;

    m_aliases.set(alias, command);
    return true;
}
