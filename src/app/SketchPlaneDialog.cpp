#include "SketchPlaneDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>

SketchPlaneDialog::SketchPlaneDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("New Sketch"));

    m_planeCombo = new QComboBox(this);
    m_planeCombo->addItem(QStringLiteral("XY Plane"));
    m_planeCombo->addItem(QStringLiteral("XZ Plane"));
    m_planeCombo->addItem(QStringLiteral("YZ Plane"));

    m_offsetSpin = new QDoubleSpinBox(this);
    m_offsetSpin->setRange(-1e6, 1e6);
    m_offsetSpin->setValue(0.0);

    m_angleSpin = new QDoubleSpinBox(this);
    m_angleSpin->setRange(-360.0, 360.0);
    m_angleSpin->setValue(0.0);
    m_angleSpin->setSuffix(QStringLiteral(" deg"));

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Base plane:"), m_planeCombo);
    form->addRow(QStringLiteral("Offset:"), m_offsetSpin);
    form->addRow(QStringLiteral("Angle:"), m_angleSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

lcad::SketchPlane SketchPlaneDialog::plane() const {
    const double offset = m_offsetSpin->value();
    const double angle = m_angleSpin->value();
    switch (m_planeCombo->currentIndex()) {
    case 1: return lcad::SketchPlane::XZ(offset, angle);
    case 2: return lcad::SketchPlane::YZ(offset, angle);
    default: return lcad::SketchPlane::XY(offset, angle);
    }
}
