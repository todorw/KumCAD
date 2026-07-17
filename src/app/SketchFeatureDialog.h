#pragma once

#include "core/core3d/Document3D.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QLineEdit;

// A single dialog covering every Sprint-3 sketch-based/derived feature type
// (Pad/Pocket, Revolve/Groove, Fillet, Linear/Polar Pattern, Mirror) --
// one dialog with one field set, rather than six bespoke flows, given how
// much each type's parameters overlap. All fields are always visible (not
// shown/hidden per type) to keep this simple; a hint label explains which
// ones actually matter for the currently-selected type. A field irrelevant
// to the selected type is simply ignored when building the Feature3D.
class SketchFeatureDialog : public QDialog {
    Q_OBJECT
public:
    SketchFeatureDialog(const lcad::Document3D& document, QWidget* parent = nullptr);

    lcad::Feature3D result() const;

private:
    void updateHint();

    const lcad::Document3D& m_document;
    QComboBox* m_typeCombo = nullptr;
    QLabel* m_hintLabel = nullptr;
    QComboBox* m_sketchCombo = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QCheckBox* m_cutModeCheck = nullptr;
    QDoubleSpinBox* m_p1Spin = nullptr;
    QSpinBox* m_countSpin = nullptr;
    QDoubleSpinBox* m_posX = nullptr;
    QDoubleSpinBox* m_posY = nullptr;
    QDoubleSpinBox* m_posZ = nullptr;
    QDoubleSpinBox* m_dirX = nullptr;
    QDoubleSpinBox* m_dirY = nullptr;
    QDoubleSpinBox* m_dirZ = nullptr;
    QLineEdit* m_edgeIndices = nullptr;
};
