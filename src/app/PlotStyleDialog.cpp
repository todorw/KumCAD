#include "PlotStyleDialog.h"

#include "core/io/DxfColors.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

PlotStyleDialog::PlotStyleDialog(lcad::Document& document, QWidget* parent) : QDialog(parent), m_document(document) {
    setWindowTitle(QStringLiteral("Plot Style Table"));
    resize(340, 400);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("Named styles (STB)"));
    m_modeCombo->addItem(QStringLiteral("Color-dependent (CTB)"));
    m_modeCombo->setCurrentIndex(m_document.plotStyleMode() == lcad::PlotStyleMode::ColorDependent ? 1 : 0);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, &PlotStyleDialog::onModeChanged);

    m_list = new QListWidget(this);

    auto* newButton = new QPushButton(QStringLiteral("New..."), this);
    auto* editButton = new QPushButton(QStringLiteral("Edit..."), this);
    auto* deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);
    connect(newButton, &QPushButton::clicked, this, &PlotStyleDialog::onNew);
    connect(editButton, &QPushButton::clicked, this, &PlotStyleDialog::onEdit);
    connect(deleteButton, &QPushButton::clicked, this, &PlotStyleDialog::onDelete);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PlotStyleDialog::onEdit);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(newButton);
    buttons->addWidget(editButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_modeCombo);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    refresh();
}

bool PlotStyleDialog::colorDependent() const {
    return m_modeCombo->currentIndex() == 1;
}

void PlotStyleDialog::onModeChanged(int index) {
    m_document.setPlotStyleMode(index == 1 ? lcad::PlotStyleMode::ColorDependent : lcad::PlotStyleMode::Named);
    refresh();
}

void PlotStyleDialog::refresh() {
    m_list->clear();
    if (colorDependent()) {
        for (const lcad::CtbEntry& entry : m_document.ctbEntries()) {
            QString label = QStringLiteral("Color %1").arg(entry.aci);
            if (entry.screening < 100.0) label += QStringLiteral(" (screen %1%)").arg(entry.screening);
            auto* item = new QListWidgetItem(label, m_list);
            item->setData(Qt::UserRole, entry.aci);
            const lcad::Color swatch = lcad::aciToColor(entry.aci);
            item->setForeground(QColor(swatch.r, swatch.g, swatch.b));
        }
    } else {
        for (const lcad::PlotStyle& style : m_document.plotStyles()) {
            m_list->addItem(QString::fromStdString(style.name));
        }
    }
}

void PlotStyleDialog::onNew() {
    const bool changed = colorDependent() ? editCtbEntry(0) : editStyle(QString());
    if (changed) refresh();
}

void PlotStyleDialog::onEdit() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    const bool changed = colorDependent() ? editCtbEntry(item->data(Qt::UserRole).toInt()) : editStyle(item->text());
    if (changed) refresh();
}

void PlotStyleDialog::onDelete() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    if (QMessageBox::question(this, QStringLiteral("Delete Plot Style"),
                               QStringLiteral("Delete \"%1\"?").arg(item->text()))
        != QMessageBox::Yes) {
        return;
    }
    if (colorDependent()) {
        m_document.deleteCtbEntry(item->data(Qt::UserRole).toInt());
    } else {
        m_document.deletePlotStyle(item->text().toStdString());
    }
    refresh();
}

namespace {

// The override rows (color/lineweight/linetype/screening) shared by the
// named-style and CTB editors, bound to the caller's widgets.
struct OverrideWidgets {
    QCheckBox* colorCheck;
    QPushButton* colorButton;
    QCheckBox* weightCheck;
    QDoubleSpinBox* weightSpin;
    QCheckBox* typeCheck;
    QComboBox* typeCombo;
    QDoubleSpinBox* screenSpin;
};

OverrideWidgets buildOverrideRows(QDialog& dlg, QFormLayout* form, lcad::Color& pickedColor, bool hasColor,
                                  std::optional<double> lineweight, std::optional<lcad::LineType> linetype,
                                  double screening) {
    OverrideWidgets w{};
    w.colorCheck = new QCheckBox(QStringLiteral("Override color"), &dlg);
    w.colorCheck->setChecked(hasColor);
    w.colorButton = new QPushButton(QStringLiteral("Pick..."), &dlg);
    QObject::connect(w.colorButton, &QPushButton::clicked, &dlg, [&dlg, &pickedColor, w]() {
        const QColor picked = QColorDialog::getColor(QColor(pickedColor.r, pickedColor.g, pickedColor.b), &dlg,
                                                      QStringLiteral("Plot Color"));
        if (!picked.isValid()) return;
        pickedColor = lcad::Color{static_cast<unsigned char>(picked.red()), static_cast<unsigned char>(picked.green()),
                                  static_cast<unsigned char>(picked.blue())};
        w.colorCheck->setChecked(true);
        w.colorButton->setStyleSheet(QStringLiteral("background-color: %1").arg(picked.name()));
    });
    w.colorButton->setStyleSheet(
        QStringLiteral("background-color: rgb(%1,%2,%3)").arg(pickedColor.r).arg(pickedColor.g).arg(pickedColor.b));

    w.weightCheck = new QCheckBox(QStringLiteral("Override lineweight (mm)"), &dlg);
    w.weightCheck->setChecked(lineweight.has_value());
    w.weightSpin = new QDoubleSpinBox(&dlg);
    w.weightSpin->setRange(0.0, 5.0);
    w.weightSpin->setSingleStep(0.05);
    w.weightSpin->setValue(lineweight.value_or(0.25));

    w.typeCheck = new QCheckBox(QStringLiteral("Override linetype"), &dlg);
    w.typeCheck->setChecked(linetype.has_value());
    w.typeCombo = new QComboBox(&dlg);
    for (lcad::LineType type : lcad::allLineTypes()) {
        w.typeCombo->addItem(QLatin1String(lcad::lineTypeName(type)), static_cast<int>(type));
    }
    if (linetype) {
        const int idx = w.typeCombo->findData(static_cast<int>(*linetype));
        if (idx >= 0) w.typeCombo->setCurrentIndex(idx);
    }

    w.screenSpin = new QDoubleSpinBox(&dlg);
    w.screenSpin->setRange(0.0, 100.0);
    w.screenSpin->setSuffix(QStringLiteral(" %"));
    w.screenSpin->setValue(screening);

    auto* colorRow = new QHBoxLayout();
    colorRow->addWidget(w.colorCheck);
    colorRow->addWidget(w.colorButton);
    form->addRow(colorRow);
    auto* weightRow = new QHBoxLayout();
    weightRow->addWidget(w.weightCheck);
    weightRow->addWidget(w.weightSpin);
    form->addRow(weightRow);
    auto* typeRow = new QHBoxLayout();
    typeRow->addWidget(w.typeCheck);
    typeRow->addWidget(w.typeCombo);
    form->addRow(typeRow);
    form->addRow(QStringLiteral("Screening:"), w.screenSpin);
    return w;
}

} // namespace

bool PlotStyleDialog::editStyle(const QString& existingName) {
    const bool isNew = existingName.isEmpty();
    lcad::PlotStyle style;
    style.name = existingName.toStdString();
    if (!isNew) {
        if (const lcad::PlotStyle* found = m_document.findPlotStyle(style.name)) style = *found;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(isNew ? QStringLiteral("New Plot Style") : QStringLiteral("Edit Plot Style"));

    auto* nameEdit = new QLineEdit(QString::fromStdString(style.name), &dlg);
    nameEdit->setEnabled(isNew);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Name:"), nameEdit);
    lcad::Color pickedColor = style.color.value_or(lcad::Color{255, 255, 255});
    const OverrideWidgets w =
        buildOverrideRows(dlg, form, pickedColor, style.color.has_value(), style.lineweight, style.linetype,
                          style.screening);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* dlgLayout = new QVBoxLayout(&dlg);
    dlgLayout->addLayout(form);
    dlgLayout->addWidget(buttonBox);

    if (dlg.exec() != QDialog::Accepted) return false;

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) return false;

    lcad::PlotStyle result;
    result.name = name.toStdString();
    if (w.colorCheck->isChecked()) result.color = pickedColor;
    if (w.weightCheck->isChecked()) result.lineweight = w.weightSpin->value();
    if (w.typeCheck->isChecked()) result.linetype = static_cast<lcad::LineType>(w.typeCombo->currentData().toInt());
    result.screening = w.screenSpin->value();

    m_document.savePlotStyle(std::move(result));
    return true;
}

bool PlotStyleDialog::editCtbEntry(int aci) {
    const bool isNew = aci <= 0;
    lcad::CtbEntry entry;
    if (!isNew) {
        if (const lcad::CtbEntry* found = m_document.findCtbEntry(aci)) entry = *found;
        else entry.aci = aci;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(isNew ? QStringLiteral("New CTB Entry") : QStringLiteral("Edit CTB Entry"));

    auto* aciSpin = new QSpinBox(&dlg);
    aciSpin->setRange(1, 255);
    aciSpin->setValue(isNew ? 1 : entry.aci);
    aciSpin->setEnabled(isNew);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Color index (ACI):"), aciSpin);
    lcad::Color pickedColor = entry.color.value_or(lcad::Color{0, 0, 0});
    const OverrideWidgets w = buildOverrideRows(dlg, form, pickedColor, entry.color.has_value(), entry.lineweight,
                                                entry.linetype, entry.screening);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* dlgLayout = new QVBoxLayout(&dlg);
    dlgLayout->addLayout(form);
    dlgLayout->addWidget(buttonBox);

    if (dlg.exec() != QDialog::Accepted) return false;

    lcad::CtbEntry result;
    result.aci = aciSpin->value();
    if (w.colorCheck->isChecked()) result.color = pickedColor;
    if (w.weightCheck->isChecked()) result.lineweight = w.weightSpin->value();
    if (w.typeCheck->isChecked()) result.linetype = static_cast<lcad::LineType>(w.typeCombo->currentData().toInt());
    result.screening = w.screenSpin->value();

    m_document.saveCtbEntry(result);
    return true;
}
