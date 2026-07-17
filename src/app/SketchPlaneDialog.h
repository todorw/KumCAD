#pragma once

#include "core/sketch/SketchGeometry.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

// Picks one of the three FreeCAD-style base planes (XY/XZ/YZ) plus an
// attachment offset and angle before opening the sketch editor -- see
// core/sketch/SketchGeometry.h's SketchPlane for what these actually mean.
class SketchPlaneDialog : public QDialog {
    Q_OBJECT
public:
    explicit SketchPlaneDialog(QWidget* parent = nullptr);

    lcad::SketchPlane plane() const;

private:
    QComboBox* m_planeCombo;
    QDoubleSpinBox* m_offsetSpin;
    QDoubleSpinBox* m_angleSpin;
};
