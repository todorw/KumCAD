#include "SketchFeatureDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

using lcad::Feature3D;
using lcad::FeatureType;

namespace {
QDoubleSpinBox* makeSpin(double lo, double hi, double value) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(lo, hi);
    spin->setDecimals(3);
    spin->setValue(value);
    return spin;
}
} // namespace

SketchFeatureDialog::SketchFeatureDialog(const lcad::Document3D& document, QWidget* parent)
    : QDialog(parent), m_document(document) {
    setWindowTitle(QStringLiteral("Add Sketch-Based / Derived Feature"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(QStringLiteral("Pad / Pocket"), static_cast<int>(FeatureType::Pad));
    m_typeCombo->addItem(QStringLiteral("Revolve / Groove"), static_cast<int>(FeatureType::Revolve));
    m_typeCombo->addItem(QStringLiteral("Fillet"), static_cast<int>(FeatureType::Fillet));
    m_typeCombo->addItem(QStringLiteral("Chamfer"), static_cast<int>(FeatureType::Chamfer));
    m_typeCombo->addItem(QStringLiteral("Linear Pattern"), static_cast<int>(FeatureType::LinearPattern));
    m_typeCombo->addItem(QStringLiteral("Polar Pattern"), static_cast<int>(FeatureType::PolarPattern));
    m_typeCombo->addItem(QStringLiteral("Mirror"), static_cast<int>(FeatureType::Mirror));
    m_typeCombo->addItem(QStringLiteral("Shell"), static_cast<int>(FeatureType::Shell));
    m_typeCombo->addItem(QStringLiteral("Loft"), static_cast<int>(FeatureType::Loft));
    form->addRow(QStringLiteral("Type:"), m_typeCombo);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &SketchFeatureDialog::updateHint);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    layout->insertWidget(1, m_hintLabel);

    m_sketchCombo = new QComboBox(this);
    for (std::size_t i = 0; i < document.sketches().size(); ++i) {
        m_sketchCombo->addItem(QStringLiteral("Sketch %1").arg(i), static_cast<int>(i));
    }
    form->addRow(QStringLiteral("Sketch (Pad/Revolve):"), m_sketchCombo);

    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItem(QStringLiteral("(none)"), -1);
    for (std::size_t i = 0; i < document.features().size(); ++i) {
        if (document.isValid(static_cast<int>(i))) m_targetCombo->addItem(QStringLiteral("Feature %1").arg(i), static_cast<int>(i));
    }
    form->addRow(QStringLiteral("Target (Fillet/Pattern/Mirror require one):"), m_targetCombo);

    m_cutModeCheck = new QCheckBox(QStringLiteral("Cut from target (Pocket/Groove) instead of fusing into it"), this);
    form->addRow(QString(), m_cutModeCheck);

    m_p1Spin = makeSpin(-1e6, 1e6, 10.0);
    form->addRow(QStringLiteral("Height / Angle° / Radius / Spacing:"), m_p1Spin);

    m_countSpin = new QSpinBox(this);
    m_countSpin->setRange(1, 1000);
    m_countSpin->setValue(3);
    form->addRow(QStringLiteral("Pattern count:"), m_countSpin);

    m_posX = makeSpin(-1e6, 1e6, 0.0);
    m_posY = makeSpin(-1e6, 1e6, 0.0);
    m_posZ = makeSpin(-1e6, 1e6, 0.0);
    form->addRow(QStringLiteral("Position/Axis point X:"), m_posX);
    form->addRow(QStringLiteral("Position/Axis point Y:"), m_posY);
    form->addRow(QStringLiteral("Position/Axis point Z:"), m_posZ);

    m_dirX = makeSpin(-1.0, 1.0, 0.0);
    m_dirY = makeSpin(-1.0, 1.0, 0.0);
    m_dirZ = makeSpin(-1.0, 1.0, 1.0);
    form->addRow(QStringLiteral("Direction/Axis/Normal X:"), m_dirX);
    form->addRow(QStringLiteral("Direction/Axis/Normal Y:"), m_dirY);
    form->addRow(QStringLiteral("Direction/Axis/Normal Z:"), m_dirZ);

    m_edgeIndices = new QLineEdit(this);
    m_edgeIndices->setPlaceholderText(QStringLiteral("blank = every edge"));
    form->addRow(QStringLiteral("Fillet/Chamfer Edge Indices (comma-separated, see Pick3D.h):"), m_edgeIndices);

    m_faceIndices = new QLineEdit(this);
    m_faceIndices->setPlaceholderText(QStringLiteral("required -- which face(s) to open, see Pick3D.h's pickFace"));
    form->addRow(QStringLiteral("Shell Face Indices (comma-separated):"), m_faceIndices);

    m_sketchIndices = new QLineEdit(this);
    m_sketchIndices->setPlaceholderText(QStringLiteral("required, 2+, in order -- e.g. 0,1,2"));
    form->addRow(QStringLiteral("Loft Sketch Indices (comma-separated):"), m_sketchIndices);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateHint();
}

void SketchFeatureDialog::updateHint() {
    const auto type = static_cast<FeatureType>(m_typeCombo->currentData().toInt());
    switch (type) {
    case FeatureType::Pad:
        m_hintLabel->setText(QStringLiteral("Pad: extrudes Sketch by Height along Direction, starting at Position. "
                                            "Leave Target as (none) for a standalone solid, or pick one to fuse "
                                            "into it (or cut from it, with Cut Mode, for a Pocket)."));
        break;
    case FeatureType::Revolve:
        m_hintLabel->setText(QStringLiteral("Revolve: sweeps Sketch by Angle° around the axis through Position with "
                                            "direction Axis. Target/Cut Mode work the same as Pad (a Groove is "
                                            "Revolve with Cut Mode)."));
        break;
    case FeatureType::Fillet:
        m_hintLabel->setText(QStringLiteral("Fillet: rounds Target's edges by Radius -- every edge if Edge Indices "
                                            "is blank, or just the listed ones (find indices via a pick, see "
                                            "Pick3D.h's pickEdge)."));
        break;
    case FeatureType::Chamfer:
        m_hintLabel->setText(QStringLiteral("Chamfer: bevels Target's edges by the Height/Angle/Radius/Spacing "
                                            "field's value -- same Edge Indices convention as Fillet."));
        break;
    case FeatureType::LinearPattern:
        m_hintLabel->setText(QStringLiteral("Linear Pattern: replicates Target Count times along Direction, spaced "
                                            "by Spacing, fused into one shape."));
        break;
    case FeatureType::PolarPattern:
        m_hintLabel->setText(QStringLiteral("Polar Pattern: replicates Target Count times around the axis through "
                                            "Position with direction Axis, spread over Angle° total."));
        break;
    case FeatureType::Mirror:
        m_hintLabel->setText(QStringLiteral("Mirror: reflects Target across the plane through Position with normal "
                                            "Normal, fused with the original."));
        break;
    case FeatureType::Shell:
        m_hintLabel->setText(QStringLiteral("Shell: hollows Target to wall thickness Radius, opening it up through "
                                            "the listed Face Indices (required -- find them via a pick, see "
                                            "Pick3D.h's pickFace)."));
        break;
    case FeatureType::Loft:
        m_hintLabel->setText(QStringLiteral("Loft: builds a solid through the listed Sketch Indices (2+, in order) "
                                            "spread evenly along Height."));
        break;
    default:
        break;
    }
}

Feature3D SketchFeatureDialog::result() const {
    Feature3D f;
    f.type = static_cast<FeatureType>(m_typeCombo->currentData().toInt());
    f.sketchIndex = m_sketchCombo->currentIndex() >= 0 ? m_sketchCombo->currentData().toInt() : -1;
    f.inputA = m_targetCombo->currentData().toInt();
    f.cutMode = m_cutModeCheck->isChecked();
    f.p1 = m_p1Spin->value();
    f.count = m_countSpin->value();
    f.posX = m_posX->value();
    f.posY = m_posY->value();
    f.posZ = m_posZ->value();
    f.dirX = m_dirX->value();
    f.dirY = m_dirY->value();
    f.dirZ = m_dirZ->value();
    for (const QString& token : m_edgeIndices->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (ok) f.edgeIndices.push_back(value);
    }
    for (const QString& token : m_faceIndices->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (ok) f.faceIndices.push_back(value);
    }
    for (const QString& token : m_sketchIndices->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (ok) f.sketchIndices.push_back(value);
    }
    return f;
}
