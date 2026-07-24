#include "SketchFeatureDialog.h"

#include "core/core3d/TopoNaming.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
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
    m_typeCombo->addItem(QStringLiteral("Scaled Pattern"), static_cast<int>(FeatureType::ScaledPattern));
    m_typeCombo->addItem(QStringLiteral("Mirror"), static_cast<int>(FeatureType::Mirror));
    m_typeCombo->addItem(QStringLiteral("Shell"), static_cast<int>(FeatureType::Shell));
    m_typeCombo->addItem(QStringLiteral("Loft"), static_cast<int>(FeatureType::Loft));
    m_typeCombo->addItem(QStringLiteral("Sweep"), static_cast<int>(FeatureType::Sweep));
    m_typeCombo->addItem(QStringLiteral("Draft"), static_cast<int>(FeatureType::Draft));
    m_typeCombo->addItem(QStringLiteral("Hole"), static_cast<int>(FeatureType::Hole));
    form->addRow(QStringLiteral("Type:"), m_typeCombo);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &SketchFeatureDialog::updateHint);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    layout->insertWidget(1, m_hintLabel);

    m_sketchCombo = new QComboBox(this);
    for (std::size_t i = 0; i < document.sketches().size(); ++i) {
        m_sketchCombo->addItem(QStringLiteral("Sketch %1").arg(i), static_cast<int>(i));
    }
    form->addRow(QStringLiteral("Sketch (Pad/Revolve/Sweep profile):"), m_sketchCombo);

    m_pathSketchCombo = new QComboBox(this);
    for (std::size_t i = 0; i < document.sketches().size(); ++i) {
        m_pathSketchCombo->addItem(QStringLiteral("Sketch %1").arg(i), static_cast<int>(i));
    }
    form->addRow(QStringLiteral("Path Sketch (Sweep only, one straight line):"), m_pathSketchCombo);

    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItem(QStringLiteral("(none)"), -1);
    for (std::size_t i = 0; i < document.features().size(); ++i) {
        if (document.isValid(static_cast<int>(i))) m_targetCombo->addItem(QStringLiteral("Feature %1").arg(i), static_cast<int>(i));
    }
    form->addRow(QStringLiteral("Target (Fillet/Pattern/Mirror require one):"), m_targetCombo);

    m_cutModeCheck = new QCheckBox(QStringLiteral("Cut from target (Pocket/Groove) instead of fusing into it"), this);
    form->addRow(QString(), m_cutModeCheck);

    m_p1Spin = makeSpin(-1e6, 1e6, 10.0);
    form->addRow(QStringLiteral("Height / Angle° / Radius / Spacing / Hole Diameter:"), m_p1Spin);

    m_p2Spin = makeSpin(-1e6, 1e6, 0.0);
    form->addRow(QStringLiteral("Hole Depth:"), m_p2Spin);
    m_p3Spin = makeSpin(-1e6, 1e6, 0.0);
    form->addRow(QStringLiteral("Hole Counterbore/sink Diameter:"), m_p3Spin);
    m_p4Spin = makeSpin(-1e6, 1e6, 0.0);
    form->addRow(QStringLiteral("Hole Counterbore Depth / Countersink Angle:"), m_p4Spin);

    m_countSpin = new QSpinBox(this);
    // Range starts at 0 so Hole's own reuse (0=Simple/1=Counterbore/
    // 2=Countersink, see FeatureType::Hole's own comment) is reachable;
    // a pattern count of 0 is simply never a meaningful value to set.
    m_countSpin->setRange(0, 1000);
    m_countSpin->setValue(3);
    form->addRow(QStringLiteral("Pattern Count / Hole Type (0=Simple,1=Counterbore,2=Countersink):"), m_countSpin);

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

    auto* axisFromEdgeButton = new QPushButton(QStringLiteral("Set Position/Direction from Target's Edge..."), this);
    form->addRow(QString(), axisFromEdgeButton);
    connect(axisFromEdgeButton, &QPushButton::clicked, this, [this] {
        const int targetIndex = m_targetCombo->currentData().toInt();
        if (targetIndex < 0 || !m_document.isValid(targetIndex)) {
            QMessageBox::warning(this, QStringLiteral("Set Axis from Edge"),
                                  QStringLiteral("Pick a valid Target feature first."));
            return;
        }
        bool ok = false;
        const int edgeIndex = QInputDialog::getInt(
            this, QStringLiteral("Set Axis from Edge"),
            QStringLiteral("Edge index on the Target feature (see Window3D's List Edges...):"), 0, 0, 9999, 1, &ok);
        if (!ok) return;

        const auto axis = lcad::axisFromEdge(m_document.shapeAt(targetIndex), edgeIndex);
        if (!axis) {
            QMessageBox::warning(this, QStringLiteral("Set Axis from Edge"),
                                  QStringLiteral("That edge isn't straight or doesn't exist -- pick a straight edge."));
            return;
        }
        m_posX->setValue(axis->pointX);
        m_posY->setValue(axis->pointY);
        m_posZ->setValue(axis->pointZ);
        m_dirX->setValue(axis->dirX);
        m_dirY->setValue(axis->dirY);
        m_dirZ->setValue(axis->dirZ);
    });

    auto* pointFromVertexButton = new QPushButton(QStringLiteral("Set Position from Target's Vertex..."), this);
    form->addRow(QString(), pointFromVertexButton);
    connect(pointFromVertexButton, &QPushButton::clicked, this, [this] {
        const int targetIndex = m_targetCombo->currentData().toInt();
        if (targetIndex < 0 || !m_document.isValid(targetIndex)) {
            QMessageBox::warning(this, QStringLiteral("Set Position from Vertex"),
                                  QStringLiteral("Pick a valid Target feature first."));
            return;
        }
        bool ok = false;
        const int vertexIndex = QInputDialog::getInt(this, QStringLiteral("Set Position from Vertex"),
                                                      QStringLiteral("Vertex index on the Target feature:"), 0, 0,
                                                      9999, 1, &ok);
        if (!ok) return;

        const auto point = lcad::pointFromVertex(m_document.shapeAt(targetIndex), vertexIndex);
        if (!point) {
            QMessageBox::warning(this, QStringLiteral("Set Position from Vertex"),
                                  QStringLiteral("That vertex doesn't exist."));
            return;
        }
        m_posX->setValue(point->x);
        m_posY->setValue(point->y);
        m_posZ->setValue(point->z);
    });

    auto* centerFromCircularEdgeButton =
        new QPushButton(QStringLiteral("Set Position/Direction from Target's Circular Edge..."), this);
    form->addRow(QString(), centerFromCircularEdgeButton);
    connect(centerFromCircularEdgeButton, &QPushButton::clicked, this, [this] {
        const int targetIndex = m_targetCombo->currentData().toInt();
        if (targetIndex < 0 || !m_document.isValid(targetIndex)) {
            QMessageBox::warning(this, QStringLiteral("Set Position from Circular Edge"),
                                  QStringLiteral("Pick a valid Target feature first."));
            return;
        }
        bool ok = false;
        const int edgeIndex = QInputDialog::getInt(
            this, QStringLiteral("Set Position from Circular Edge"),
            QStringLiteral("Edge index on the Target feature (see Window3D's List Edges...):"), 0, 0, 9999, 1, &ok);
        if (!ok) return;

        const auto circle = lcad::centerOfCircularEdge(m_document.shapeAt(targetIndex), edgeIndex);
        if (!circle) {
            QMessageBox::warning(
                this, QStringLiteral("Set Position from Circular Edge"),
                QStringLiteral("That edge isn't circular or doesn't exist -- pick a hole/round edge."));
            return;
        }
        m_posX->setValue(circle->centerX);
        m_posY->setValue(circle->centerY);
        m_posZ->setValue(circle->centerZ);
        m_dirX->setValue(circle->normalX);
        m_dirY->setValue(circle->normalY);
        m_dirZ->setValue(circle->normalZ);
    });

    m_edgeIndices = new QLineEdit(this);
    m_edgeIndices->setPlaceholderText(QStringLiteral("blank = every edge"));
    form->addRow(QStringLiteral("Fillet/Chamfer Edge Indices (comma-separated, see Pick3D.h):"), m_edgeIndices);

    m_faceIndices = new QLineEdit(this);
    m_faceIndices->setPlaceholderText(QStringLiteral("required -- which face(s) to open, see Pick3D.h's pickFace"));
    form->addRow(QStringLiteral("Shell/Draft Face Indices (comma-separated):"), m_faceIndices);

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
    case FeatureType::ScaledPattern:
        m_hintLabel->setText(QStringLiteral("Scaled Pattern: replicates Target Count times, each copy uniformly "
                                            "scaled about Position from 1.0 up to the Height/Angle/Radius field's "
                                            "value at the last copy, fused into one shape."));
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
                                            "spread evenly along Height. Cut Mode is reused here as ruled (checked) "
                                            "vs. smooth/BSpline-blended (unchecked, the default) between profiles."));
        break;
    case FeatureType::Sweep:
        m_hintLabel->setText(QStringLiteral("Sweep: sweeps Sketch (the profile) along Path Sketch's own path -- "
                                            "any chained mix of straight lines and arcs, not just one line."));
        break;
    case FeatureType::Draft:
        m_hintLabel->setText(QStringLiteral("Draft: adds a Radius-field-as-degrees taper to Target's listed Face "
                                            "Indices, pulled along Normal, relative to the neutral plane through "
                                            "Position with that same direction as its own normal."));
        break;
    case FeatureType::Hole:
        m_hintLabel->setText(
            QStringLiteral("Hole: drills into Target at Position along Direction. Diameter/Depth fields set the "
                          "main hole; Pattern Count/Hole Type selects Simple(0)/Counterbore(1)/Countersink(2) "
                          "-- Counterbore uses Counterbore Diameter+Depth, Countersink uses Diameter+full "
                          "included Angle in degrees."));
        break;
    default:
        break;
    }
}

Feature3D SketchFeatureDialog::result() const {
    Feature3D f;
    f.type = static_cast<FeatureType>(m_typeCombo->currentData().toInt());
    f.sketchIndex = m_sketchCombo->currentIndex() >= 0 ? m_sketchCombo->currentData().toInt() : -1;
    f.pathSketchIndex = m_pathSketchCombo->currentIndex() >= 0 ? m_pathSketchCombo->currentData().toInt() : -1;
    f.inputA = m_targetCombo->currentData().toInt();
    f.cutMode = m_cutModeCheck->isChecked();
    f.p1 = m_p1Spin->value();
    f.p2 = m_p2Spin->value();
    f.p3 = m_p3Spin->value();
    f.p4 = m_p4Spin->value();
    f.count = m_countSpin->value();
    f.posX = m_posX->value();
    f.posY = m_posY->value();
    f.posZ = m_posZ->value();
    f.dirX = m_dirX->value();
    f.dirY = m_dirY->value();
    f.dirZ = m_dirZ->value();
    // A typed index is captured against the target's CURRENT shape as a
    // geometric fingerprint too (see core/core3d/TopoNaming.h) -- the
    // same topological-naming mitigation a real interactive pick would
    // get, applied here since this dialog uses typed indices (cross-
    // referenced via "List Edges.../List Faces...") rather than actual
    // viewport clicking.
    const TopoDS_Shape* targetShape =
        (f.inputA >= 0 && m_document.isValid(f.inputA)) ? &m_document.shapeAt(f.inputA) : nullptr;
    for (const QString& token : m_edgeIndices->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (!ok) continue;
        f.edgeIndices.push_back(value);
        if (targetShape) {
            if (const auto fp = lcad::fingerprintEdge(*targetShape, value)) f.edgeFingerprints.push_back(*fp);
        }
    }
    // If any index failed to fingerprint (target unresolved, or an
    // out-of-range typed index), drop the whole fingerprint list rather
    // than leaving it partially populated -- recomputeOne's own
    // reresolveIndices only trusts fingerprints when the count exactly
    // matches edgeIndices, so a partial list would silently misalign.
    if (targetShape && f.edgeFingerprints.size() != f.edgeIndices.size()) f.edgeFingerprints.clear();

    for (const QString& token : m_faceIndices->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (!ok) continue;
        f.faceIndices.push_back(value);
        if (targetShape) {
            if (const auto fp = lcad::fingerprintFace(*targetShape, value)) f.faceFingerprints.push_back(*fp);
        }
    }
    if (targetShape && f.faceFingerprints.size() != f.faceIndices.size()) f.faceFingerprints.clear();
    for (const QString& token : m_sketchIndices->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = token.trimmed().toInt(&ok);
        if (ok) f.sketchIndices.push_back(value);
    }
    return f;
}
